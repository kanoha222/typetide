// Pure schema codec and legacy migration; never reads credentials or writes files.
#pragma once
#include "Json.h"
#include "TranslationProfile.h"
#include <string>

enum class ProfileLoadState { Current, MigratedLegacy, UnsupportedSchema, Invalid };

struct ProfileLoadResult {
    ProfileLoadState state = ProfileLoadState::Invalid;
    ProfileConfiguration configuration;
    std::string error;
};

namespace profiles::store {

ProfileLoadResult Decode(const json::Value& root);
// Returns the schemaVersion/profiles/three binding fields for the settings root.
json::Object Encode(const ProfileConfiguration& configuration);

} // namespace profiles::store
