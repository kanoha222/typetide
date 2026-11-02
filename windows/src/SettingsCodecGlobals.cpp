#include "SettingsCodec.h"
#include "Util.h"
#include <climits>
#include <cmath>
#include <optional>
#include <utility>

namespace settings_codec::detail {
namespace {
constexpr size_t kMaxEndpointBytes = 2048;
constexpr size_t kMaxApps = 100;
constexpr size_t kMaxAppUtf16 = 260;
constexpr long long kMaxExactTimestamp = (1LL << 53) - 1;
constexpr UINT kAllowedModifiers = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;

struct StringField {
    const char* key;
    std::string SettingsData::*member;
    size_t limit;
};
constexpr StringField kStrings[] = {
    {"ollamaHost", &SettingsData::ollamaHost, kMaxEndpointBytes},
    {"ollamaModel", &SettingsData::ollamaModel, profiles::MaxModelBytes},
    {"openAIBaseURL", &SettingsData::openAIBaseURL, kMaxEndpointBytes},
    {"openAIModel", &SettingsData::openAIModel, profiles::MaxModelBytes},
};

bool Has(const json::Value& root, const char* key) { return root.object()->contains(key); }

bool ValidUtf8(const std::string& value, size_t limit) {
    if (value.size() > limit || value.size() > INT_MAX || value.find('\0') != std::string::npos) return false;
    return value.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                               static_cast<int>(value.size()), nullptr, 0) > 0;
}

bool ValidApp(const std::wstring& value) {
    return !value.empty() && value.size() <= kMaxAppUtf16 && value.find(L'\0') == std::wstring::npos &&
           WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                               nullptr, 0, nullptr, nullptr) > 0;
}

bool ReadStrings(const json::Value& root, SettingsData& data, bool strict) {
    for (const auto& field : kStrings) {
        if (!Has(root, field.key)) continue;
        const auto& value = root[field.key];
        if (!value.isString() || !ValidUtf8(value.asString(), field.limit)) {
            if (strict) return false;
            continue; // Legacy malformed values keep the safe default.
        }
        data.*field.member = value.asString();
    }
    return true;
}

bool ReadBool(const json::Value& root, const char* key, bool& output, bool strict) {
    if (!Has(root, key)) return true;
    if (root[key].type() != json::Value::Type::Bool) return !strict;
    output = root[key].asBool();
    return true;
}

template <typename Enum, typename Parser>
bool ReadEnum(const json::Value& root, const char* key, Enum& output, Parser parse, bool strict) {
    if (!Has(root, key)) return true;
    const auto value = root[key].isString() ? parse(root[key].asString()) : std::optional<Enum>{};
    if (!value) return !strict;
    output = *value;
    return true;
}

std::optional<SelectionTrigger> ParseTrigger(const std::string& value) {
    if (value == "none") return SelectionTrigger::None;
    if (value == "icon") return SelectionTrigger::Icon;
    if (value == "auto") return SelectionTrigger::Auto;
    return std::nullopt;
}

bool ReadEnums(const json::Value& root, SettingsData& data, bool strict) {
    return ReadEnum(root, "backend", data.backend, BackendFromCode, strict) &&
           ReadEnum(root, "nativeLanguage", data.nativeLanguage, lang::FromCode, strict) &&
           ReadEnum(root, "foreignLanguage", data.foreignLanguage, lang::FromCode, strict) &&
           ReadEnum(root, "readDirection", data.readDirection, lang::DirectionFromCode, strict) &&
           ReadEnum(root, "rewriteDirection", data.rewriteDirection, lang::DirectionFromCode, strict) &&
           ReadEnum(root, "readStyle", data.readStyle, lang::StyleFromCode, strict) &&
           ReadEnum(root, "rewriteStyle", data.rewriteStyle, lang::StyleFromCode, strict) &&
           ReadEnum(root, "readShortcutAction", data.readShortcutAction, ShortcutActionFromCode, strict) &&
           ReadEnum(root, "rewriteShortcutAction", data.rewriteShortcutAction, ShortcutActionFromCode, strict) &&
           ReadEnum(root, "selectionTrigger", data.selectionTrigger, ParseTrigger, strict);
}

bool WholeNumber(const json::Value& value, double minimum, double maximum) {
    if (value.type() != json::Value::Type::Number) return false;
    const double number = value.asNumber();
    return std::isfinite(number) && std::floor(number) == number && number >= minimum && number <= maximum;
}

bool ReadNumbers(const json::Value& root, SettingsData& data, bool strict) {
    if (Has(root, "ollamaPort")) {
        if (WholeNumber(root["ollamaPort"], 1, 65535)) data.ollamaPort = static_cast<int>(root["ollamaPort"].asNumber());
        else if (strict) return false;
    }
    if (Has(root, "lastUpdateCheck")) {
        if (WholeNumber(root["lastUpdateCheck"], 0, static_cast<double>(kMaxExactTimestamp))) {
            data.lastUpdateCheck = static_cast<long long>(root["lastUpdateCheck"].asNumber());
        } else if (strict) return false;
    }
    return true;
}

bool ValidHotkey(const Hotkey& key) {
    return (key.modifiers & ~kAllowedModifiers) == 0 && key.vk < 256;
}

bool ReadHotkey(const json::Value& root, const char* key, Hotkey& output, bool strict) {
    if (!Has(root, key)) return true;
    const auto& object = root[key];
    if (!object.isObject()) return !strict;
    Hotkey candidate = output;
    if (Has(object, "modifiers")) {
        if (!WholeNumber(object["modifiers"], 0, kAllowedModifiers)) return !strict;
        candidate.modifiers = static_cast<UINT>(object["modifiers"].asNumber());
    }
    if (Has(object, "vk")) {
        if (!WholeNumber(object["vk"], 0, 255)) return !strict;
        candidate.vk = static_cast<UINT>(object["vk"].asNumber());
    }
    if (!ValidHotkey(candidate)) return !strict;
    output = candidate;
    return true;
}

bool ReadApps(const json::Value& root, SettingsData& data, bool strict) {
    if (!Has(root, "skipApps")) return true;
    const auto* list = root["skipApps"].array();
    if (!list || (strict && list->size() > kMaxApps)) return !strict;
    std::vector<std::wstring> apps;
    for (const auto& value : *list) {
        if (!value.isString() || !ValidUtf8(value.asString(), kMaxAppUtf16 * 4)) {
            if (strict) return false;
            continue;
        }
        auto app = util::Widen(value.asString());
        if (!strict) app = util::Trim(app);
        if (!ValidApp(app)) {
            if (strict) return false;
            continue;
        }
        bool duplicate = false;
        if (!strict) {
            for (const auto& existing : apps) duplicate |= util::ToLower(existing) == util::ToLower(app);
        }
        if (!duplicate && apps.size() < kMaxApps) apps.push_back(std::move(app));
    }
    data.skipApps = std::move(apps);
    return true;
}

bool ValidEnums(const SettingsData& data) {
    return BackendFromCode(BackendCode(data.backend)) == data.backend &&
           lang::FromCode(lang::Code(data.nativeLanguage)) == data.nativeLanguage &&
           lang::FromCode(lang::Code(data.foreignLanguage)) == data.foreignLanguage &&
           lang::DirectionFromCode(lang::DirectionCode(data.readDirection)) == data.readDirection &&
           lang::DirectionFromCode(lang::DirectionCode(data.rewriteDirection)) == data.rewriteDirection &&
           lang::StyleFromCode(lang::StyleCode(data.readStyle)) == data.readStyle &&
           lang::StyleFromCode(lang::StyleCode(data.rewriteStyle)) == data.rewriteStyle &&
           ShortcutActionFromCode(ShortcutActionCode(data.readShortcutAction)) == data.readShortcutAction &&
           ShortcutActionFromCode(ShortcutActionCode(data.rewriteShortcutAction)) == data.rewriteShortcutAction &&
           (data.selectionTrigger == SelectionTrigger::None || data.selectionTrigger == SelectionTrigger::Icon ||
            data.selectionTrigger == SelectionTrigger::Auto);
}

json::Value HotkeyJson(const Hotkey& key) {
    return json::Object{{"modifiers", static_cast<int>(key.modifiers)}, {"vk", static_cast<int>(key.vk)}};
}

} // namespace

bool ReadGlobals(const json::Value& root, SettingsData& data, bool strict) {
    return ReadStrings(root, data, strict) && ReadEnums(root, data, strict) && ReadNumbers(root, data, strict) &&
           ReadBool(root, "enabled", data.enabled, strict) &&
           ReadBool(root, "rewritePreview", data.rewritePreview, strict) &&
           ReadBool(root, "hasCompletedFirstLaunch", data.hasCompletedFirstLaunch, strict) &&
           ReadHotkey(root, "readShortcut", data.readShortcut, strict) &&
           ReadHotkey(root, "rewriteShortcut", data.rewriteShortcut, strict) &&
           ReadApps(root, data, strict) && ValidateGlobals(data);
}

bool ValidateGlobals(const SettingsData& data) {
    if (!ValidEnums(data) || !ValidHotkey(data.readShortcut) || !ValidHotkey(data.rewriteShortcut) ||
        data.ollamaPort < 1 || data.ollamaPort > 65535 || data.lastUpdateCheck < 0 ||
        data.lastUpdateCheck > kMaxExactTimestamp || data.skipApps.size() > kMaxApps) return false;
    for (const auto& field : kStrings) if (!ValidUtf8(data.*field.member, field.limit)) return false;
    for (const auto& app : data.skipApps) if (!ValidApp(app)) return false;
    return true;
}

void AppendGlobals(const SettingsData& data, json::Object& root) {
    for (const auto& field : kStrings) root[field.key] = data.*field.member;
    root["enabled"] = data.enabled;
    root["backend"] = BackendCode(data.backend);
    root["ollamaPort"] = data.ollamaPort;
    // Compatibility values for old main-thread controls, not the v2 profile source of truth.
    root["nativeLanguage"] = lang::Code(data.nativeLanguage);
    root["foreignLanguage"] = lang::Code(data.foreignLanguage);
    root["readDirection"] = lang::DirectionCode(data.readDirection);
    root["rewriteDirection"] = lang::DirectionCode(data.rewriteDirection);
    root["readShortcut"] = HotkeyJson(data.readShortcut);
    root["rewriteShortcut"] = HotkeyJson(data.rewriteShortcut);
    root["readShortcutAction"] = ShortcutActionCode(data.readShortcutAction);
    root["rewriteShortcutAction"] = ShortcutActionCode(data.rewriteShortcutAction);
    root["selectionTrigger"] = data.selectionTrigger == SelectionTrigger::Icon ? "icon"
                               : data.selectionTrigger == SelectionTrigger::Auto ? "auto" : "none";
    root["rewritePreview"] = data.rewritePreview;
    root["readStyle"] = lang::StyleCode(data.readStyle);
    root["rewriteStyle"] = lang::StyleCode(data.rewriteStyle);
    json::Array apps;
    for (const auto& app : data.skipApps) apps.emplace_back(util::Narrow(app));
    root["skipApps"] = json::Value(std::move(apps));
    root["hasCompletedFirstLaunch"] = data.hasCompletedFirstLaunch;
    root["lastUpdateCheck"] = static_cast<double>(data.lastUpdateCheck);
}

} // namespace settings_codec::detail
