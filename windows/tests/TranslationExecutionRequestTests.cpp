#include "TranslationExecutionGoldens.h"
#include "TranslationExecutionSupport.h"
#include <cmath>
#include <initializer_list>
#include <string>

// Provider messages/bodies, Preview-vs-Prepare separation and capability gates.
namespace execution_test {
namespace {

using execution::PrepareStatus;
using execution::PreparedRequest;

constexpr const char* kEnglishUtf8 = "The cat is in the house and it is that";
constexpr const char* kCustomTemplate = "Translate {{source_language}} into {{target_language}} politely.";
constexpr const char* kCustomRendered = "Translate English into Chinese politely.";

TranslationProfile CustomProfile(TranslationBackend backend, const char* prompt) {
    TranslationProfile profile = MakeProfile(backend);
    profile.promptMode = PromptMode::Custom;
    profile.systemPrompt = prompt;
    return profile;
}

bool IsBool(const json::Value& value, bool expected) {
    return value.type() == json::Value::Type::Bool && value.asBool(!expected) == expected;
}

bool IsNumber(const json::Value& value, double expected) {
    return value.type() == json::Value::Type::Number && std::fabs(value.asNumber() - expected) < 1e-9;
}

void CheckMessages(const json::Value& body, const std::string& system, const std::string& user,
                   const char* name) {
    const json::Value& messages = body["messages"];
    Check(messages.size() == 2, name);
    Check(messages.at(0)["role"].asString() == "system" && messages.at(0)["content"].asString() == system,
          "OpenAI messages[0] is the system role with the exact system text");
    Check(messages.at(1)["role"].asString() == "user" && messages.at(1)["content"].asString() == user,
          "OpenAI messages[1] is the user role with the exact user text");
}

void CheckOllamaFields(const json::Value& body, bool expectThink, const char* name) {
    Check(body["model"].asString() == "synthetic-ollama-model", name);
    Check(IsBool(body["stream"], true) && body["keep_alive"].asString() == "10m",
          "Ollama body keeps stream and keep_alive");
    Check(IsNumber(body["options"]["temperature"], 0.3) && IsNumber(body["options"]["top_p"], 0.9) &&
              IsNumber(body["options"]["top_k"], 40),
          "Ollama body keeps the existing sampling options");
    Check(expectThink ? IsBool(body["think"], false) : body["think"].isNull(),
          expectThink ? "initial Ollama body disables thinking" : "retry Ollama body drops think");
    Check(body["raw"].isNull(), "Ollama body never sets raw");
}

void TestPresetOpenAI() {
    const PreparedRequest r = RunPrepare(MakeProfile(TranslationBackend::OpenAI),
                                         ShortcutAction::SmartPopup, MakeConnection(), kEnglishText);
    if (!CheckPrepared(r, "preset OpenAI prepares")) return;
    Check(r.backend == TranslationBackend::OpenAI, "preset OpenAI backend");
    Check(r.system == GOLD_SYS_EN_ZH_FAITHFUL, "preset OpenAI system equals the legacy SystemPrompt golden");
    Check(r.user == kEnglishUtf8, "preset OpenAI user is the raw selected text");
    Check(r.finalUrl == L"https://api.example.test/v1/chat/completions", "preset OpenAI final URL");
    Check(r.retryBody.empty(), "OpenAI has no retry body");
    const json::Value body = ParseJson(r.body, "preset OpenAI body is a JSON object");
    Check(body.size() == 3 && body["model"].asString() == "synthetic-openai-model" &&
              IsBool(body["stream"], true),
          "preset OpenAI body keeps exactly model/stream/messages");
    CheckMessages(body, r.system, r.user, "preset OpenAI sends two messages");
}

void TestPresetOllama() {
    const PreparedRequest r = RunPrepare(MakeProfile(TranslationBackend::Ollama),
                                         ShortcutAction::SmartPopup, MakeConnection(), kEnglishText);
    if (!CheckPrepared(r, "preset Ollama prepares")) return;
    const std::string plain =
        std::string(GOLD_PLAIN_HEAD "English to Chinese.") + GOLD_PLAIN_TAIL + kEnglishUtf8;
    Check(r.system.empty(), "preset Ollama sends no separate system");
    Check(r.user == plain, "preset Ollama prompt equals the legacy PlainPrompt golden");
    Check(r.finalUrl == L"http://127.0.0.1:11434/api/generate", "preset Ollama final URL");
    const json::Value body = ParseJson(r.body, "preset Ollama body is a JSON object");
    Check(body.size() == 6 && body["prompt"].asString() == plain && body["system"].isNull(),
          "preset Ollama body keeps exactly model/prompt/stream/keep_alive/think/options");
    CheckOllamaFields(body, true, "preset Ollama model");
}

void TestPresetFormalStyle() {
    TranslationProfile profile = MakeProfile(TranslationBackend::Ollama);
    profile.presetStyle = RewriteStyle::Formal;
    const PreparedRequest r = RunPrepare(profile, ShortcutAction::SmartPopup, MakeConnection(), kEnglishText);
    if (!CheckPrepared(r, "preset Formal Ollama prepares")) return;
    Check(r.user == std::string(GOLD_PLAIN_HEAD "English to Chinese.") + GOLD_STYLE_FORMAL +
                        GOLD_PLAIN_TAIL + kEnglishUtf8,
          "preset Formal appends the style instruction before the tail");
}

void TestCustomOpenAI() {
    const PreparedRequest r = RunPrepare(CustomProfile(TranslationBackend::OpenAI, kCustomTemplate),
                                         ShortcutAction::SmartPopup, MakeConnection(), kEnglishText);
    if (!CheckPrepared(r, "custom OpenAI prepares")) return;
    Check(r.system == kCustomRendered, "custom OpenAI system is the rendered template only");
    Check(r.user == kEnglishUtf8, "custom OpenAI user is the original text");
    const json::Value body = ParseJson(r.body, "custom OpenAI body is a JSON object");
    Check(body.size() == 3, "custom OpenAI body keeps exactly model/stream/messages");
    CheckMessages(body, kCustomRendered, kEnglishUtf8, "custom OpenAI sends two messages");
}

void TestCustomOllama() {
    const PreparedRequest r = RunPrepare(CustomProfile(TranslationBackend::Ollama, kCustomTemplate),
                                         ShortcutAction::SmartPopup, MakeConnection(), kEnglishText);
    if (!CheckPrepared(r, "custom Ollama prepares")) return;
    Check(r.system == kCustomRendered && r.user == kEnglishUtf8,
          "custom Ollama system is the template, prompt is the original text");
    const json::Value body = ParseJson(r.body, "custom Ollama body is a JSON object");
    Check(body.size() == 7 && body["system"].asString() == kCustomRendered &&
              body["prompt"].asString() == kEnglishUtf8,
          "custom Ollama body separates system and prompt");
    CheckOllamaFields(body, true, "custom Ollama model");
}

void TestThinkRetryBody() {
    for (bool custom : {false, true}) {
        const TranslationProfile profile = custom ? CustomProfile(TranslationBackend::Ollama, kCustomTemplate)
                                                  : MakeProfile(TranslationBackend::Ollama);
        const PreparedRequest r = RunPrepare(profile, ShortcutAction::SmartPopup, MakeConnection(), kEnglishText);
        if (!CheckPrepared(r, "Ollama request for retry check prepares")) continue;
        const json::Value body = ParseJson(r.body, "Ollama initial body parses");
        const json::Value retry = ParseJson(r.retryBody, "Ollama retry body parses");
        Check(retry.dump() == WithoutKey(body, "think").dump(),
              "retry body equals the initial body minus think (system/prompt/options preserved)");
        CheckOllamaFields(retry, false, "retry body model");
    }
}

void TestRawTextIsNeverTouched() {
    const std::wstring raw = L"  Hello {{target_language}}\n";
    const PreparedRequest r = RunPrepare(CustomProfile(TranslationBackend::OpenAI, kCustomTemplate),
                                         ShortcutAction::SmartPopup, MakeConnection(), raw);
    if (!CheckPrepared(r, "raw-text custom request prepares")) return;
    Check(r.user == "  Hello {{target_language}}\n", "selected text is sent verbatim, never trimmed or rendered");
    Check(r.system.find("Hello") == std::string::npos, "selected text never enters the system prompt");
}

void TestOverrideReachesPrompt() {
    const execution::ContextPtr context =
        execution::Freeze(CustomProfile(TranslationBackend::OpenAI, kCustomTemplate),
                          ShortcutAction::SmartPopup, MakeConnection());
    const PreparedRequest r = execution::Prepare(*context, kEnglishText, Language::Russian);
    if (!CheckPrepared(r, "retargeted request prepares")) return;
    Check(r.system == "Translate English into Russian politely.", "retarget re-renders the same template");
    Check(r.direction.target == Language::Russian && r.direction.source == Language::English,
          "retarget changes only the target");
}

void TestFreezeIsImmutable() {
    TranslationProfile profile = CustomProfile(TranslationBackend::OpenAI, kCustomTemplate);
    execution::ConnectionSnapshot connection = MakeConnection();
    const execution::ContextPtr context =
        execution::Freeze(profile, ShortcutAction::SmartPopup, connection);
    const PreparedRequest before = execution::Prepare(*context, kEnglishText);
    profile.systemPrompt = "changed";
    profile.model = "changed-model";
    connection.openAIBaseURL = "https://changed.example.test/v1";
    connection.connectionEpoch = 99;
    const PreparedRequest after = execution::Prepare(*context, kEnglishText);
    if (!CheckPrepared(before, "frozen request prepares") || !CheckPrepared(after, "frozen request re-prepares")) return;
    Check(before.body == after.body && before.finalUrl == after.finalUrl &&
              before.cacheIdentity == after.cacheIdentity,
          "later edits to the source values never leak into a frozen context");
}

void TestPreviewNeedsNoConnection() {
    const TranslationProfile profile = CustomProfile(TranslationBackend::OpenAI, kCustomTemplate);
    const execution::ConnectionSnapshot empty;
    const PreparedRequest preview = RunPreview(profile, ShortcutAction::SmartPopup, empty, kEnglishText);
    if (CheckPreviewed(preview, "Preview works without endpoint or API key")) {
        Check(preview.system == kCustomRendered && preview.user == kEnglishUtf8,
              "Preview still renders the system and user messages");
        Check(preview.finalUrl.empty() && preview.cacheIdentity.empty(),
              "Preview carries no URL or cache identity");
    }
    Check(RunPrepare(profile, ShortcutAction::SmartPopup, empty, kEnglishText).status ==
              PrepareStatus::EndpointMissing,
          "Prepare reports the missing OpenAI endpoint");
    execution::ConnectionSnapshot https = MakeConnection();
    https.openAIKey.clear();
    CheckPreviewed(RunPreview(profile, ShortcutAction::SmartPopup, https, kEnglishText),
                   "Preview does not require an API key");
    Check(RunPrepare(profile, ShortcutAction::SmartPopup, https, kEnglishText).status ==
              PrepareStatus::KeyMissing,
          "Prepare (not Preview) reports the missing HTTPS key");
}

void TestOllamaMissingPieces() {
    TranslationProfile profile = MakeProfile(TranslationBackend::Ollama);
    execution::ConnectionSnapshot connection = MakeConnection();
    connection.ollamaHost.clear();
    CheckPreviewed(RunPreview(profile, ShortcutAction::SmartPopup, connection, kEnglishText),
                   "Ollama Preview works without a host");
    Check(RunPrepare(profile, ShortcutAction::SmartPopup, connection, kEnglishText).status ==
              PrepareStatus::EndpointMissing, "Ollama Prepare reports the missing host");
    profile.model.clear();
    CheckPreviewed(RunPreview(profile, ShortcutAction::SmartPopup, MakeConnection(), kEnglishText),
                   "Ollama Preview works without a model name");
    Check(RunPrepare(profile, ShortcutAction::SmartPopup, MakeConnection(), kEnglishText).status ==
              PrepareStatus::ModelMissing, "Ollama Prepare reports the missing model");
    profile = MakeProfile(TranslationBackend::OpenAI);
    profile.model.clear();
    CheckPreviewed(RunPreview(profile, ShortcutAction::SmartPopup, MakeConnection(), kEnglishText),
                   "OpenAI Preview works without a model name");
    Check(RunPrepare(profile, ShortcutAction::SmartPopup, MakeConnection(), kEnglishText).status ==
              PrepareStatus::ModelMissing, "OpenAI Prepare reports the missing model");
}

void TestTemplateAndCapabilityGates() {
    const TranslationProfile blank = CustomProfile(TranslationBackend::OpenAI, "  \n\t");
    Check(RunPreview(blank, ShortcutAction::SmartPopup, MakeConnection(), kEnglishText).status ==
              PrepareStatus::TemplateInvalid, "blank custom prompt is a template error in Preview");
    Check(RunPrepare(blank, ShortcutAction::SmartPopup, MakeConnection(), kEnglishText).status ==
              PrepareStatus::TemplateInvalid, "blank custom prompt is a template error in Prepare");
    const TranslationProfile builtInCustom = CustomProfile(TranslationBackend::BuiltIn, kCustomTemplate);
    TranslationProfile builtInFormal = MakeProfile(TranslationBackend::BuiltIn);
    builtInFormal.presetStyle = RewriteStyle::Formal;
    for (const TranslationProfile* p : {&builtInCustom, &builtInFormal}) {
        Check(RunPreview(*p, ShortcutAction::SmartPopup, MakeConnection(), kEnglishText).status ==
                  PrepareStatus::CapabilityRejected, "BuiltIn Preview rejects unsupported prompt/style");
        Check(RunPrepare(*p, ShortcutAction::SmartPopup, MakeConnection(), kEnglishText).status ==
                  PrepareStatus::CapabilityRejected, "BuiltIn Prepare rejects unsupported prompt/style");
    }
}

void TestUnknownPlaceholderIsNonBlocking() {
    const PreparedRequest r = RunPrepare(CustomProfile(TranslationBackend::OpenAI, "Use {{tone}} tone to {{target_language}}"),
                                         ShortcutAction::SmartPopup, MakeConnection(), kEnglishText);
    if (!CheckPrepared(r, "unknown placeholder does not block Prepare")) return;
    Check(r.system == "Use {{tone}} tone to Chinese", "unknown placeholder stays literal in the system prompt");
    Check(r.warnings.size() == 1 && r.warnings[0] == execution::kWarningUnknownPlaceholder,
          "unknown placeholder surfaces only the fixed warning code");
}

void TestUnknownSourceWording() {
    const PreparedRequest preset = RunPrepare(MakeProfile(TranslationBackend::OpenAI), ShortcutAction::SmartPopup,
                                              MakeConnection(), kUndetectable);
    const PreparedRequest custom = RunPrepare(CustomProfile(TranslationBackend::OpenAI, "From {{source_language}}"),
                                              ShortcutAction::SmartPopup, MakeConnection(), kUndetectable);
    if (CheckPrepared(preset, "undetectable preset prepares"))
        Check(preset.system.find("from the detected language into Chinese.") != std::string::npos,
              "preset OpenAI keeps its legacy 'the detected language' wording");
    if (CheckPrepared(custom, "undetectable custom prepares"))
        Check(custom.system == "From auto-detected language", "custom unknown source is 'auto-detected language'");
}

} // namespace

void RunRequestTests() {
    TestPresetOpenAI();
    TestPresetOllama();
    TestPresetFormalStyle();
    TestCustomOpenAI();
    TestCustomOllama();
    TestThinkRetryBody();
    TestRawTextIsNeverTouched();
    TestOverrideReachesPrompt();
    TestFreezeIsImmutable();
    TestPreviewNeedsNoConnection();
    TestOllamaMissingPieces();
    TestTemplateAndCapabilityGates();
    TestUnknownPlaceholderIsNonBlocking();
    TestUnknownSourceWording();
}

} // namespace execution_test
