#include "TranslationExecutionSupport.h"
#include <string>

// Cache identity isolation + BuiltIn execution. Every comparison goes through
// IdentityOf/CheckSame/CheckDiffers, which first require Ok and non-empty values
// so an unimplemented (NotReady, empty) result can never compare "equal".
namespace execution_test {
namespace {

using execution::ConnectionSnapshot;

TranslationProfile CustomOpenAI(const char* prompt) {
    TranslationProfile profile = MakeProfile(TranslationBackend::OpenAI);
    profile.promptMode = PromptMode::Custom;
    profile.systemPrompt = prompt;
    return profile;
}

std::string OpenAIId(const TranslationProfile& profile, const ConnectionSnapshot& connection,
                     const std::wstring& text = kEnglishText,
                     ShortcutAction action = ShortcutAction::SmartPopup) {
    return IdentityOf(profile, action, connection, text);
}

void TestStableAndRenameSafe() {
    const TranslationProfile base = MakeProfile(TranslationBackend::OpenAI);
    const std::string id = OpenAIId(base, MakeConnection());
    CheckSame(id, OpenAIId(base, MakeConnection()), "identity is deterministic");
    TranslationProfile renamed = base;
    renamed.name = "A completely different display name";
    CheckSame(id, OpenAIId(renamed, MakeConnection()), "renaming a profile does not invalidate its cache");
    renamed.id = "another-profile-id";
    CheckSame(id, OpenAIId(renamed, MakeConnection()), "the profile id is not part of the identity");
    Check(id.find(kProfileId) == std::string::npos && id.find(kProfileName) == std::string::npos,
          "identity text carries neither profile id nor display name");
}

void TestProfileEditsInvalidate() {
    const TranslationProfile base = MakeProfile(TranslationBackend::OpenAI);
    const std::string id = OpenAIId(base, MakeConnection());
    TranslationProfile edited = base;
    edited.model = "synthetic-openai-model-2";
    CheckDiffers(id, OpenAIId(edited, MakeConnection()), "changing the model changes the identity");
    edited = base;
    edited.presetStyle = RewriteStyle::Formal;
    CheckDiffers(id, OpenAIId(edited, MakeConnection()), "changing the preset style changes the identity");
    edited = base;
    edited.promptMode = PromptMode::Custom;
    edited.systemPrompt = "Translate it.";
    const std::string custom = OpenAIId(edited, MakeConnection());
    CheckDiffers(id, custom, "switching preset to custom changes the identity");
    edited.systemPrompt = "Translate it!";
    CheckDiffers(custom, OpenAIId(edited, MakeConnection()), "editing the custom prompt changes the identity");
    CheckDiffers(id, OpenAIId(base, MakeConnection(), L"A different selected text"),
                 "changing the selected text changes the identity");
}

void TestInactiveBranchIgnored() {
    TranslationProfile preset = MakeProfile(TranslationBackend::OpenAI);
    const std::string presetId = OpenAIId(preset, MakeConnection());
    preset.systemPrompt = "an unused custom draft";
    CheckSame(presetId, OpenAIId(preset, MakeConnection()), "an unused custom draft never changes a preset identity");
    TranslationProfile custom = CustomOpenAI("Translate it.");
    const std::string customId = OpenAIId(custom, MakeConnection());
    custom.presetStyle = RewriteStyle::Polished;
    CheckSame(customId, OpenAIId(custom, MakeConnection()), "an unused preset style never changes a custom identity");
}

void TestDirectionInIdentity() {
    const TranslationProfile base = MakeProfile(TranslationBackend::OpenAI);
    const ConnectionSnapshot connection = MakeConnection();
    CheckDiffers(IdentityOf(base, ShortcutAction::NativePopup, connection, kUndetectable),
                 IdentityOf(base, ShortcutAction::ForeignPopup, connection, kUndetectable),
                 "different resolved targets give different identities");
    CheckDiffers(IdentityOf(base, ShortcutAction::SmartPopup, connection, kEnglishText),
                 IdentityOf(base, ShortcutAction::SmartPopup, connection, kEnglishText, Language::Russian),
                 "a retarget override changes the identity");
    CheckDiffers(IdentityOf(base, ShortcutAction::ForeignPopup, connection, kChineseText),
                 IdentityOf(base, ShortcutAction::ForeignPopup, connection, kUndetectable),
                 "different sources (zh vs unknown) give different identities");
}

void TestConnectionAndEpoch() {
    const TranslationProfile base = MakeProfile(TranslationBackend::OpenAI);
    ConnectionSnapshot connection = MakeConnection();
    const std::string id = OpenAIId(base, connection);
    connection.openAIBaseURL = "https://other.example.test/v1";
    CheckDiffers(id, OpenAIId(base, connection), "a different actual endpoint changes the identity");
    connection = MakeConnection();
    connection.connectionEpoch = 8;
    CheckDiffers(id, OpenAIId(base, connection), "a new connection epoch changes the identity");
    connection = MakeConnection();
    connection.openAIKey = "a-rotated-synthetic-key";
    CheckSame(id, OpenAIId(base, connection), "key rotation alone is signalled by the epoch, not the key");
    ConnectionSnapshot low = MakeConnection();
    ConnectionSnapshot high = MakeConnection();
    low.connectionEpoch = 9007199254740992ULL;
    high.connectionEpoch = 9007199254740993ULL;
    CheckDiffers(OpenAIId(base, low), OpenAIId(base, high), "epochs above 2^53 stay distinct (decimal, not double)");
}

void TestOnlyEffectiveEndpointCounts() {
    const TranslationProfile ollama = MakeProfile(TranslationBackend::Ollama);
    ConnectionSnapshot connection = MakeConnection();
    const std::string id = IdentityOf(ollama, ShortcutAction::SmartPopup, connection, kEnglishText);
    connection.openAIBaseURL = "https://other.example.test/v1";
    CheckSame(id, IdentityOf(ollama, ShortcutAction::SmartPopup, connection, kEnglishText),
              "an unrelated OpenAI endpoint does not change an Ollama identity");
    connection = MakeConnection();
    connection.ollamaPort = 11435;
    CheckDiffers(id, IdentityOf(ollama, ShortcutAction::SmartPopup, connection, kEnglishText),
                 "the effective Ollama port changes the identity");
    connection = MakeConnection();
    connection.ollamaHost = "http://localhost";
    CheckDiffers(id, IdentityOf(ollama, ShortcutAction::SmartPopup, connection, kEnglishText),
                 "the effective Ollama host changes the identity");
    CheckDiffers(id, IdentityOf(MakeProfile(TranslationBackend::OpenAI), ShortcutAction::SmartPopup,
                                MakeConnection(), kEnglishText),
                 "different backends never share an identity");
}

void TestLengthSafeEncoding() {
    const ConnectionSnapshot connection = MakeConnection();
    const std::string a = OpenAIId(CustomOpenAI("x|y"), connection, L"z");
    const std::string b = OpenAIId(CustomOpenAI("x"), connection, L"y|z");
    CheckDiffers(a, b, "delimiter characters in prompt/text cannot forge another request");
    const std::string c = OpenAIId(CustomOpenAI("ab"), connection, L"c");
    const std::string d = OpenAIId(CustomOpenAI("a"), connection, L"bc");
    CheckDiffers(c, d, "moving a character between system and text changes the identity");
}

void TestNoSecretsInOutputs() {
    const execution::PreparedRequest r = RunPrepare(MakeProfile(TranslationBackend::OpenAI),
                                                    ShortcutAction::SmartPopup, MakeConnection(), kEnglishText);
    if (!CheckPrepared(r, "key-hygiene request prepares")) return;
    const std::string key = kFakeKey;
    Check(r.cacheIdentity.find(key) == std::string::npos && r.body.find(key) == std::string::npos &&
              r.system.find(key) == std::string::npos && r.user.find(key) == std::string::npos,
          "the API key never appears in identity, body or messages");
    for (const std::string& warning : r.warnings)
        Check(warning.find(key) == std::string::npos, "the API key never appears in warnings");
}

void TestOllamaRetryDiffersFromBody() {
    const execution::PreparedRequest r = RunPrepare(MakeProfile(TranslationBackend::Ollama),
                                                    ShortcutAction::SmartPopup, MakeConnection(), kEnglishText);
    if (!CheckPrepared(r, "Ollama request for retry hygiene prepares")) return;
    Check(!r.retryBody.empty() && r.retryBody != r.body, "Ollama exposes a distinct retry body");
}

void TestBuiltInFaithful() {
    const TranslationProfile profile = MakeProfile(TranslationBackend::BuiltIn);
    const execution::PreparedRequest r =
        RunPrepare(profile, ShortcutAction::SmartPopup, ConnectionSnapshot{}, kEnglishText);
    if (!CheckPrepared(r, "BuiltIn faithful preset prepares with no endpoint or key")) return;
    Check(r.backend == TranslationBackend::BuiltIn && r.body.empty() && r.retryBody.empty() &&
              r.finalUrl.empty(),
          "BuiltIn has no HTTP body or URL");
    Check(r.user == "The cat is in the house and it is that", "BuiltIn receives the raw UTF-8 text");
    Check(r.direction.ready && r.direction.target == Language::Chinese && r.direction.source == Language::English,
          "BuiltIn direction is resolved like any other profile");
    Check(r.cacheIdentity.find(execution::kBuiltInModelRevision) != std::string::npos,
          "BuiltIn identity names the pinned model revision");
    TranslationProfile otherPair = profile;
    otherPair.nativeLanguage = Language::French;
    CheckDiffers(r.cacheIdentity, IdentityOf(otherPair, ShortcutAction::SmartPopup, ConnectionSnapshot{}, kEnglishText),
                 "BuiltIn identity follows the resolved target");
    ConnectionSnapshot unrelated;
    unrelated.ollamaHost = "http://elsewhere.example.test";
    CheckSame(r.cacheIdentity, IdentityOf(profile, ShortcutAction::SmartPopup, unrelated, kEnglishText),
              "unrelated Ollama connection values do not change a BuiltIn identity");
}

} // namespace

void RunBuiltInTests() {
    TestBuiltInFaithful();
}

void RunCacheTests() {
    TestStableAndRenameSafe();
    TestProfileEditsInvalidate();
    TestInactiveBranchIgnored();
    TestDirectionInIdentity();
    TestConnectionAndEpoch();
    TestOnlyEffectiveEndpointCounts();
    TestLengthSafeEncoding();
    TestNoSecretsInOutputs();
    TestOllamaRetryDiffersFromBody();
}

} // namespace execution_test
