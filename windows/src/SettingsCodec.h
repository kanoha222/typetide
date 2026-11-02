#pragma once
#include "Json.h"
#include "Settings.h"

namespace settings_codec {

struct Decoded {
    StorageState state = StorageState::Invalid;
    SettingsData data;
    std::string ref;
    bool legacy = false;
    std::string error;
};

// Pure value operations. No filesystem, credential backend, or singleton access.
Decoded Decode(const json::Value& root);
bool Validate(const SettingsData& data, std::string& error);
json::Object Encode(const SettingsData& data, const std::string& ref);
bool ConnectionChanged(const SettingsData& before, const SettingsData& after);

namespace detail {
bool ReadGlobals(const json::Value& root, SettingsData& data, bool strict);
bool ValidateGlobals(const SettingsData& data);
void AppendGlobals(const SettingsData& data, json::Object& root);
}

} // namespace settings_codec
