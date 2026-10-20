#include "Settings.h"
#include "Credentials.h"
#include "Json.h"
#include "Util.h"
#include <cmath>
#include <fstream>
#include <limits>

std::wstring Hotkey::displayString() const {
    if (!isConfigured()) return L"Not set";
    std::wstring s;
    if (modifiers & MOD_CONTROL) s += L"Ctrl+";
    if (modifiers & MOD_SHIFT) s += L"Shift+";
    if (modifiers & MOD_ALT) s += L"Alt+";
    if (modifiers & MOD_WIN) s += L"Win+";

    wchar_t name[64]{};
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG l = (LONG)(scan << 16);
    switch (vk) {  // 扩展键需要置扩展位才能取到正确名字
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_INSERT: case VK_DELETE:
        l |= 1 << 24; break;
    }
    if (GetKeyNameTextW(l, name, 64) > 0) s += name;
    else s += (wchar_t)vk;
    return s;
}

Settings& Settings::shared() {
    static Settings s = [] {
        Settings v;
        v.load();
        return v;
    }();
    return s;
}

std::wstring Settings::filePath() {
    return util::AppDataDir() + L"\\settings.json";
}

namespace {

bool checkFileAttributes(const std::wstring& path, StorageState& state, std::string& err) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) {
            state = StorageState::Ready;
            err.clear();
            return false;
        }
        state = StorageState::IoError;
        err = "access error";
        return false;
    }
    return true;
}

bool readFileContent(const std::wstring& path, std::string& out,
                     StorageState& state, std::string& err) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        state = StorageState::IoError;
        err = "cannot open file";
        return false;
    }
    const auto endPos = f.tellg();
    if (endPos < 0) {
        state = StorageState::IoError;
        err = "seek error";
        return false;
    }
    const auto len = static_cast<size_t>(endPos);
    if (len == 0) {
        state = StorageState::Invalid;
        err = "empty file";
        return false;
    }
    if (len > 1024 * 1024) {
        state = StorageState::Invalid;
        err = "refusing invalid file size";
        util::Log("settings: refusing invalid file size");
        return false;
    }
    f.seekg(0, std::ios::beg);
    if (!f.good()) {
        state = StorageState::IoError;
        err = "seek error";
        return false;
    }
    out.resize(len);
    f.read(out.data(), static_cast<std::streamsize>(len));
    if (f.gcount() != static_cast<std::streamsize>(len) || f.fail()) {
        state = StorageState::IoError;
        err = "read error";
        return false;
    }
    return true;
}

bool checkSchemaVersion(const json::Value& root, StorageState& state, std::string& err) {
    const json::Object* obj = root.object();
    if (!obj) return false;
    auto it = obj->find("schemaVersion");
    if (it == obj->end()) return true;

    const json::Value& sv = it->second;
    if (sv.type() != json::Value::Type::Number) {
        state = StorageState::Invalid;
        err = "invalid schemaVersion type";
        return false;
    }
    double num = sv.asNumber(0.0);
    if (!std::isfinite(num) || num <= 0.0 || std::floor(num) != num ||
        num > static_cast<double>(std::numeric_limits<int>::max())) {
        state = StorageState::Invalid;
        err = "invalid schemaVersion number";
        return false;
    }

    state = StorageState::UnsupportedSchema;
    err = "versioned schema not supported in flat settings";
    return false;
}

void loadLegacyCore(const json::Value& root, Settings& s, bool& migratedKey) {
    auto str = [&](const char* k, const std::string& def) {
        return root[k].isString() ? root[k].asString() : def;
    };
    auto boolean = [&](const char* k, bool def) {
        return root[k].type() == json::Value::Type::Bool ? root[k].asBool() : def;
    };

    s.enabled = boolean("enabled", s.enabled);
    const std::string backendCode = str("backend", "builtin");
    s.backend = backendCode == "builtin" ? TranslationBackend::BuiltIn
              : backendCode == "openai" ? TranslationBackend::OpenAI : TranslationBackend::Ollama;

    s.ollamaHost = str("ollamaHost", s.ollamaHost);
    s.ollamaPort = root["ollamaPort"].isNull() ? s.ollamaPort : root["ollamaPort"].asInt(s.ollamaPort);
    if (s.ollamaPort < 1 || s.ollamaPort > 65535) s.ollamaPort = 11434;
    s.ollamaModel = str("ollamaModel", s.ollamaModel);
    if (s.ollamaHost.size() > 2048) s.ollamaHost = "http://127.0.0.1";
    if (s.ollamaModel.size() > 256) s.ollamaModel = "qwen2.5:3b";

    s.openAIBaseURL = str("openAIBaseURL", s.openAIBaseURL);
    if (s.openAIBaseURL.size() > 2048) s.openAIBaseURL = "https://api.openai.com/v1";

    const std::string legacyKey = str("openAIKey", "");
    wchar_t testDir[2]{};
    const bool isolatedTest = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", testDir, 2) != 0;
    if (isolatedTest) {
        s.openAIKey = legacyKey;
    } else {
        s.openAIKey = credentials::LoadCloudApiKey();
        if (s.openAIKey.empty() && !legacyKey.empty()) {
            s.openAIKey = legacyKey;
            credentials::SaveCloudApiKey(s.openAIKey);
            migratedKey = true;
        }
    }
    s.openAIModel = str("openAIModel", s.openAIModel);
    if (s.openAIModel.size() > 256) s.openAIModel = "gpt-5-mini";
}

void loadLegacyShortcuts(const json::Value& root, Settings& s) {
    auto str = [&](const char* k, const std::string& def) {
        return root[k].isString() ? root[k].asString() : def;
    };
    if (auto l = lang::FromCode(str("nativeLanguage", ""))) s.nativeLanguage = *l;
    if (auto l = lang::FromCode(str("foreignLanguage", ""))) s.foreignLanguage = *l;
    if (auto d = lang::DirectionFromCode(str("readDirection", ""))) s.readDirection = *d;
    if (auto d = lang::DirectionFromCode(str("rewriteDirection", ""))) s.rewriteDirection = *d;

    auto loadHotkey = [&](const char* k, Hotkey& hk) {
        const json::Value& v = root[k];
        if (v.isObject()) {
            UINT modifiers = (UINT)v["modifiers"].asInt((int)hk.modifiers);
            UINT vk = (UINT)v["vk"].asInt((int)hk.vk);
            constexpr UINT allowed = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
            if ((modifiers & ~allowed) == 0 && vk < 256) {
                hk.modifiers = modifiers;
                hk.vk = vk;
            }
        }
    };
    loadHotkey("readShortcut", s.readShortcut);
    loadHotkey("rewriteShortcut", s.rewriteShortcut);

    auto loadAction = [&](const char* key, ShortcutAction fallback) {
        const std::string val = str(key, "");
        if (val == "nativePopup") return ShortcutAction::NativePopup;
        if (val == "foreignPopup") return ShortcutAction::ForeignPopup;
        if (val == "smartReplace") return ShortcutAction::SmartReplace;
        if (val == "nativeReplace") return ShortcutAction::NativeReplace;
        if (val == "foreignReplace") return ShortcutAction::ForeignReplace;
        if (val == "smartPopup") return ShortcutAction::SmartPopup;
        return fallback;
    };
    s.readShortcutAction = loadAction("readShortcutAction", ShortcutAction::SmartPopup);
    s.rewriteShortcutAction = loadAction("rewriteShortcutAction", ShortcutAction::SmartReplace);
}

void loadLegacyBehaviorAndApps(const json::Value& root, Settings& s) {
    auto str = [&](const char* k, const std::string& def) {
        return root[k].isString() ? root[k].asString() : def;
    };
    auto boolean = [&](const char* k, bool def) {
        return root[k].type() == json::Value::Type::Bool ? root[k].asBool() : def;
    };

    std::string trig = str("selectionTrigger", "none");
    s.selectionTrigger = trig == "icon" ? SelectionTrigger::Icon
                       : trig == "auto" ? SelectionTrigger::Auto
                       : SelectionTrigger::None;
    s.rewritePreview = boolean("rewritePreview", s.rewritePreview);
    if (auto s2 = lang::StyleFromCode(str("readStyle", ""))) s.readStyle = *s2;
    if (auto s2 = lang::StyleFromCode(str("rewriteStyle", ""))) s.rewriteStyle = *s2;

    s.skipApps.clear();
    if (const json::Array* arr = root["skipApps"].array()) {
        for (auto& item : *arr) {
            if (!item.isString() || s.skipApps.size() >= 100) continue;
            std::wstring app = util::Trim(util::Widen(item.asString()));
            if (app.empty() || app.size() > 260) continue;
            bool dup = false;
            for (const auto& existing : s.skipApps)
                if (util::ToLower(existing) == util::ToLower(app)) dup = true;
            if (!dup) s.skipApps.push_back(std::move(app));
        }
    }
    s.hasCompletedFirstLaunch = boolean("hasCompletedFirstLaunch", false);
    s.lastUpdateCheck = (long long)root["lastUpdateCheck"].asNumber(0);
}

json::Object buildLegacyCoreObject(const Settings& s) {
    json::Object root;
    root["enabled"] = s.enabled;
    root["backend"] = s.backend == TranslationBackend::BuiltIn ? "builtin"
                    : s.backend == TranslationBackend::OpenAI ? "openai" : "ollama";
    root["ollamaHost"] = s.ollamaHost;
    root["ollamaPort"] = s.ollamaPort;
    root["ollamaModel"] = s.ollamaModel;
    root["openAIBaseURL"] = s.openAIBaseURL;
    root["openAIModel"] = s.openAIModel;
    root["nativeLanguage"] = lang::Code(s.nativeLanguage);
    root["foreignLanguage"] = lang::Code(s.foreignLanguage);
    root["readDirection"] = lang::DirectionCode(s.readDirection);
    root["rewriteDirection"] = lang::DirectionCode(s.rewriteDirection);
    return root;
}

void appendLegacyShortcutsAndApps(const Settings& s, json::Object& root) {
    auto hotkeyJson = [](const Hotkey& hk) {
        json::Object o;
        o["modifiers"] = (int)hk.modifiers;
        o["vk"] = (int)hk.vk;
        return json::Value(std::move(o));
    };
    root["readShortcut"] = hotkeyJson(s.readShortcut);
    root["rewriteShortcut"] = hotkeyJson(s.rewriteShortcut);

    auto actionCode = [](ShortcutAction action) -> std::string {
        switch (action) {
        case ShortcutAction::NativePopup: return "nativePopup";
        case ShortcutAction::ForeignPopup: return "foreignPopup";
        case ShortcutAction::SmartReplace: return "smartReplace";
        case ShortcutAction::NativeReplace: return "nativeReplace";
        case ShortcutAction::ForeignReplace: return "foreignReplace";
        case ShortcutAction::SmartPopup:
        default: return "smartPopup";
        }
    };
    root["readShortcutAction"] = actionCode(s.readShortcutAction);
    root["rewriteShortcutAction"] = actionCode(s.rewriteShortcutAction);

    root["selectionTrigger"] = s.selectionTrigger == SelectionTrigger::Icon ? "icon"
                             : s.selectionTrigger == SelectionTrigger::Auto ? "auto"
                             : "none";
    root["rewritePreview"] = s.rewritePreview;
    root["readStyle"] = lang::StyleCode(s.readStyle);
    root["rewriteStyle"] = lang::StyleCode(s.rewriteStyle);

    json::Array apps;
    for (const auto& a : s.skipApps) apps.push_back(util::Narrow(a));
    root["skipApps"] = json::Value(std::move(apps));

    root["hasCompletedFirstLaunch"] = s.hasCompletedFirstLaunch;
    root["lastUpdateCheck"] = (double)s.lastUpdateCheck;
}

bool writeTempAndReplace(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            util::Log("settings: cannot open temp file (%lu)", GetLastError());
            return false;
        }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.flush();
        if (!f.good()) {
            util::Log("settings: temp write failed");
            f.close();
            DeleteFileW(tmp.c_str());
            return false;
        }
    }
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (MoveFileExW(tmp.c_str(), path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        if (attempt < 3) Sleep(15 * (attempt + 1));
    }
    util::Log("settings: atomic replace failed (%lu)", GetLastError());
    DeleteFileW(tmp.c_str());
    return false;
}

} // namespace

void Settings::load() {
    storageState_ = StorageState::Ready;
    storageError_.clear();
    const std::wstring path = filePath();
    if (!checkFileAttributes(path, storageState_, storageError_)) {
        return;
    }
    std::string content;
    if (!readFileContent(path, content, storageState_, storageError_)) {
        return;
    }
    bool ok = false;
    json::Value root = json::Parse(content, &ok);
    if (!ok || !root.isObject()) {
        storageState_ = StorageState::Invalid;
        storageError_ = "invalid JSON";
        util::Log("settings: invalid JSON, using safe defaults");
        return;
    }
    if (!checkSchemaVersion(root, storageState_, storageError_)) {
        return;
    }
    bool migratedLegacyKey = false;
    loadLegacyCore(root, *this, migratedLegacyKey);
    loadLegacyShortcuts(root, *this);
    loadLegacyBehaviorAndApps(root, *this);
    if (migratedLegacyKey) {
        save();
    }
}

bool Settings::save() const {
    if (storageState_ != StorageState::Ready) {
        util::Log("settings: save rejected, persistent state is %d", static_cast<int>(storageState_));
        return false;
    }

    json::Object root = buildLegacyCoreObject(*this);
    appendLegacyShortcutsAndApps(*this, root);

    wchar_t testDir[2]{};
    const bool isolatedTest = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", testDir, 2) != 0;
    if (isolatedTest) {
        root["openAIKey"] = openAIKey;
    } else if (!credentials::SaveCloudApiKey(openAIKey)) {
        util::Log("settings: credential save failed (%lu)", GetLastError());
        return false;
    }

    return writeTempAndReplace(filePath(), json::Value(std::move(root)).dump());
}

bool Settings::commit(const Settings& candidate, std::string* error) {
    // RED scaffold only. Do not publish, persist or access credentials here.
    (void)candidate;
    if (error) *error = "NotReady";
    return false;
}

SettingsSnapshot Settings::snapshot() const {
    // RED value shell only: committed-state isolation is deliberately not implemented.
    SettingsSnapshot snap;
    snap.profileConfig = profileConfig;
    snap.connectionEpoch = connectionEpoch_;
    snap.backend = backend;
    snap.ollamaHost = ollamaHost;
    snap.ollamaPort = ollamaPort;
    snap.ollamaModel = ollamaModel;
    snap.openAIBaseURL = openAIBaseURL;
    snap.openAIModel = openAIModel;
    snap.openAIKey = openAIKey;
    snap.openAICredentialRef = credentialRef_;
    snap.hasCompletedFirstLaunch = hasCompletedFirstLaunch;
    return snap;
}
