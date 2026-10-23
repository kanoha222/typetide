#include "RuntimeSelfTest.h"
#include "Json.h"
#include "Settings.h"
#include "TranslationExecution.h"
#include "Translator.h"
#include "Util.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

// Loopback mock only, synthetic text and a synthetic key: never a real provider, credential
// or user selection. The mock records each request (path, raw body, whether an
// Authorization header was present) as one JSON line, never the header value.
namespace {

int g_failures = 0;

bool check(bool cond, const char* name, const std::string& detail = {}) {
    if (cond) {
        printf("  ok   %s\n", name);
    } else {
        ++g_failures;
        printf("  FAIL %s%s%s\n", name, detail.empty() ? "" : " -- ", detail.c_str());
    }
    return cond;
}

constexpr char kKey[] = "synthetic-runtime-key";
constexpr wchar_t kExactReply[] = L"  EXACT_OUTPUT\n\tline 2\n";
constexpr wchar_t kDefaultReply[] = L"MOCK_TRANSLATION_OK";
constexpr char kTemplate[] = "Translate {{source_language}} into {{target_language}}.";

struct MockEnv {
    int port = 0;
    std::wstring recordPath;
};

std::wstring EnvValue(const wchar_t* name) {
    wchar_t buffer[1024]{};
    const DWORD length = GetEnvironmentVariableW(name, buffer, 1024);
    return length && length < 1024 ? std::wstring(buffer, length) : std::wstring();
}

struct Recorded {
    std::string path;
    std::string body;
    bool auth = false;
};

std::vector<Recorded> ReadRecords(const MockEnv& env) {
    std::vector<Recorded> records;
    std::ifstream in(env.recordPath, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        bool ok = false;
        const json::Value value = json::Parse(line, &ok);
        if (ok) records.push_back({value["path"].asString(), value["body"].asString(), value["auth"].asBool()});
    }
    return records;
}

std::string Base(const MockEnv& env) {
    return "http://127.0.0.1:" + std::to_string(env.port) + "/v1";
}

execution::ConnectionSnapshot Connection(const MockEnv& env, uint64_t epoch = 1) {
    execution::ConnectionSnapshot connection;
    connection.connectionEpoch = epoch;
    connection.ollamaHost = "http://127.0.0.1";
    connection.ollamaPort = env.port;
    connection.openAIBaseURL = Base(env);
    connection.openAIKey = kKey;
    return connection;
}

TranslationProfile Profile(TranslationBackend backend, const char* model, bool custom) {
    TranslationProfile profile;
    profile.id = "runtime-test";
    profile.name = "Runtime test";
    profile.promptMode = custom ? PromptMode::Custom : PromptMode::Preset;
    profile.systemPrompt = custom ? kTemplate : "";
    profile.backend = backend;
    profile.model = model;
    return profile;
}

struct Outcome {
    std::optional<std::wstring> text;
    std::wstring error;
};

Outcome Run(const TranslationProfile& profile, const execution::ConnectionSnapshot& connection,
            const std::wstring& text) {
    Outcome outcome;
    outcome.text = translator::TranslateFully(
        execution::Freeze(profile, ShortcutAction::SmartPopup, connection), text, &outcome.error);
    return outcome;
}

execution::PreparedRequest Prepared(const TranslationProfile& profile,
                                    const execution::ConnectionSnapshot& connection,
                                    const std::wstring& text) {
    return execution::Prepare(*execution::Freeze(profile, ShortcutAction::SmartPopup, connection), text);
}

void testPresetOpenAI(const MockEnv& env) {
    printf("[preset OpenAI]\n");
    const TranslationProfile profile = Profile(TranslationBackend::OpenAI, "mock-default", false);
    const std::wstring text = L"The cat is in the house and it is that (preset)";
    const size_t before = ReadRecords(env).size();
    const Outcome outcome = Run(profile, Connection(env), text);
    check(outcome.text == std::optional<std::wstring>(kDefaultReply), "translation completes",
          util::Narrow(outcome.error));
    const std::vector<Recorded> records = ReadRecords(env);
    if (!check(records.size() == before + 1, "exactly one request is sent")) return;
    const Recorded& request = records.back();
    check(request.path == "/v1/chat/completions", "request path", request.path);
    check(request.body == Prepared(profile, Connection(env), text).body, "wire body equals the prepared body");
    check(request.auth, "the Authorization header is present");
}

void testExactOutput(const MockEnv& env) {
    printf("[exact output]\n");
    const std::wstring text = L"The cat is in the house and it is that (exact)";
    const Outcome custom = Run(Profile(TranslationBackend::OpenAI, "mock-exact", true), Connection(env), text);
    check(custom.text == std::optional<std::wstring>(kExactReply),
          "custom output keeps indentation, tabs and the trailing newline", util::Narrow(custom.error));
    const Outcome preset = Run(Profile(TranslationBackend::OpenAI, "mock-exact", false), Connection(env), text);
    check(preset.text == std::optional<std::wstring>(L"EXACT_OUTPUT\n\tline 2"),
          "preset output keeps the legacy trim", util::Narrow(preset.error));
}

void testOllamaCustom(const MockEnv& env) {
    printf("[custom Ollama]\n");
    const TranslationProfile profile = Profile(TranslationBackend::Ollama, "mock-exact", true);
    const std::wstring text = L"The cat is in the house and it is that (ollama)";
    const size_t before = ReadRecords(env).size();
    const Outcome outcome = Run(profile, Connection(env), text);
    check(outcome.text == std::optional<std::wstring>(kExactReply), "custom Ollama output is exact",
          util::Narrow(outcome.error));
    const std::vector<Recorded> records = ReadRecords(env);
    if (!check(records.size() == before + 1, "exactly one Ollama request is sent")) return;
    const Recorded& request = records.back();
    check(request.path == "/api/generate", "Ollama request path", request.path);
    check(request.body == Prepared(profile, Connection(env), text).body, "Ollama wire body equals the prepared body");
    const json::Value body = json::Parse(request.body);
    check(body["system"].asString() == "Translate English into Chinese." &&
              body["prompt"].asString() == util::Narrow(text),
          "system carries the rendered template and prompt the original text");
    check(!request.auth, "Ollama requests never carry an Authorization header");
}

void testThinkRetry(const MockEnv& env) {
    printf("[Ollama think retry]\n");
    const TranslationProfile profile = Profile(TranslationBackend::Ollama, "mock-reject-think", false);
    const std::wstring text = L"The cat is in the house and it is that (think)";
    const size_t before = ReadRecords(env).size();
    const Outcome outcome = Run(profile, Connection(env), text);
    check(outcome.text == std::optional<std::wstring>(kDefaultReply), "HTTP 400 on think is retried without it",
          util::Narrow(outcome.error));
    const std::vector<Recorded> records = ReadRecords(env);
    if (!check(records.size() == before + 2, "exactly two requests are sent")) return;
    const execution::PreparedRequest prepared = Prepared(profile, Connection(env), text);
    check(records[before].body == prepared.body, "the first request is the initial body");
    check(records[before + 1].body == prepared.retryBody, "the retry is the prepared retry body");
}

void testCacheAndEpoch(const MockEnv& env) {
    printf("[cache and connection epoch]\n");
    const TranslationProfile profile = Profile(TranslationBackend::OpenAI, "mock-exact", true);
    const std::wstring text = L"The cat is in the house and it is that (cache)";
    const size_t before = ReadRecords(env).size();
    const Outcome first = Run(profile, Connection(env, 1), text);
    const Outcome second = Run(profile, Connection(env, 1), text);
    check(first.text && first.text == second.text, "a cache hit returns the identical exact output",
          util::Narrow(first.error));
    check(ReadRecords(env).size() == before + 1, "the cache hit sends no request");
    const Outcome renewed = Run(profile, Connection(env, 2), text);
    check(renewed.text == std::optional<std::wstring>(kExactReply), "a new connection epoch translates again",
          util::Narrow(renewed.error));
    check(ReadRecords(env).size() == before + 2, "the new epoch misses the old cache entry");
}

void testRedirectRefused(const MockEnv& env) {
    printf("[redirects]\n");
    const Outcome outcome = Run(Profile(TranslationBackend::OpenAI, "mock-redirect", false), Connection(env),
                                L"The cat is in the house and it is that (redirect)");
    check(!outcome.text && outcome.error.find(L"307") != std::wstring::npos,
          "a provider redirect is reported, not followed", util::Narrow(outcome.error));
    bool followed = false;
    for (const Recorded& request : ReadRecords(env))
        followed = followed || request.path.rfind("/redirected", 0) == 0;
    check(!followed, "the redirect target is never requested");
}

void testRemoteHttpRefused(const MockEnv& env) {
    printf("[remote HTTP refusal]\n");
    execution::ConnectionSnapshot connection = Connection(env);
    connection.openAIBaseURL = "http://192.0.2.10/v1";
    const size_t before = ReadRecords(env).size();
    const Outcome outcome = Run(Profile(TranslationBackend::OpenAI, "mock-default", false), connection,
                                L"The cat is in the house and it is that (insecure)");
    const std::wstring expected =
        execution::PrepareErrorMessage(execution::PrepareStatus::EndpointInsecure, TranslationBackend::OpenAI);
    check(!outcome.text && !expected.empty() && outcome.error == expected,
          "remote plain HTTP is refused with the fixed message", util::Narrow(outcome.error));
    check(ReadRecords(env).size() == before, "a refused request never reaches any server");
}

void testLongUrl(const MockEnv& env) {
    printf("[long URL]\n");
    std::string base = "http://127.0.0.1:" + std::to_string(env.port) + "/long";
    for (int i = 0; i < 200; ++i) base += "/segment-" + std::to_string(100 + i);
    execution::ConnectionSnapshot connection = Connection(env);
    connection.openAIBaseURL = base;
    const Outcome outcome = Run(Profile(TranslationBackend::OpenAI, "mock-default", false), connection,
                                L"The cat is in the house and it is that (long)");
    check(base.size() > 2048, "the base path exceeds the old 2048-character buffer");
    check(outcome.text == std::optional<std::wstring>(kDefaultReply), "a long provider URL is sent intact",
          util::Narrow(outcome.error));
    const std::vector<Recorded> records = ReadRecords(env);
    check(!records.empty() && records.back().path == base.substr(base.find("/long")) + "/chat/completions",
          "the full path reaches the server");
}

bool CommitBoundProfile(const MockEnv& env, Settings& settings) {
    Settings candidate = settings;
    candidate.openAIBaseURL = Base(env);
    candidate.openAIKey = kKey;
    TranslationProfile bound = Profile(TranslationBackend::OpenAI, "mock-exact", true);
    bound.id = "runtime-bound";
    candidate.profileConfig.profiles.push_back(bound);
    candidate.profileConfig.bindings.shortcut1ProfileId = bound.id;
    std::string error;
    return check(settings.commit(candidate, &error), "commit a custom profile bound to shortcut 1", error);
}

void testCommittedBinding(const MockEnv& env) {
    printf("[committed binding]\n");
    Settings& settings = Settings::shared();
    settings.load();
    if (!CommitBoundProfile(env, settings)) return;
    const execution::ContextPtr context = translator::FreezeEntry(ProfileEntry::Shortcut1, ShortcutAction::SmartPopup);
    if (!check(context && context->profile.id == "runtime-bound", "shortcut 1 freezes its committed profile")) return;
    check(context->connection.openAIBaseURL == Base(env) && context->connection.openAIKey == kKey &&
              context->connection.connectionEpoch == settings.connectionEpoch(),
          "the frozen connection is the committed one");
    std::wstring error;
    const std::optional<std::wstring> text =
        translator::TranslateFully(context, L"The cat is in the house and it is that (bound)", &error);
    check(text == std::optional<std::wstring>(kExactReply), "the bound custom profile runs end to end",
          util::Narrow(error));
    settings.openAIBaseURL = "http://192.0.2.10/v1";  // uncommitted draft only
    const execution::ContextPtr later = translator::FreezeEntry(ProfileEntry::Shortcut1, ShortcutAction::SmartPopup);
    check(later && later->connection.openAIBaseURL == Base(env), "uncommitted drafts never reach a frozen entry");
    settings.openAIBaseURL = Base(env);
}

} // namespace

int RunRuntimeSelfTests() {
    printf("TypeTide runtime self-test\n");
    MockEnv env;
    env.port = _wtoi(EnvValue(L"TYPETIDE_MOCK_PORT").c_str());
    env.recordPath = EnvValue(L"TYPETIDE_MOCK_RECORD");
    if (env.port <= 0 || env.recordPath.empty() || EnvValue(L"TYPETIDE_DATA_DIR").empty()) {
        printf("TYPETIDE_MOCK_PORT, TYPETIDE_MOCK_RECORD and TYPETIDE_DATA_DIR are required\n");
        return 2;
    }
    translator::ClearCacheForTesting();
    testPresetOpenAI(env);
    testExactOutput(env);
    testOllamaCustom(env);
    testThinkRetry(env);
    testCacheAndEpoch(env);
    testRedirectRefused(env);
    testRemoteHttpRefused(env);
    testLongUrl(env);
    testCommittedBinding(env);
    printf(g_failures ? "\n%d FAILURE(S)\n" : "\nall runtime tests passed\n", g_failures);
    return g_failures ? 1 : 0;
}
