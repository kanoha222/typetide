#include "TranslationExecutionSupport.h"
#include <cstdio>
#include <utility>

namespace execution_test {

namespace {
int g_failures = 0;
}

bool Check(bool condition, const char* name) {
    if (!condition) {
        ++g_failures;
        std::printf("[execution] FAIL: %s\n", name);
    }
    return condition;
}

int FailureCount() { return g_failures; }

const wchar_t* const kEnglishText = L"The cat is in the house and it is that";
const wchar_t* const kChineseText = L"你好世界";
const wchar_t* const kRussianText = L"Привет мир";
const wchar_t* const kUndetectable = L"12345 !!!";

TranslationProfile MakeProfile(TranslationBackend backend) {
    TranslationProfile profile;
    profile.id = kProfileId;
    profile.name = kProfileName;
    profile.nativeLanguage = Language::Chinese;
    profile.foreignLanguage = Language::English;
    profile.direction = TranslationDirection::Auto;
    profile.promptMode = PromptMode::Preset;
    profile.presetStyle = RewriteStyle::Faithful;
    profile.backend = backend;
    switch (backend) {
    case TranslationBackend::BuiltIn: profile.model.clear(); break;
    case TranslationBackend::Ollama: profile.model = "synthetic-ollama-model"; break;
    case TranslationBackend::OpenAI: profile.model = "synthetic-openai-model"; break;
    }
    return profile;
}

execution::ConnectionSnapshot MakeConnection() {
    execution::ConnectionSnapshot connection;
    connection.connectionEpoch = 7;
    connection.ollamaHost = "http://127.0.0.1";
    connection.ollamaPort = 11434;
    connection.openAIBaseURL = "https://api.example.test/v1";
    connection.openAIKey = kFakeKey;
    return connection;
}

execution::PreparedRequest RunPrepare(const TranslationProfile& profile, ShortcutAction action,
                                      const execution::ConnectionSnapshot& connection,
                                      const std::wstring& text,
                                      std::optional<Language> targetOverride) {
    execution::ContextPtr context = execution::Freeze(profile, action, connection);
    return execution::Prepare(*context, text, targetOverride);
}

execution::PreparedRequest RunPreview(const TranslationProfile& profile, ShortcutAction action,
                                      const execution::ConnectionSnapshot& connection,
                                      const std::wstring& text,
                                      std::optional<Language> targetOverride) {
    execution::ContextPtr context = execution::Freeze(profile, action, connection);
    return execution::Preview(*context, text, targetOverride);
}

bool CheckPrepared(const execution::PreparedRequest& request, const char* name) {
    if (!Check(request.ok(), name)) return false;
    bool good = Check(!request.cacheIdentity.empty(), "prepared request has a cache identity");
    if (request.backend != TranslationBackend::BuiltIn)
        good = Check(!request.body.empty(), "provider request has a body") && good;
    return good;
}

bool CheckPreviewed(const execution::PreparedRequest& request, const char* name) {
    if (!Check(request.ok(), name)) return false;
    return Check(!request.user.empty(), "preview has a user message");
}

std::wstring AsciiWiden(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

json::Value ParseJson(const std::string& text, const char* name) {
    bool ok = false;
    json::Value value = json::Parse(text, &ok);
    if (!Check(ok && value.isObject(), name)) return json::Value();
    return value;
}

json::Value WithoutKey(const json::Value& object, const std::string& key) {
    json::Object copy;
    if (const json::Object* source = object.object()) copy = *source;
    copy.erase(key);
    return json::Value(std::move(copy));
}

std::string IdentityOf(const TranslationProfile& profile, ShortcutAction action,
                       const execution::ConnectionSnapshot& connection, const std::wstring& text,
                       std::optional<Language> targetOverride) {
    const execution::PreparedRequest request =
        RunPrepare(profile, action, connection, text, targetOverride);
    return CheckPrepared(request, "identity source request prepares") ? request.cacheIdentity
                                                                       : std::string();
}

bool CheckDiffers(const std::string& left, const std::string& right, const char* name) {
    if (!Check(!left.empty() && !right.empty(), "identity comparison inputs are non-empty"))
        return false;
    return Check(left != right, name);
}

bool CheckSame(const std::string& left, const std::string& right, const char* name) {
    if (!Check(!left.empty() && !right.empty(), "identity comparison inputs are non-empty"))
        return false;
    return Check(left == right, name);
}

} // namespace execution_test
