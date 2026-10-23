#include "Settings.h"
#include "Credentials.h"
#include "SettingsCodec.h"
#include "SettingsDiagnostics.h"
#include "SettingsStorage.h"
#include "Util.h"
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
bool ResolveCredential(settings_codec::Decoded& decoded, bool& legacyOrigin) {
    if (decoded.legacy && !decoded.data.openAIKey.empty()) return true;
    if (!decoded.legacy && decoded.ref.empty()) return true;
    const auto result = decoded.legacy ? credentials::ReadLegacyFixedKey() : credentials::ReadKey(decoded.ref);
    if (result.status == credentials::LoadStatus::Found) {
        decoded.data.openAIKey = result.key;
        legacyOrigin = decoded.legacy; // Only an actually read fixed source may later be deleted.
        return true;
    }
    if (decoded.legacy && result.status == credentials::LoadStatus::NotFound) return true;
    decoded.state = StorageState::IoError;
    decoded.error = result.status == credentials::LoadStatus::NotFound
                        ? "Referenced API credential is missing; settings are read-only"
                        : "Cannot read API credential; settings are read-only";
    return false;
}
}

std::wstring Hotkey::displayString() const {
    if (!isConfigured()) return L"Not set";
    std::wstring result;
    if (modifiers & MOD_CONTROL) result += L"Ctrl+";
    if (modifiers & MOD_SHIFT) result += L"Shift+";
    if (modifiers & MOD_ALT) result += L"Alt+";
    if (modifiers & MOD_WIN) result += L"Win+";
    wchar_t name[64]{};
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG scanCode = static_cast<LONG>(scan << 16);
    switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_INSERT: case VK_DELETE: scanCode |= 1 << 24; break;
    }
    if (GetKeyNameTextW(scanCode, name, 64) > 0) result += name;
    else result += static_cast<wchar_t>(vk);
    return result;
}

Settings::Settings() : committed_(static_cast<const SettingsData&>(*this)) {}

Settings& Settings::shared() {
    static Settings settings = [] {
        Settings loaded;
        loaded.load();
        return loaded;
    }();
    return settings;
}

std::wstring Settings::filePath() {
    wchar_t isolated[MAX_PATH]{};
    SetLastError(ERROR_SUCCESS);
    const DWORD size = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", isolated, MAX_PATH);
    if (size >= MAX_PATH || (!size && GetLastError() != ERROR_ENVVAR_NOT_FOUND)) {
        throw std::runtime_error("Invalid isolated settings directory");
    }
    // Never let Util's bounded environment reader fall back to real AppData for
    // a present but oversized/invalid isolation value.
    return util::AppDataDir() + L"\\settings.json";
}

void Settings::adopt(SettingsData data, std::string ref, bool legacyOrigin, bool versioned, uint64_t epoch) {
    SettingsData ledger = data; // All potentially throwing copies precede publication.
    static_assert(std::is_nothrow_move_assignable_v<SettingsData>);
    static_cast<SettingsData&>(*this) = std::move(data);
    committed_ = std::move(ledger);
    credentialRef_ = std::move(ref);
    connectionEpoch_ = epoch;
    legacyOrigin_ = legacyOrigin;
    versioned_ = versioned;
    storageState_ = StorageState::Ready;
    storageError_.clear();
    storageWarning_.clear();
}

void Settings::load() {
    try {
        const auto file = settings_storage::Read(filePath());
        if (file.state != StorageState::Ready) {
            storageState_ = file.state;
            storageError_ = file.error;
            return;
        }
        settings_codec::Decoded decoded;
        if (file.missing) decoded.state = StorageState::Ready;
        else {
            bool parsed = false;
            const auto root = json::Parse(file.bytes, &parsed);
            if (parsed && root.isObject()) decoded = settings_codec::Decode(root);
            else decoded.error = "Invalid settings JSON";
        }
        bool legacyOrigin = false;
        if (decoded.state == StorageState::Ready) ResolveCredential(decoded, legacyOrigin);
        if (decoded.state != StorageState::Ready) {
            storageState_ = decoded.state;
            storageError_ = decoded.error;
            return;
        }
        const bool changed = settings_codec::ConnectionChanged(committed_, decoded.data);
        if (changed && connectionEpoch_ == std::numeric_limits<uint64_t>::max()) {
            storageState_ = StorageState::IoError;
            storageError_ = "Connection version exhausted; restart required";
            return;
        }
        Settings prepared = *this;
        prepared.adopt(std::move(decoded.data), std::move(decoded.ref), legacyOrigin,
                       !file.missing && !decoded.legacy, connectionEpoch_ + (changed ? 1 : 0));
        *this = std::move(prepared); // Load never automatically migrates/writes a legacy file.
    } catch (...) {
        storageState_ = StorageState::IoError;
        storageError_ = "Cannot load settings safely";
        settings_diagnostics::Log("settings: load failed before publication");
    }
}

bool Settings::save(std::string* error) { return commit(*this, error); }

bool Settings::save(std::string* error) const {
    if (isReadOnly()) {
        if (error) *error = storageError_.empty() ? "Settings storage is read-only" : storageError_;
        return false;
    }
    if (!versioned_ || legacyOrigin_ || static_cast<const SettingsData&>(*this) != committed_ ||
        (committed_.openAIKey.empty() != credentialRef_.empty())) {
        if (error) *error = "Uncommitted settings or legacy migration require mutable save/commit";
        return false;
    }
    try {
        Settings copy = *this;
        return copy.commit(*this, error); // Receiver-owned idempotent values; never mutate const/global state.
    } catch (...) {
        if (error) *error = "Cannot prepare an idempotent settings save";
        return false;
    }
}

SettingsSnapshot Settings::snapshot() const {
    SettingsSnapshot result;
    result.profileConfig = committed_.profileConfig;
    result.connectionEpoch = connectionEpoch_;
    result.backend = committed_.backend;
    result.ollamaHost = committed_.ollamaHost;
    result.ollamaPort = committed_.ollamaPort;
    result.ollamaModel = committed_.ollamaModel;
    result.openAIBaseURL = committed_.openAIBaseURL;
    result.openAIModel = committed_.openAIModel;
    result.openAIKey = committed_.openAIKey;
    result.openAICredentialRef = credentialRef_;
    result.hasCompletedFirstLaunch = committed_.hasCompletedFirstLaunch;
    return result;
}
