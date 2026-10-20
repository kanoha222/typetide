#pragma once
#include "../src/Credentials.h"
#include <cstddef>
#include <map>
#include <string>
#include <vector>

enum class CredentialOperation { Load, Stage, Delete, LoadLegacy, SaveLegacy, DeleteLegacy };

struct CredentialEvent {
    CredentialOperation operation;
    std::string ref;
    bool succeeded;
};

class FakeCredentialStore : public credentials::StoreBackend {
public:
    std::string LoadKey(const std::string& ref) override;
    bool StageKey(const std::string& ref, const std::string& key) override;
    bool DeleteKey(const std::string& ref) override;
    std::string LoadLegacyFixedKey() override;
    bool SaveLegacyFixedKey(const std::string& key) override;
    bool DeleteLegacyFixedKey() override;

    // Each case creates a fresh store; counters/events are never reset mid-case.
    std::map<std::string, std::string> entries;
    std::string legacyFixedKey;
    std::vector<CredentialEvent> events; // Records order/ref/result, never a key.
    size_t loadKeyCalls = 0;
    size_t stageKeyCalls = 0;
    size_t deleteKeyCalls = 0;
    size_t loadLegacyCalls = 0;
    size_t saveLegacyCalls = 0;
    size_t deleteLegacyCalls = 0;
    bool simulateStageFailure = false;
};

class ScopedCredentialStore {
public:
    explicit ScopedCredentialStore(credentials::StoreBackend& backend);
    ~ScopedCredentialStore();
    ScopedCredentialStore(const ScopedCredentialStore&) = delete;
    ScopedCredentialStore& operator=(const ScopedCredentialStore&) = delete;

private:
    credentials::StoreBackend* prior_;
};
