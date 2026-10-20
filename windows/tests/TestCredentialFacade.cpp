// Link-time transaction-test facade. Deliberately no Windows/WinCred includes
// or real credential implementation in this test executable.
#include "../src/Credentials.h"

namespace {
credentials::StoreBackend* g_backend = nullptr;
}

namespace credentials {

void SetStoreBackendForTesting(StoreBackend* backend) {
    g_backend = backend;
}

StoreBackend* GetStoreBackendForTesting() {
    return g_backend;
}

std::string LoadCloudApiKey() {
    return g_backend ? g_backend->LoadLegacyFixedKey() : std::string{};
}

bool SaveCloudApiKey(const std::string& key) {
    if (!g_backend) return false;
    return key.empty() ? g_backend->DeleteLegacyFixedKey()
                       : g_backend->SaveLegacyFixedKey(key);
}

std::string LoadKey(const std::string& ref) {
    if (!IsValidRef(ref) || !g_backend) return {};
    return g_backend->LoadKey(ref);
}

bool StageKey(const std::string& ref, const std::string& key) {
    if (!IsValidRef(ref) || key.empty() || !g_backend) return false;
    return g_backend->StageKey(ref, key);
}

bool DeleteKey(const std::string& ref) {
    if (!IsValidRef(ref) || !g_backend) return false;
    return g_backend->DeleteKey(ref);
}

std::string LoadLegacyFixedKey() {
    return g_backend ? g_backend->LoadLegacyFixedKey() : std::string{};
}

bool DeleteLegacyFixedKey() {
    return g_backend && g_backend->DeleteLegacyFixedKey();
}

} // namespace credentials
