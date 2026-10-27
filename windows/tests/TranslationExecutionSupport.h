#pragma once
#include "../src/Json.h"
#include "../src/TranslationExecution.h"
#include <optional>
#include <string>

// Shared fixtures and checks for the standalone TranslationExecutionSelfTest.
// All data is synthetic: fake keys, fake hosts (.test), fake selected text.
namespace execution_test {

bool Check(bool condition, const char* name);
int FailureCount();

inline const char* const kFakeKey = "synthetic-test-key-0001";
inline const char* const kProfileId = "profile-id-9f31c0";
inline const char* const kProfileName = "Display Name 7be2";

/// zh native / en foreign / Auto / Preset Faithful. Model: "" (BuiltIn),
/// "synthetic-ollama-model" or "synthetic-openai-model".
TranslationProfile MakeProfile(TranslationBackend backend);
/// epoch 7, Ollama http://127.0.0.1 + 11434, OpenAI https://api.example.test/v1 + fake key.
execution::ConnectionSnapshot MakeConnection();

execution::PreparedRequest RunPrepare(const TranslationProfile& profile, ShortcutAction action,
                                      const execution::ConnectionSnapshot& connection,
                                      const std::wstring& text,
                                      std::optional<Language> targetOverride = std::nullopt);
execution::PreparedRequest RunPreview(const TranslationProfile& profile, ShortcutAction action,
                                      const execution::ConnectionSnapshot& connection,
                                      const std::wstring& text,
                                      std::optional<Language> targetOverride = std::nullopt);

/// Requires status Ok AND a non-empty identity (and non-empty body for cloud/local
/// providers) so an unimplemented NotReady shell can never compare "equal" by emptiness.
bool CheckPrepared(const execution::PreparedRequest& request, const char* name);
/// Requires status Ok and a non-empty user message.
bool CheckPreviewed(const execution::PreparedRequest& request, const char* name);

/// ASCII-only widening for synthetic URLs (test data is never non-ASCII here).
std::wstring AsciiWiden(const std::string& text);

/// Parses JSON; records a failure and returns Null when it is not valid.
json::Value ParseJson(const std::string& text, const char* name);
/// Copy of a parsed object without one key.
json::Value WithoutKey(const json::Value& object, const std::string& key);

/// Identity of a prepared request, or "" (with a recorded failure) when not Ok.
std::string IdentityOf(const TranslationProfile& profile, ShortcutAction action,
                       const execution::ConnectionSnapshot& connection, const std::wstring& text,
                       std::optional<Language> targetOverride = std::nullopt);
bool CheckDiffers(const std::string& left, const std::string& right, const char* name);
bool CheckSame(const std::string& left, const std::string& right, const char* name);

// Fixed selected-text samples with known Detect() results.
extern const wchar_t* const kEnglishText;   // English
extern const wchar_t* const kChineseText;   // Chinese
extern const wchar_t* const kRussianText;   // Russian (a third language for zh/en profiles)
extern const wchar_t* const kUndetectable;  // digits only -> Detect() == nullopt

// Test groups (one translation unit each).
void RunPresetTests();
void RunRenderTests();
void RunDirectionTests();
void RunRequestTests();
void RunBuiltInTests();
void RunClassifierTests();
void RunPrepareEndpointTests();
void RunCacheTests();
void RunRuntimeContractTests();
void RunSessionTests();
void RunProfileEditorTests();

} // namespace execution_test
