#include "ProfileSelfTest.h"
#include "Json.h"
#ifndef TYPETIDE_JSON_SELF_TEST_ONLY
#include "TranslationProfile.h"
#include "TranslationProfileStore.h"
#endif
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* name) {
    if (!condition) ++failures;
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", name);
}

bool Parses(const std::string& text) {
    bool ok = false;
    json::Parse(text, &ok);
    return ok;
}

void TestStrictJson() {
    std::printf("[profile JSON boundaries]\n");
    const char* malformed[] = {
        "{} trailing", "true false", "01", "-01", "+1", ".5", "1.",
        "1e", "1e+", "0x10", "nan", "NaN", "Infinity", "-Infinity", "1e9999",
        R"("\uD800")", R"("\uDC00")", R"("\uD800A")",
        R"("\uD800\uDBFF")", R"("\u12")", R"("\x41")",
        "[1,]", "{\"a\":1,}", "[", "{", "\"unterminated",
    };
    for (const auto* text : malformed)
        Check(!Parses(text), (std::string("reject ") + text).c_str());
    Check(!Parses(std::string("null\0true", 9)), "reject embedded NUL after value");
    Check(!Parses(std::string("\"line\nbreak\"")), "reject unescaped newline");
    Check(!Parses(std::string("\"tab\there\"")), "reject unescaped tab");
    const std::vector<std::string> malformedUtf8 = {
        std::string("\x80", 1), std::string("\xC0\xAF", 2),
        std::string("\xC2", 1), std::string("\xE0\x80\xAF", 3),
        std::string("\xED\xA0\x80", 3), std::string("\xF0\x80\x80\xAF", 4),
        std::string("\xF4\x90\x80\x80", 4), std::string("\xF5\x80\x80\x80", 4),
    };
    for (const auto& bytes : malformedUtf8)
        Check(!Parses("\"" + bytes + "\""), "reject malformed UTF-8 string");
    const std::string tooDeep(65, '[');
    Check(!Parses(tooDeep + "0" + std::string(65, ']')), "reject nesting beyond 64");
    Check(Parses(std::string(64, '[') + "0" + std::string(64, ']')),
          "accept nesting at depth limit");
    Check(Parses(" \r\n\t {\"n\":-1.25e+2,\"a\":[true,false,null]} \n"),
          "accept valid JSON number/whitespace forms");
}

void TestJsonUnicodeRoundtrip() {
    bool ok = false;
    const auto escaped = json::Parse(R"({"text":"中文 😀\n\t\"\\"})", &ok);
    Check(ok && escaped["text"].asString() == "中文 😀\n\t\"\\",
          "valid raw Unicode and JSON string escapes");
    const std::string slash(1, '\\');
    const auto surrogate = json::Parse("\"" + slash + "uD83D" + slash + "uDE00\"", &ok);
    Check(ok && surrogate.asString() == "😀", "valid escaped UTF-16 surrogate pair");
    json::Object object;
    const std::string original = "  中文 😀\t\"quoted\" \\\ntrailing\n";
    object["text"] = original;
    const auto round = json::Parse(json::Value(std::move(object)).dump(), &ok);
    Check(ok && round["text"].asString() == original,
          "multiline/indentation/emoji roundtrip is byte exact");
    const auto nul = json::Parse("\"a" + slash + "u0000b\"", &ok);
    Check(ok && nul.asString() == std::string("a\0b", 3),
          "escaped U+0000 is preserved inside string");
    const auto nulRoundtrip = json::Parse(nul.dump(), &ok);
    Check(ok && nulRoundtrip.asString() == std::string("a\0b", 3),
          "embedded U+0000 survives dump and parse");
    Check(Parses(R"("\u0000")"), "escaped U+0000 remains legal JSON");
}

#ifndef TYPETIDE_JSON_SELF_TEST_ONLY

void TestTranslationTypeCodes() {
    struct BackendCase { const char* code; TranslationBackend backend; };
    constexpr BackendCase backends[] = {
        {"builtin", TranslationBackend::BuiltIn},
        {"ollama", TranslationBackend::Ollama},
        {"openai", TranslationBackend::OpenAI},
    };
    for (const auto& entry : backends) {
        Check(std::string(BackendCode(entry.backend)) == entry.code,
              "backend encoder matches independent literal");
        const auto parsed = BackendFromCode(entry.code);
        Check(parsed && *parsed == entry.backend, "backend decoder matches independent literal");
    }
    Check(!BackendFromCode("unknown"), "unknown backend code is not silently selected");
    Check(!ShortcutActionFromCode("unknown"), "unknown shortcut action code is rejected");
}

TranslationProfile ExampleProfile() {
    TranslationProfile profile;
    profile.id = "test-profile";
    profile.name = "测试 😀";
    profile.backend = TranslationBackend::Ollama;
    profile.model = "qwen2.5:3b";
    return profile;
}

ProfileConfiguration ExampleConfiguration() {
    const auto first = ExampleProfile();
    auto second = first;
    second.id = "second-profile";
    second.name = "Second profile";
    return {{first, second}, {first.id, second.id, first.id}};
}

void TestProfilePromptModeValidation() {
    std::printf("[profile structural validation]\n");
    const auto original = ExampleProfile();
    Check(profiles::Validate(original).ok, "valid preset profile");
    auto candidate = original;
    candidate.promptMode = PromptMode::Custom;
    candidate.systemPrompt = "  Rewrite\t{{source_language}} into {{target_language}}.\n中文 😀\n";
    Check(profiles::Validate(candidate).ok, "valid multiline custom prompt");
    candidate.systemPrompt = " \t\r\n";
    Check(!profiles::Validate(candidate).ok, "reject whitespace-only custom prompt");
    candidate.systemPrompt = "\xE3\x80\x80\xC2\xA0";
    Check(!profiles::Validate(candidate).ok, "reject Unicode whitespace-only prompt");
    candidate.promptMode = PromptMode::Preset;
    Check(profiles::Validate(candidate).ok, "inactive custom prompt may be blank");
    candidate.promptMode = PromptMode::Custom;
    candidate.systemPrompt = "Leave {{unknown_variable}} literal.";
    const auto warning = profiles::Validate(candidate);
    Check(warning.ok && !warning.warnings.empty(), "unknown variable is nonblocking warning");
    candidate.systemPrompt = "Leave {{secret_synthetic_token_123}} literal.";
    const auto privacyWarning = profiles::Validate(candidate);
    Check(privacyWarning.ok && !privacyWarning.warnings.empty(), "unknown variable warning emitted");
    bool leakedToken = false;
    for (const auto& w : privacyWarning.warnings) {
        if (w.find("secret_synthetic_token_123") != std::string::npos) leakedToken = true;
    }
    Check(!leakedToken, "custom prompt warning does not leak user template variable");
    Check(candidate.systemPrompt == "Leave {{secret_synthetic_token_123}} literal.",
          "validation preserves prompt and placeholders verbatim");

}

void TestProfileNameValidation() {
    const auto original = ExampleProfile();
    auto candidate = original;
    candidate.name = std::string(profiles::MaxNameUtf16, 'n');
    Check(profiles::Validate(candidate).ok, "name exact ASCII UTF-16 limit");
    candidate.name += 'n';
    Check(!profiles::Validate(candidate).ok, "name above UTF-16 limit");
    candidate.name.clear();
    for (int i = 0; i < 40; ++i) candidate.name += "😀";
    Check(profiles::Validate(candidate).ok, "40 emoji use 80 UTF-16 units");
    candidate.name += "😀";
    Check(!profiles::Validate(candidate).ok, "41 emoji exceed name UTF-16 limit");
    candidate = original;
    candidate.name = "bad\nname";
    Check(!profiles::Validate(candidate).ok, "name control characters rejected");
    candidate.name.clear();
    Check(!profiles::Validate(candidate).ok, "empty name rejected");
    candidate.name = "   ";
    Check(!profiles::Validate(candidate).ok, "spaces-only name rejected");
    candidate.name = std::string("bad\xC0\xAF", 5);
    Check(!profiles::Validate(candidate).ok, "invalid UTF-8 name rejected");

}

void TestProfilePromptLimitValidation() {
    const auto original = ExampleProfile();
    auto candidate = original;
    candidate.promptMode = PromptMode::Custom;
    candidate.systemPrompt = std::string(profiles::MaxPromptUtf16, 'p');
    Check(profiles::Validate(candidate).ok, "prompt exact UTF-16 limit");
    candidate.systemPrompt += 'p';
    Check(!profiles::Validate(candidate).ok, "oversized prompt not truncated");
    candidate.systemPrompt.clear();
    for (std::size_t i = 0; i < profiles::MaxPromptUtf16 / 2; ++i)
        candidate.systemPrompt += "😀";
    Check(profiles::Validate(candidate).ok, "emoji prompt counted as UTF-16 units");
    candidate.systemPrompt += 'x';
    Check(!profiles::Validate(candidate).ok, "emoji prompt over boundary rejected");
    candidate = original;
    candidate.systemPrompt = std::string("\xED\xA0\x80", 3);
    Check(!profiles::Validate(candidate).ok, "inactive prompt must still be valid UTF-8");

}

void TestProfileIdentifierValidation() {
    const auto original = ExampleProfile();
    auto candidate = original;
    candidate.id.clear();
    Check(!profiles::Validate(candidate).ok, "empty ID rejected");
    candidate.id = std::string(profiles::MaxIdBytes + 1, 'i');
    Check(!profiles::Validate(candidate).ok, "oversized ID rejected");
    candidate.id.clear();
    for (std::size_t i = 0; i < profiles::MaxIdBytes / 2; ++i) candidate.id += "é";
    Check(profiles::Validate(candidate).ok, "multibyte ID exact UTF-8 byte limit");
    candidate.id += 'x';
    Check(!profiles::Validate(candidate).ok, "multibyte ID above byte limit rejected");
    candidate.id = "bad\nid";
    Check(!profiles::Validate(candidate).ok, "ID control characters rejected");
    candidate.id = std::string("\xF4\x90\x80\x80", 4);
    Check(!profiles::Validate(candidate).ok, "invalid UTF-8 ID rejected");
    candidate = original;
    candidate.model = std::string(profiles::MaxModelBytes, 'm');
    Check(profiles::Validate(candidate).ok, "model exact byte limit");
    candidate.model += 'm';
    Check(!profiles::Validate(candidate).ok, "oversized model rejected");
    candidate.model.clear();
    for (std::size_t i = 0; i < profiles::MaxModelBytes / 2; ++i) candidate.model += "é";
    Check(profiles::Validate(candidate).ok, "multibyte model exact UTF-8 byte limit");
    candidate.model += 'x';
    Check(!profiles::Validate(candidate).ok, "multibyte model above byte limit rejected");
    candidate.model = " \t";
    Check(!profiles::Validate(candidate).ok, "blank remote model rejected");
    candidate.model = "bad\nmodel";
    Check(!profiles::Validate(candidate).ok, "model control newline rejected");
    candidate.model = "bad\tmodel";
    Check(!profiles::Validate(candidate).ok, "model tab rejected");
    candidate.model = std::string("bad\0model", 9);
    Check(!profiles::Validate(candidate).ok, "model embedded NUL rejected");
    candidate.model = std::string("bad\xC2\x85model", 10);
    Check(!profiles::Validate(candidate).ok, "model C1 control character rejected");
    candidate.model = std::string("\xC2", 1);
    Check(!profiles::Validate(candidate).ok, "invalid UTF-8 model rejected");

}

void TestProfileEnumValidation() {
    const auto original = ExampleProfile();
    auto candidate = original;
    candidate.nativeLanguage = static_cast<Language>(999);
    Check(!profiles::Validate(candidate).ok, "invalid native language enum rejected");
    candidate = original;
    candidate.foreignLanguage = static_cast<Language>(999);
    Check(!profiles::Validate(candidate).ok, "invalid foreign language enum rejected");
    candidate = original;
    candidate.direction = static_cast<TranslationDirection>(999);
    Check(!profiles::Validate(candidate).ok, "invalid direction enum rejected");
    candidate = original;
    candidate.presetStyle = static_cast<RewriteStyle>(999);
    Check(!profiles::Validate(candidate).ok, "invalid style enum rejected");
    candidate = original;
    candidate.promptMode = static_cast<PromptMode>(999);
    Check(!profiles::Validate(candidate).ok, "invalid prompt mode enum rejected");
    candidate = original;
    candidate.backend = static_cast<TranslationBackend>(999);
    Check(!profiles::Validate(candidate).ok, "invalid backend enum rejected");
    Check(original == ExampleProfile(), "validation did not mutate original value");
}

void TestProfileCapabilities() {
    std::printf("[profile backend capabilities]\n");
    auto candidate = ExampleProfile();
    candidate.backend = TranslationBackend::BuiltIn;
    candidate.model.clear();
    Check(profiles::Validate(candidate).ok && profiles::ValidateCapability(candidate).ok,
          "BuiltIn faithful preset remains supported");
    candidate.presetStyle = RewriteStyle::Formal;
    Check(profiles::Validate(candidate).ok && !profiles::ValidateCapability(candidate).ok,
          "unsupported BuiltIn style is preserved but not executable");
    candidate.presetStyle = RewriteStyle::Faithful;
    candidate.promptMode = PromptMode::Custom;
    candidate.systemPrompt = "A real custom prompt.";
    Check(profiles::Validate(candidate).ok && !profiles::ValidateCapability(candidate).ok,
          "BuiltIn custom is structurally valid but unavailable");
    candidate.backend = TranslationBackend::Ollama;
    candidate.model = "qwen2.5:3b";
    Check(profiles::ValidateCapability(candidate).ok, "Ollama custom supported");
    candidate.backend = TranslationBackend::OpenAI;
    Check(profiles::ValidateCapability(candidate).ok, "OpenAI custom supported");
    candidate.backend = static_cast<TranslationBackend>(999);
    Check(!profiles::ValidateCapability(candidate).ok, "invalid backend fails capability check");
    candidate = ExampleProfile();
    candidate.name.clear();
    Check(!profiles::ValidateCapability(candidate).ok, "structurally invalid profile fails capability check");
}

void TestConfigurationValidation() {
    std::printf("[profile bindings]\n");
    const auto original = ExampleConfiguration();
    Check(profiles::Validate(original).ok, "valid profile collection and bindings");
    Check(profiles::Find(original, original.profiles[0].id) == &original.profiles[0],
          "stable-ID lookup");
    Check(!profiles::Find(original, "missing"), "missing profile lookup stays missing");
    Check(profiles::BoundProfile(original, ProfileEntry::Shortcut1) == &original.profiles[0] &&
          profiles::BoundProfile(original, ProfileEntry::Shortcut2) == &original.profiles[1] &&
          profiles::BoundProfile(original, ProfileEntry::Selection) == &original.profiles[0],
          "each physical entry resolves its own binding");
    Check(!profiles::BoundProfile(original, static_cast<ProfileEntry>(999)),
          "invalid entry does not silently choose a backend");
    auto candidate = original;
    candidate.profiles.clear();
    Check(!profiles::Validate(candidate).ok, "cannot delete final profile");
    candidate = original;
    candidate.profiles[1].id = candidate.profiles[0].id;
    const auto dupRes = profiles::Validate(candidate);
    Check(!dupRes.ok, "duplicate stable IDs rejected");
    auto candidatePrivacy = original;
    candidatePrivacy.profiles[1].id = "secret-synthetic-id-999";
    candidatePrivacy.profiles[0].id = "secret-synthetic-id-999";
    const auto dupPrivacyRes = profiles::Validate(candidatePrivacy);
    Check(!dupPrivacyRes.ok, "duplicate custom profile IDs rejected");
    Check(dupPrivacyRes.error.find("secret-synthetic-id-999") == std::string::npos,
          "duplicate ID error does not leak user profile ID value");
    candidate = original;
    candidate.bindings.shortcut1ProfileId = "missing";
    Check(!profiles::Validate(candidate).ok, "shortcut1 dangling binding rejected");
    candidate = original;
    candidate.bindings.shortcut2ProfileId = "missing";
    Check(!profiles::Validate(candidate).ok, "shortcut2 dangling binding rejected");
    candidate = original;
    candidate.bindings.selectionProfileId = "missing";
    Check(!profiles::Validate(candidate).ok, "selection dangling binding rejected");
    Check(!profiles::BoundProfile(candidate, ProfileEntry::Selection),
          "dangling selection never falls back silently");
    candidate = original;
    for (std::size_t i = candidate.profiles.size(); i < profiles::MaxProfiles; ++i) {
        auto added = ExampleProfile();
        added.id = "id-" + std::to_string(i);
        candidate.profiles.push_back(added);
    }
    Check(profiles::Validate(candidate).ok, "64 profiles accepted");
    auto excess = ExampleProfile();
    excess.id = "excess";
    candidate.profiles.push_back(excess);
    Check(!profiles::Validate(candidate).ok, "65 profiles rejected");
    Check(original == ExampleConfiguration(), "collection validation is immutable");
    const auto defaults = profiles::Defaults();
    Check(defaults.profiles.size() == 2 && profiles::Validate(defaults).ok,
          "new-install defaults have two valid modes and bindings");
}

json::Object LegacyRoot(const char* first, const char* second) {
    json::Object root;
    root["backend"] = "ollama";
    root["ollamaModel"] = "saved-not-installed:model";
    root["nativeLanguage"] = "fr";
    root["foreignLanguage"] = "ru";
    root["readDirection"] = "foreignToNative";
    root["rewriteDirection"] = "nativeToForeign";
    root["readStyle"] = "formal";
    root["rewriteStyle"] = "polished";
    root["readShortcutAction"] = first;
    root["rewriteShortcutAction"] = second;
    root["openAIKey"] = "fixed-synthetic-secret";
    return root;
}

void CheckLegacyPair(const char* firstCode, const char* secondCode,
                     const char* firstExpectedId, const char* secondExpectedId) {
    const auto migrated = profiles::store::Decode(json::Value(LegacyRoot(firstCode, secondCode)));
    Check(migrated.state == ProfileLoadState::MigratedLegacy &&
          profiles::Validate(migrated.configuration).ok,
          "legacy action pair migrates to valid collection");
    Check(migrated.configuration.profiles.size() == 2, "legacy migration creates exactly two profiles");
    if (migrated.configuration.profiles.size() != 2) return;
    const auto& read = migrated.configuration.profiles[0];
    const auto& rewrite = migrated.configuration.profiles[1];
    const auto& bindings = migrated.configuration.bindings;
    Check(read.id == "legacy-read" && rewrite.id == "legacy-rewrite",
          "migration IDs are deterministic and independent");
    Check(bindings.shortcut1ProfileId == firstExpectedId &&
          bindings.shortcut2ProfileId == secondExpectedId,
          "physical slot binding follows literal old action not slot name");
    Check(bindings.selectionProfileId == "legacy-read", "selection binds literal legacy-read");
    Check(read.nativeLanguage == Language::French && read.foreignLanguage == Language::Russian &&
          rewrite.nativeLanguage == Language::French && rewrite.foreignLanguage == Language::Russian &&
          read.direction == TranslationDirection::ForeignToNative &&
          rewrite.direction == TranslationDirection::NativeToForeign &&
          read.presetStyle == RewriteStyle::Formal && rewrite.presetStyle == RewriteStyle::Polished,
          "legacy independent read/write languages direction and style preserved");
    Check(read.model == "saved-not-installed:model" && rewrite.model == read.model,
          "missing configured model preserved, never auto-replaced");
    Check(json::Value(profiles::store::Encode(migrated.configuration)).dump().find(
              "fixed-synthetic-secret") == std::string::npos,
          "profiles never contain legacy API credential");
}

void TestLegacyMigration() {
    std::printf("[legacy migration]\n");
    struct ActionCase {
        const char* code;
        const char* expectedProfileId;
        ShortcutAction action;
        bool replace;
    };
    constexpr ActionCase actions[] = {
        {"smartPopup", "legacy-read", ShortcutAction::SmartPopup, false},
        {"nativePopup", "legacy-read", ShortcutAction::NativePopup, false},
        {"foreignPopup", "legacy-read", ShortcutAction::ForeignPopup, false},
        {"smartReplace", "legacy-rewrite", ShortcutAction::SmartReplace, true},
        {"nativeReplace", "legacy-rewrite", ShortcutAction::NativeReplace, true},
        {"foreignReplace", "legacy-rewrite", ShortcutAction::ForeignReplace, true},
    };
    for (const auto& entry : actions) {
        Check(std::string(ShortcutActionCode(entry.action)) == entry.code,
              "action encoder matches independent literal");
        const auto parsed = ShortcutActionFromCode(entry.code);
        Check(parsed && *parsed == entry.action, "action decoder matches independent literal");
        Check(IsReplaceAction(entry.action) == entry.replace,
              "replace-action helper matches independent literal");
    }
    for (const auto& first : actions) {
        for (const auto& second : actions) {
            CheckLegacyPair(first.code, second.code, first.expectedProfileId, second.expectedProfileId);
        }
    }
}

void TestLegacyBackends() {
    auto legacy = LegacyRoot("smartReplace", "nativePopup");
    legacy["backend"] = "builtin";
    const auto builtin = profiles::store::Decode(json::Value(legacy));
    Check(builtin.state == ProfileLoadState::MigratedLegacy &&
          builtin.configuration.profiles.size() == 2 &&
          builtin.configuration.profiles[0].presetStyle == RewriteStyle::Formal &&
          !profiles::ValidateCapability(builtin.configuration.profiles[0]).ok,
          "migration preserves incompatible BuiltIn style without silent correction");
    legacy["backend"] = "openai";
    legacy["openAIModel"] = "explicit-api-model";
    const auto api = profiles::store::Decode(json::Value(legacy));
    Check(api.state == ProfileLoadState::MigratedLegacy && api.configuration.profiles.size() == 2 &&
          api.configuration.profiles[0].backend == TranslationBackend::OpenAI &&
          api.configuration.profiles[0].model == "explicit-api-model",
          "API backend and explicit model migrated");
    const auto defaults = profiles::store::Decode(json::Value(json::Object{}));
    Check(defaults.state == ProfileLoadState::MigratedLegacy &&
          profiles::Validate(defaults.configuration).ok,
          "empty legacy object uses valid defaults");
}

json::Object WithProfileField(const ProfileConfiguration& configuration,
                             const char* key, json::Value value) {
    auto root = profiles::store::Encode(configuration);
    const auto found = root.find("profiles");
    const auto* array = found == root.end() ? nullptr : found->second.array();
    Check(array && array->size() == configuration.profiles.size() && !array->empty(),
          "encoder provides expected nonempty profile array");
    if (!array || array->empty()) return root;
    const auto* object = array->front().object();
    Check(object != nullptr, "encoder provides object profile entries");
    if (!object) return root;
    auto items = *array;
    auto first = *object;
    first[key] = std::move(value);
    items[0] = json::Value(std::move(first));
    root["profiles"] = json::Value(std::move(items));
    return root;
}

ProfileConfiguration CodecConfiguration() {
    auto configuration = ExampleConfiguration();
    configuration.profiles[0].nativeLanguage = Language::French;
    configuration.profiles[0].foreignLanguage = Language::Russian;
    configuration.profiles[0].direction = TranslationDirection::ForeignToNative;
    configuration.profiles[0].presetStyle = RewriteStyle::Formal;
    configuration.profiles[1].nativeLanguage = Language::Hindi;
    configuration.profiles[1].foreignLanguage = Language::Arabic;
    configuration.profiles[1].direction = TranslationDirection::NativeToForeign;
    configuration.profiles[1].presetStyle = RewriteStyle::Polished;
    configuration.profiles[1].backend = TranslationBackend::OpenAI;
    configuration.profiles[1].model = "different-api-model";
    configuration.profiles[0].promptMode = PromptMode::Custom;
    configuration.profiles[0].systemPrompt = "  Exact\t{{target_language}}\n中文 😀 \"quotes\" \\\n";
    configuration.profiles[1].systemPrompt = "Keep inactive editor contents.\n";
    return configuration;
}

void TestSchemaRoundtrip() {
    std::printf("[schema v2 codec]\n");
    const auto configuration = CodecConfiguration();
    const auto encoded = json::Value(profiles::store::Encode(configuration));
    bool ok = false;
    const auto decoded = profiles::store::Decode(json::Parse(encoded.dump(), &ok));
    Check(ok && decoded.state == ProfileLoadState::Current && decoded.configuration == configuration,
          "schema v2 roundtrip preserves all bytes and inactive prompt");
}

void TestSchemaVersions() {
    const auto configuration = CodecConfiguration();
    const std::vector<json::Value> invalidVersions = {
        json::Value(), json::Value("2"), json::Value(true), json::Value(2.5),
        json::Value(-1), json::Value(0), json::Value(1e30),
        json::Value(std::numeric_limits<double>::infinity()),
    };
    for (const auto& version : invalidVersions) {
        auto candidate = profiles::store::Encode(configuration);
        candidate["schemaVersion"] = version;
        Check(profiles::store::Decode(json::Value(candidate)).state == ProfileLoadState::Invalid,
              "schema type/integer/range checked before conversion");
    }
    auto future = profiles::store::Encode(configuration);
    future["schemaVersion"] = 3;
    Check(profiles::store::Decode(json::Value(future)).state == ProfileLoadState::UnsupportedSchema,
          "future schema explicitly distinguished from corruption");
    auto maxFuture = profiles::store::Encode(configuration);
    maxFuture["schemaVersion"] = static_cast<double>(std::numeric_limits<int>::max());
    Check(profiles::store::Decode(json::Value(maxFuture)).state == ProfileLoadState::UnsupportedSchema,
          "max int schema version classified as unsupported schema");
    Check(profiles::store::Decode(json::Value(json::Array{})).state == ProfileLoadState::Invalid,
          "settings root must be object");
}

void TestDirectDuplicateDecoding(const ProfileConfiguration& configuration) {
    auto directDup = profiles::store::Encode(configuration);
    const auto itProfiles = directDup.find("profiles");
    const auto* arr = itProfiles == directDup.end() ? nullptr : itProfiles->second.array();
    Check(arr && arr->size() >= 2, "encoder provides multi-profile array for direct dup test");
    if (!arr || arr->size() < 2) return;
    auto items = *arr;
    const auto* secondObj = items[1].object();
    const auto* firstObj = items[0].object();
    Check(firstObj && secondObj, "profile items are JSON objects for direct dup test");
    if (!firstObj || !secondObj) return;
    auto modifiedSecond = *secondObj;
    const std::string firstId = (*firstObj).at("id").asString();
    modifiedSecond["id"] = firstId;
    items[1] = json::Value(std::move(modifiedSecond));
    directDup["profiles"] = json::Value(std::move(items));
    directDup["shortcut1ProfileId"] = firstId;
    directDup["shortcut2ProfileId"] = firstId;
    directDup["selectionProfileId"] = firstId;
    Check(profiles::store::Decode(json::Value(directDup)).state == ProfileLoadState::Invalid,
          "direct serialized duplicate IDs rejected while decoding");
}

void TestSchemaFields() {
    const auto configuration = CodecConfiguration();
    const char* fields[] = {
        "id", "name", "nativeLanguage", "foreignLanguage", "direction", "promptMode",
        "presetStyle", "systemPrompt", "backend", "model",
    };
    for (const auto* field : fields) {
        Check(profiles::store::Decode(json::Value(WithProfileField(configuration, field, 42))).state ==
                  ProfileLoadState::Invalid,
              "profile field wrong type rejected");
    }
    const char* enumFields[] = {
        "nativeLanguage", "foreignLanguage", "direction", "promptMode", "presetStyle", "backend",
    };
    for (const auto* field : enumFields) {
        Check(profiles::store::Decode(json::Value(WithProfileField(configuration, field, "unknown"))).state ==
                  ProfileLoadState::Invalid,
              "unknown serialized profile enum rejected");
    }
    auto dangling = profiles::store::Encode(configuration);
    dangling["selectionProfileId"] = "missing";
    Check(profiles::store::Decode(json::Value(dangling)).state == ProfileLoadState::Invalid,
          "serialized invalid reference rejected");
    auto missingProfiles = profiles::store::Encode(configuration);
    missingProfiles.erase("profiles");
    Check(profiles::store::Decode(json::Value(missingProfiles)).state == ProfileLoadState::Invalid,
          "v2 never falls back to legacy when profiles missing");
    auto duplicate = configuration;
    duplicate.profiles[1].id = duplicate.profiles[0].id;
    Check(profiles::store::Decode(json::Value(profiles::store::Encode(duplicate))).state ==
              ProfileLoadState::Invalid,
          "duplicate IDs rejected while decoding");
    ProfileConfiguration emptyConfig;
    Check(profiles::store::Decode(json::Value(profiles::store::Encode(emptyConfig))).state ==
              ProfileLoadState::Invalid,
          "encoded empty configuration decodes as invalid without legacy fallback");
    auto badBinding = configuration;
    badBinding.bindings.shortcut1ProfileId = "missing";
    Check(profiles::store::Decode(json::Value(profiles::store::Encode(badBinding))).state ==
              ProfileLoadState::Invalid,
          "encoded invalid binding decodes as invalid without legacy fallback");
    TestDirectDuplicateDecoding(configuration);
}

#endif
} // namespace

int RunProfileSelfTests() {
    failures = 0;
    TestStrictJson();
    TestJsonUnicodeRoundtrip();
#ifndef TYPETIDE_JSON_SELF_TEST_ONLY
    TestTranslationTypeCodes();
    TestProfilePromptModeValidation();
    TestProfileNameValidation();
    TestProfilePromptLimitValidation();
    TestProfileIdentifierValidation();
    TestProfileEnumValidation();
    TestProfileCapabilities();
    TestConfigurationValidation();
    TestLegacyMigration();
    TestLegacyBackends();
    TestSchemaRoundtrip();
    TestSchemaVersions();
    TestSchemaFields();
#endif
    std::printf("[profile core] %d failure(s)\n", failures);
    return failures;
}

#ifdef TYPETIDE_PROFILE_CORE_TEST_MAIN
int main() { return RunProfileSelfTests(); }
#endif
