#include "Credentials.h"
#include <windows.h>
#include <wincred.h>

namespace {
constexpr wchar_t kTarget[] = L"TypeTide/OpenAICompatibleApiKey";
credentials::StoreBackend* g_testBackend = nullptr;
}

namespace credentials {

void SetStoreBackendForTesting(StoreBackend* backend) {
    g_testBackend = backend;
}

StoreBackend* GetStoreBackendForTesting() {
    return g_testBackend;
}

std::string LoadCloudApiKey() {
    if (g_testBackend) return g_testBackend->LoadLegacyFixedKey();
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(kTarget, CRED_TYPE_GENERIC, 0, &credential)) return {};

    std::string key;
    if (credential->CredentialBlob && credential->CredentialBlobSize) {
        const char* bytes = reinterpret_cast<const char*>(credential->CredentialBlob);
        key.assign(bytes, bytes + credential->CredentialBlobSize);
    }
    CredFree(credential);
    return key;
}

bool SaveCloudApiKey(const std::string& key) {
    if (g_testBackend) {
        return key.empty() ? g_testBackend->DeleteLegacyFixedKey()
                           : g_testBackend->SaveLegacyFixedKey(key);
    }
    if (key.empty()) {
        return CredDeleteW(kTarget, CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
    }

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(kTarget);
    credential.CredentialBlobSize = static_cast<DWORD>(key.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(key.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"TypeTide");
    return CredWriteW(&credential, 0) != FALSE;
}

std::string LoadKey(const std::string& ref) {
    if (!IsValidRef(ref) || !g_testBackend) return {};
    return g_testBackend->LoadKey(ref);
}

bool StageKey(const std::string& ref, const std::string& key) {
    if (!IsValidRef(ref) || key.empty() || !g_testBackend) return false;
    return g_testBackend->StageKey(ref, key);
}

bool DeleteKey(const std::string& ref) {
    if (!IsValidRef(ref) || !g_testBackend) return false;
    return g_testBackend->DeleteKey(ref);
}

std::string LoadLegacyFixedKey() {
    return g_testBackend ? g_testBackend->LoadLegacyFixedKey() : std::string{};
}

bool DeleteLegacyFixedKey() {
    return g_testBackend && g_testBackend->DeleteLegacyFixedKey();
}

} // namespace credentials
