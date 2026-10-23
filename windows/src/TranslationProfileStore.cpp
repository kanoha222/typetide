#include "TranslationProfileStore.h"
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace profiles::store {
namespace {

std::string GetString(const json::Object& obj, const char* key, const std::string& def) {
    auto it = obj.find(key);
    return it != obj.end() && it->second.isString() ? it->second.asString() : def;
}

bool ValidateSchemaVersion(const json::Value& val, int& outVersion, ProfileLoadState& outState) {
    if (val.type() != json::Value::Type::Number) {
        outState = ProfileLoadState::Invalid;
        return false;
    }
    const double d = val.asNumber();
    if (!std::isfinite(d)) {
        outState = ProfileLoadState::Invalid;
        return false;
    }
    constexpr double maxInt = static_cast<double>(std::numeric_limits<int>::max());
    if (d <= 0.0 || d > maxInt || std::floor(d) != d) {
        outState = ProfileLoadState::Invalid;
        return false;
    }
    const int version = static_cast<int>(d);
    if (version < 2) {
        outState = ProfileLoadState::Invalid;
        return false;
    }
    if (version > 2) {
        outVersion = version;
        outState = ProfileLoadState::UnsupportedSchema;
        return false;
    }
    outVersion = version;
    outState = ProfileLoadState::Current;
    return true;
}

bool CheckRequiredFields(const json::Object& pObj, std::string& outError) {
    const char* fields[] = {
        "id", "name", "nativeLanguage", "foreignLanguage", "direction",
        "promptMode", "presetStyle", "systemPrompt", "backend", "model"
    };
    for (const char* f : fields) {
        auto itField = pObj.find(f);
        if (itField == pObj.end() || !itField->second.isString()) {
            outError = std::string("Profile field missing or invalid: ") + f;
            return false;
        }
    }
    return true;
}

bool DecodeSingleProfile(const json::Value& item, TranslationProfile& outProfile,
                         std::string& outError) {
    const json::Object* pObj = item.object();
    if (!pObj) {
        outError = "Profile entry must be object";
        return false;
    }
    if (!CheckRequiredFields(*pObj, outError)) {
        return false;
    }

    outProfile.id = pObj->at("id").asString();
    outProfile.name = pObj->at("name").asString();

    auto nativeL = lang::FromCode(pObj->at("nativeLanguage").asString());
    if (!nativeL) { outError = "Invalid nativeLanguage"; return false; }
    outProfile.nativeLanguage = *nativeL;

    auto foreignL = lang::FromCode(pObj->at("foreignLanguage").asString());
    if (!foreignL) { outError = "Invalid foreignLanguage"; return false; }
    outProfile.foreignLanguage = *foreignL;

    auto dir = lang::DirectionFromCode(pObj->at("direction").asString());
    if (!dir) { outError = "Invalid direction"; return false; }
    outProfile.direction = *dir;

    const std::string& pmStr = pObj->at("promptMode").asString();
    if (pmStr == "preset") outProfile.promptMode = PromptMode::Preset;
    else if (pmStr == "custom") outProfile.promptMode = PromptMode::Custom;
    else { outError = "Invalid promptMode"; return false; }

    auto style = lang::StyleFromCode(pObj->at("presetStyle").asString());
    if (!style) { outError = "Invalid presetStyle"; return false; }
    outProfile.presetStyle = *style;

    outProfile.systemPrompt = pObj->at("systemPrompt").asString();

    auto b = BackendFromCode(pObj->at("backend").asString());
    if (!b) { outError = "Invalid backend"; return false; }
    outProfile.backend = *b;

    outProfile.model = pObj->at("model").asString();
    return true;
}

ProfileLoadResult DecodeV2(const json::Object& obj) {
    auto itProfiles = obj.find("profiles");
    if (itProfiles == obj.end() || itProfiles->second.type() != json::Value::Type::Array) {
        return {ProfileLoadState::Invalid, {}, "Missing or invalid profiles array"};
    }
    const auto* arr = itProfiles->second.array();
    if (!arr || arr->empty() || arr->size() > profiles::MaxProfiles) {
        return {ProfileLoadState::Invalid, {}, "Profile collection size out of range (1..64)"};
    }

    auto itS1 = obj.find("shortcut1ProfileId");
    auto itS2 = obj.find("shortcut2ProfileId");
    auto itSel = obj.find("selectionProfileId");
    if (itS1 == obj.end() || !itS1->second.isString() ||
        itS2 == obj.end() || !itS2->second.isString() ||
        itSel == obj.end() || !itSel->second.isString()) {
        return {ProfileLoadState::Invalid, {}, "Missing or invalid profile bindings"};
    }

    std::vector<TranslationProfile> profilesList;
    profilesList.reserve(arr->size());
    for (const auto& item : *arr) {
        TranslationProfile p;
        std::string err;
        if (!DecodeSingleProfile(item, p, err)) {
            return {ProfileLoadState::Invalid, {}, std::move(err)};
        }
        profilesList.push_back(std::move(p));
    }

    ProfileBindings bindings{
        itS1->second.asString(),
        itS2->second.asString(),
        itSel->second.asString()
    };
    ProfileConfiguration config{std::move(profilesList), std::move(bindings)};
    const auto valRes = profiles::Validate(config);
    if (!valRes.ok) {
        return {ProfileLoadState::Invalid, {}, valRes.error};
    }
    return {ProfileLoadState::Current, std::move(config), ""};
}

TranslationProfile MakeLegacyProfile(const json::Object& root, const char* id, const char* name,
                                     const char* dirKey, const char* styleKey,
                                     TranslationBackend backend, const std::string& model) {
    TranslationProfile p;
    p.id = id;
    p.name = name;
    p.nativeLanguage = lang::FromCode(GetString(root, "nativeLanguage", "")).value_or(Language::Chinese);
    p.foreignLanguage = lang::FromCode(GetString(root, "foreignLanguage", "")).value_or(Language::English);
    p.direction = lang::DirectionFromCode(GetString(root, dirKey, "")).value_or(TranslationDirection::Auto);
    p.promptMode = PromptMode::Preset;
    p.presetStyle = lang::StyleFromCode(GetString(root, styleKey, "")).value_or(RewriteStyle::Faithful);
    p.systemPrompt = "";
    p.backend = backend;
    p.model = model;
    return p;
}

ProfileLoadResult DecodeLegacy(const json::Object& root) {
    const std::string backendCode = GetString(root, "backend", "builtin");
    TranslationBackend backend = TranslationBackend::BuiltIn;
    if (backendCode == "openai") backend = TranslationBackend::OpenAI;
    else if (backendCode == "ollama") backend = TranslationBackend::Ollama;

    std::string model;
    if (backend == TranslationBackend::OpenAI) {
        model = GetString(root, "openAIModel", "gpt-5-mini");
    } else if (backend == TranslationBackend::Ollama) {
        model = GetString(root, "ollamaModel", "qwen2.5:3b");
    }

    TranslationProfile readProfile = MakeLegacyProfile(
        root, "legacy-read", "默认翻译", "readDirection", "readStyle", backend, model);
    TranslationProfile rewriteProfile = MakeLegacyProfile(
        root, "legacy-rewrite", "默认改写", "rewriteDirection", "rewriteStyle", backend, model);

    const ShortcutAction readAction =
        ShortcutActionFromCode(GetString(root, "readShortcutAction", "")).value_or(ShortcutAction::SmartPopup);
    const ShortcutAction rewriteAction =
        ShortcutActionFromCode(GetString(root, "rewriteShortcutAction", "")).value_or(ShortcutAction::SmartReplace);

    ProfileBindings bindings{
        IsReplaceAction(readAction) ? "legacy-rewrite" : "legacy-read",
        IsReplaceAction(rewriteAction) ? "legacy-rewrite" : "legacy-read",
        "legacy-read"
    };

    ProfileConfiguration config{{std::move(readProfile), std::move(rewriteProfile)}, std::move(bindings)};
    const auto valRes = profiles::Validate(config);
    if (!valRes.ok) {
        return {ProfileLoadState::Invalid, {}, valRes.error};
    }
    return {ProfileLoadState::MigratedLegacy, std::move(config), ""};
}

} // namespace

ProfileLoadResult Decode(const json::Value& root) {
    const json::Object* obj = root.object();
    if (!obj) {
        return {ProfileLoadState::Invalid, {}, "Settings root must be object"};
    }

    auto itVer = obj->find("schemaVersion");
    if (itVer == obj->end()) {
        return DecodeLegacy(*obj);
    }

    int version = 0;
    ProfileLoadState state = ProfileLoadState::Invalid;
    if (!ValidateSchemaVersion(itVer->second, version, state)) {
        return {state, {}, state == ProfileLoadState::UnsupportedSchema ?
                    "Unsupported future schema version" : "Invalid schema version"};
    }
    return DecodeV2(*obj);
}

json::Object Encode(const ProfileConfiguration& configuration) {
    json::Object root;
    root["schemaVersion"] = 2;
    if (!Validate(configuration).ok) {
        return root;
    }

    json::Array profilesArr;
    profilesArr.reserve(configuration.profiles.size());

    for (const auto& p : configuration.profiles) {
        json::Object pObj;
        pObj["id"] = p.id;
        pObj["name"] = p.name;
        pObj["nativeLanguage"] = lang::Code(p.nativeLanguage);
        pObj["foreignLanguage"] = lang::Code(p.foreignLanguage);
        pObj["direction"] = lang::DirectionCode(p.direction);
        pObj["promptMode"] = p.promptMode == PromptMode::Custom ? "custom" : "preset";
        pObj["presetStyle"] = lang::StyleCode(p.presetStyle);
        pObj["systemPrompt"] = p.systemPrompt;
        pObj["backend"] = BackendCode(p.backend);
        pObj["model"] = p.model;
        profilesArr.push_back(json::Value(std::move(pObj)));
    }

    root["profiles"] = json::Value(std::move(profilesArr));
    root["shortcut1ProfileId"] = configuration.bindings.shortcut1ProfileId;
    root["shortcut2ProfileId"] = configuration.bindings.shortcut2ProfileId;
    root["selectionProfileId"] = configuration.bindings.selectionProfileId;

    return root;
}

} // namespace profiles::store
