// Link-time transaction-test facade. No WinCred includes/imports/production store.
#include "../src/Credentials.h"

namespace {
credentials::StoreBackend* g_backend = nullptr;
credentials::LoadResult Checked(credentials::LoadResult result) {
    if (result.status == credentials::LoadStatus::Found && !credentials::IsValidKey(result.key)) return {};
    if (result.status != credentials::LoadStatus::Found) result.key.clear();
    return result;
}
}

namespace credentials {

void SetStoreBackendForTesting(StoreBackend* backend) { g_backend = backend; }
StoreBackend* GetStoreBackendForTesting() { return g_backend; }

LoadResult ReadKey(const std::string& ref) {
    if (!IsValidRef(ref) || !g_backend) return {};
    return Checked(g_backend->ReadKey(CanonicalRef(ref)));
}

LoadResult ReadLegacyFixedKey() {
    return g_backend ? Checked(g_backend->ReadLegacyFixedKey()) : LoadResult{};
}

std::string LoadCloudApiKey() { return ReadLegacyFixedKey().key; }
std::string LoadKey(const std::string& ref) { return ReadKey(ref).key; }
std::string LoadLegacyFixedKey() { return ReadLegacyFixedKey().key; }

bool SaveCloudApiKey(const std::string& key) {
    if (!g_backend) return false;
    return key.empty() ? g_backend->DeleteLegacyFixedKey()
                       : IsValidKey(key) && g_backend->SaveLegacyFixedKey(key);
}

bool StageKey(const std::string& ref, const std::string& key) {
    return IsValidRef(ref) && IsValidKey(key) && g_backend && g_backend->StageKey(CanonicalRef(ref), key);
}

bool DeleteKey(const std::string& ref) {
    return IsValidRef(ref) && g_backend && g_backend->DeleteKey(CanonicalRef(ref));
}

bool DeleteLegacyFixedKey() { return g_backend && g_backend->DeleteLegacyFixedKey(); }

} // namespace credentials
