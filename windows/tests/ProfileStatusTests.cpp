#include "../src/ProfileStatus.h"
#include "TranslationExecutionSupport.h"
#include <string>
#include <vector>

// Settings-window status of the committed bindings: routes, missing Ollama models (shown,
// never substituted), connection-test and preload targets, and the stale-result guard.
namespace execution_test {
namespace {

using namespace profilestatus;

bool Contains(const std::wstring& text, const std::wstring& part) {
    return text.find(part) != std::wstring::npos;
}

TranslationProfile Bound(const char* id, const char* name, TranslationBackend backend, const char* model) {
    TranslationProfile profile = MakeProfile(backend);
    profile.id = id;
    profile.name = name;
    profile.model = model;
    return profile;
}

// Shortcut 1 -> Ollama "missing:1b", Shortcut 2 -> Ollama "qwen", Selection -> OpenAI.
ProfileConfiguration MixedConfiguration() {
    ProfileConfiguration configuration;
    configuration.profiles = {
        Bound("p-missing", "Missing model", TranslationBackend::Ollama, "missing:1b"),
        Bound("p-ollama", "Local Qwen", TranslationBackend::Ollama, "qwen"),
        Bound("p-cloud", "Cloud & more", TranslationBackend::OpenAI, "synthetic-openai-model"),
    };
    configuration.bindings = {"p-missing", "p-ollama", "p-cloud"};
    return configuration;
}

void TestRoutes() {
    const execution::ConnectionSnapshot connection = MakeConnection();
    Check(RouteOf(MakeProfile(TranslationBackend::BuiltIn), connection) == Route::Local,
          "the built-in model is local");
    Check(RouteOf(MakeProfile(TranslationBackend::Ollama), connection) == Route::Local,
          "Ollama on 127.0.0.1 is local");
    execution::ConnectionSnapshot remote = connection;
    remote.ollamaHost = "http://192.0.2.10";
    Check(RouteOf(MakeProfile(TranslationBackend::Ollama), remote) == Route::NetworkUnencrypted,
          "Ollama on another computer over plain HTTP is an unencrypted network route");
    Check(RequiresPreview(MakeProfile(TranslationBackend::Ollama), remote),
          "replacing from an unencrypted network route always shows the popup first");
    execution::ConnectionSnapshot encrypted = connection;
    encrypted.ollamaHost = "https://ollama.example.test";
    Check(RouteOf(MakeProfile(TranslationBackend::Ollama), encrypted) == Route::Network &&
              !RequiresPreview(MakeProfile(TranslationBackend::Ollama), encrypted),
          "Ollama on another computer over HTTPS is a network route without a forced preview");
    Check(!RequiresPreview(MakeProfile(TranslationBackend::Ollama), connection) &&
              !RequiresPreview(MakeProfile(TranslationBackend::OpenAI), connection) &&
              !RequiresPreview(MakeProfile(TranslationBackend::BuiltIn), remote),
          "local, cloud and built-in routes keep the configured preview setting");
    EntryStatus unencrypted;
    unencrypted.entry = ProfileEntry::Selection;
    unencrypted.bound = true;
    unencrypted.profile = MakeProfile(TranslationBackend::Ollama);
    unencrypted.route = Route::NetworkUnencrypted;
    Check(Contains(SummaryLine(unencrypted), L"unencrypted"), "the summary says the route is unencrypted");
    remote.ollamaHost = "http://localhost";
    Check(RouteOf(MakeProfile(TranslationBackend::Ollama), remote) == Route::Local, "Ollama on localhost is local");
    Check(RouteOf(MakeProfile(TranslationBackend::OpenAI), connection) == Route::Cloud,
          "an HTTPS OpenAI-compatible endpoint is cloud");
    execution::ConnectionSnapshot loopback = connection;
    loopback.openAIBaseURL = "http://127.0.0.1:1234/v1";
    Check(RouteOf(MakeProfile(TranslationBackend::OpenAI), loopback) == Route::Local,
          "an OpenAI-compatible server on loopback HTTP is local");
    loopback.openAIBaseURL = "https://localhost.example.test/v1";
    Check(RouteOf(MakeProfile(TranslationBackend::OpenAI), loopback) == Route::Cloud,
          "a look-alike host name is still cloud");
}

void TestInstalledMatching() {
    const std::vector<std::wstring> installed = {L"qwen:latest", L"llama3.2:3b"};
    Check(IsInstalled(installed, "llama3.2:3b"), "an exact tag is installed");
    Check(IsInstalled(installed, "qwen"), "a name without a tag matches its :latest install");
    Check(IsInstalled(installed, "qwen:latest"), "an explicit :latest matches");
    Check(!IsInstalled(installed, "llama3.2:1b"), "another tag of the same model is not installed");
    Check(!IsInstalled(installed, "llama3.2"), "a tagless name never matches a non-latest tag");
    Check(!IsInstalled(installed, ""), "an empty model is never installed");
    Check(!IsInstalled({}, "qwen"), "nothing is installed in an empty list");
}

void TestDescribe() {
    const ProfileConfiguration configuration = MixedConfiguration();
    const std::vector<std::wstring> installed = {L"qwen:latest", L"other:7b"};
    const std::vector<EntryStatus> known = Describe(configuration, MakeConnection(), &installed);
    if (!Check(known.size() == 3, "three entries are described")) return;
    Check(known[0].entry == ProfileEntry::Shortcut1 && known[1].entry == ProfileEntry::Shortcut2 &&
              known[2].entry == ProfileEntry::Selection,
          "entries come in Shortcut 1, Shortcut 2, Selection order");
    Check(known[0].bound && known[0].profile.id == "p-missing", "shortcut 1 shows its bound profile");
    Check(known[0].model == ModelState::Missing, "a bound model that is not installed is reported missing");
    Check(known[0].profile.model == "missing:1b", "a missing model is reported as configured, not replaced");
    Check(known[1].model == ModelState::Installed, "an installed bound model is reported installed");
    Check(known[2].model == ModelState::NotApplicable && known[2].route == Route::Cloud,
          "a cloud profile has no discovery state and a cloud route");

    const std::vector<EntryStatus> unknown = Describe(configuration, MakeConnection(), nullptr);
    Check(unknown.size() == 3 && unknown[0].model == ModelState::Unknown && unknown[1].model == ModelState::Unknown,
          "Ollama models are unknown until the list arrives");

    ProfileConfiguration broken = configuration;
    broken.bindings.selectionProfileId = "deleted-profile";
    const std::vector<EntryStatus> dangling = Describe(broken, MakeConnection(), &installed);
    Check(dangling.size() == 3 && !dangling[2].bound, "a binding to a missing profile is reported unbound");
    Check(Describe(profiles::Defaults(), MakeConnection(), nullptr).at(0).model == ModelState::NotApplicable,
          "the built-in model needs no discovery");
}

void TestProbesAndPreload() {
    ProfileConfiguration configuration = MixedConfiguration();
    configuration.bindings.selectionProfileId = "p-ollama";  // the same model on two entries
    const std::vector<Probe> probes = BoundProbes(configuration);
    Check(probes.size() == 2 && probes[0] == Probe{TranslationBackend::Ollama, "missing:1b"} &&
              probes[1] == Probe{TranslationBackend::Ollama, "qwen"},
          "connection tests use each bound backend and model once, in entry order");
    ProfileConfiguration shared = configuration;
    shared.profiles[1].backend = TranslationBackend::OpenAI;
    shared.profiles[1].model = "missing:1b";
    Check(BoundProbes(shared).size() == 2, "the same model name on another backend is a separate test");
    Check(BoundProbes(profiles::Defaults()) == std::vector<Probe>{Probe{TranslationBackend::BuiltIn, ""}},
          "two profiles on the built-in model need one test");
    ProfileConfiguration dangling = configuration;
    dangling.bindings = {"gone", "gone", "gone"};
    Check(BoundProbes(dangling).empty(), "unresolved bindings have nothing to test");

    const std::vector<std::wstring> installed = {L"qwen:latest"};
    Check(PreloadModels(configuration, &installed) == std::vector<std::string>{"qwen"},
          "preload skips a missing model and warms each installed bound model once");
    Check(PreloadModels(configuration, nullptr) == std::vector<std::string>{"missing:1b", "qwen"},
          "with an unknown list every bound Ollama model is preloaded, never another");
    Check(PreloadModels(MixedConfiguration(), nullptr).size() == 2, "cloud and built-in models are never preloaded");
    Check(PreloadModels(profiles::Defaults(), nullptr).empty(), "the default configuration preloads nothing");
}

void TestLines() {
    const std::vector<std::wstring> installed = {L"qwen:latest"};
    const std::vector<EntryStatus> statuses = Describe(MixedConfiguration(), MakeConnection(), &installed);
    if (!Check(statuses.size() == 3, "lines: three entries")) return;
    const std::wstring missing = SummaryLine(statuses[0]);
    Check(Contains(missing, L"Shortcut 1") && Contains(missing, L"Missing model") && Contains(missing, L"Ollama") &&
              Contains(missing, L"missing:1b") && Contains(missing, L"local") && Contains(missing, L"not installed"),
          "a missing model's line names the entry, profile, backend, model, route and the problem");
    const std::wstring local = SummaryLine(statuses[1]);
    Check(Contains(local, L"Shortcut 2") && Contains(local, L"qwen") && !Contains(local, L"not installed"),
          "an installed model's line has no warning");
    const std::wstring cloud = SummaryLine(statuses[2]);
    Check(Contains(cloud, L"Selection") && Contains(cloud, L"Cloud & more") && Contains(cloud, L"cloud"),
          "a cloud profile's line says cloud and keeps the name exactly");
    EntryStatus unbound;
    unbound.entry = ProfileEntry::Shortcut2;
    Check(Contains(SummaryLine(unbound), L"Shortcut 2") && Contains(SummaryLine(unbound), L"No profile"),
          "an unbound entry says no profile is bound");
    EntryStatus builtIn = Describe(profiles::Defaults(), MakeConnection(), nullptr).at(0);
    Check(Contains(SummaryLine(builtIn), L"Built-in") && Contains(SummaryLine(builtIn), L"local"),
          "a built-in profile's line says Built-in and local");
    Check(Contains(LanguageLine(builtIn), L"中文") && Contains(LanguageLine(builtIn), L"English") &&
              Contains(LanguageLine(builtIn), lang::StyleDisplayName(RewriteStyle::Faithful)),
          "the language line shows the pair and the preset style");
    builtIn.profile.promptMode = PromptMode::Custom;
    Check(Contains(LanguageLine(builtIn), L"custom prompt"), "a custom profile's language line says custom prompt");
    builtIn.profile.direction = TranslationDirection::NativeToForeign;
    Check(Contains(LanguageLine(builtIn), L"中文 → English"), "a fixed direction is shown as an arrow");
}

void TestResultGuard() {
    ResultGuard guard;
    Check(!guard.accepts(Ticket{}, 0), "nothing is accepted before a request starts");
    const Ticket first = guard.begin(5);
    Check(guard.accepts(first, 5), "the latest result on the same connection is accepted");
    const Ticket second = guard.begin(5);
    Check(!guard.accepts(first, 5), "an older request's result is dropped");
    Check(guard.accepts(second, 5), "the newest request is still accepted");
    Check(!guard.accepts(second, 6), "a result from before a connection change is dropped");
    guard.invalidate();
    Check(!guard.accepts(second, 5), "invalidate drops every request in flight");
    Check(guard.accepts(guard.begin(6), 6), "a request started after invalidate is accepted");
}

} // namespace

void RunProfileStatusTests() {
    TestRoutes();
    TestInstalledMatching();
    TestDescribe();
    TestProbesAndPreload();
    TestLines();
    TestResultGuard();
}

} // namespace execution_test
