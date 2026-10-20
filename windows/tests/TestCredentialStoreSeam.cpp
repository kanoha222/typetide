#include "TestCredentialStoreSeam.h"
#include <utility>

std::string FakeCredentialStore::LoadKey(const std::string& ref) {
    ++loadKeyCalls;
    const auto it = entries.find(ref);
    const bool found = it != entries.end();
    events.push_back({CredentialOperation::Load, ref, found});
    return found ? it->second : std::string{};
}

bool FakeCredentialStore::StageKey(const std::string& ref, const std::string& key) {
    ++stageKeyCalls;
    const bool succeeded = !simulateStageFailure;
    events.push_back({CredentialOperation::Stage, ref, succeeded});
    if (!succeeded) return false;
    auto updated = entries;
    updated.insert_or_assign(ref, key);
    entries = std::move(updated);
    return true;
}

bool FakeCredentialStore::DeleteKey(const std::string& ref) {
    ++deleteKeyCalls;
    events.push_back({CredentialOperation::Delete, ref, true});
    auto updated = entries;
    updated.erase(ref);
    entries = std::move(updated);
    return true;
}

std::string FakeCredentialStore::LoadLegacyFixedKey() {
    ++loadLegacyCalls;
    events.push_back({CredentialOperation::LoadLegacy, {}, !legacyFixedKey.empty()});
    return legacyFixedKey;
}

bool FakeCredentialStore::SaveLegacyFixedKey(const std::string& key) {
    ++saveLegacyCalls;
    events.push_back({CredentialOperation::SaveLegacy, {}, true});
    legacyFixedKey = key;
    return true;
}

bool FakeCredentialStore::DeleteLegacyFixedKey() {
    ++deleteLegacyCalls;
    events.push_back({CredentialOperation::DeleteLegacy, {}, true});
    legacyFixedKey = {};
    return true;
}

ScopedCredentialStore::ScopedCredentialStore(credentials::StoreBackend& backend)
    : prior_(credentials::GetStoreBackendForTesting()) {
    credentials::SetStoreBackendForTesting(&backend);
}

ScopedCredentialStore::~ScopedCredentialStore() {
    credentials::SetStoreBackendForTesting(prior_);
}
