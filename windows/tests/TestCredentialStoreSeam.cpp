#include "TestCredentialStoreSeam.h"
#include <utility>

credentials::LoadResult FakeCredentialStore::ReadKey(const std::string& ref) {
    ++loadKeyCalls;
    const auto it = entries.find(ref);
    const bool found = !simulateReadFailure && it != entries.end();
    events.push_back({CredentialOperation::Load, ref, found});
    if (simulateReadFailure) return {};
    return {found ? credentials::LoadStatus::Found : credentials::LoadStatus::NotFound,
            found ? it->second : std::string{}};
}

std::string FakeCredentialStore::LoadKey(const std::string& ref) { return ReadKey(ref).key; }

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
    events.push_back({CredentialOperation::Delete, ref, !simulateDeleteFailure});
    if (simulateDeleteFailure) return false;
    auto updated = entries;
    updated.erase(ref);
    entries = std::move(updated);
    return true;
}

credentials::LoadResult FakeCredentialStore::ReadLegacyFixedKey() {
    ++loadLegacyCalls;
    const bool found = !simulateReadFailure && !legacyFixedKey.empty();
    events.push_back({CredentialOperation::LoadLegacy, {}, found});
    if (simulateReadFailure) return {};
    return {found ? credentials::LoadStatus::Found : credentials::LoadStatus::NotFound,
            found ? legacyFixedKey : std::string{}};
}

std::string FakeCredentialStore::LoadLegacyFixedKey() { return ReadLegacyFixedKey().key; }

bool FakeCredentialStore::SaveLegacyFixedKey(const std::string& key) {
    ++saveLegacyCalls;
    events.push_back({CredentialOperation::SaveLegacy, {}, true});
    legacyFixedKey = key;
    return true;
}

bool FakeCredentialStore::DeleteLegacyFixedKey() {
    ++deleteLegacyCalls;
    events.push_back({CredentialOperation::DeleteLegacy, {}, !simulateDeleteFailure});
    if (simulateDeleteFailure) return false;
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
