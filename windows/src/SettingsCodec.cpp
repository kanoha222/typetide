#include "SettingsCodec.h"
#include "Credentials.h"
#include "TranslationProfileStore.h"
#include <utility>

namespace settings_codec {

bool Validate(const SettingsData& data, std::string& error) {
    if (!profiles::Validate(data.profileConfig).ok) {
        error = "Invalid translation profile structure or bindings";
        return false;
    }
    if (!detail::ValidateGlobals(data)) {
        error = "Invalid settings metadata or connection values";
        return false;
    }
    if (!data.openAIKey.empty() && !credentials::IsValidKey(data.openAIKey)) {
        error = "API key contains invalid bytes or exceeds the credential byte limit";
        return false;
    }
    return true;
}

Decoded Decode(const json::Value& root) {
    Decoded result;
    const auto decodedProfiles = profiles::store::Decode(root);
    if (decodedProfiles.state == ProfileLoadState::UnsupportedSchema) {
        result.state = StorageState::UnsupportedSchema;
        result.error = "Settings use an unsupported future schema";
        return result;
    }
    if (decodedProfiles.state == ProfileLoadState::Invalid) {
        result.error = "Invalid settings schema, profiles, or bindings";
        return result;
    }
    result.data.profileConfig = decodedProfiles.configuration;
    result.legacy = decodedProfiles.state == ProfileLoadState::MigratedLegacy;
    if (!detail::ReadGlobals(root, result.data, !result.legacy)) {
        result.error = "Invalid settings metadata";
        return result;
    }
    if (result.legacy) {
        const auto item = root.object()->find("openAIKey");
        if (item != root.object()->end() && !item->second.isString()) {
            result.error = "Invalid legacy credential field";
            return result;
        }
        result.data.openAIKey = root["openAIKey"].asString();
    } else if (const auto item = root.object()->find("openAICredentialRef"); item != root.object()->end()) {
        if (!item->second.isString() || (!item->second.asString().empty() && !credentials::IsValidRef(item->second.asString()))) {
            result.error = "Invalid opaque credential reference";
            return result;
        }
        result.ref = credentials::CanonicalRef(item->second.asString());
    }
    if (!Validate(result.data, result.error)) return result;
    result.state = StorageState::Ready;
    return result;
}

json::Object Encode(const SettingsData& data, const std::string& ref) {
    auto root = profiles::store::Encode(data.profileConfig);
    detail::AppendGlobals(data, root);
    root["openAICredentialRef"] = ref; // Explicit NoKey is an empty string. Never serialize the key.
    return root;
}

bool ConnectionChanged(const SettingsData& before, const SettingsData& after) {
    return before.backend != after.backend || before.ollamaHost != after.ollamaHost ||
           before.ollamaPort != after.ollamaPort || before.ollamaModel != after.ollamaModel ||
           before.openAIBaseURL != after.openAIBaseURL || before.openAIModel != after.openAIModel ||
           before.openAIKey != after.openAIKey;
}

} // namespace settings_codec
