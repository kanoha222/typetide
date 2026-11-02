#include "Credentials.h"
#include <windows.h>
#include <wincred.h>
#include <map>
#include <memory>
#include <utility>

namespace {
constexpr wchar_t kLegacyTarget[] = L"TypeTide/OpenAICompatibleApiKey";
static_assert(credentials::MaxKeyBytes == CRED_MAX_CREDENTIAL_BLOB_SIZE);
credentials::StoreBackend* g_testBackend = nullptr;

struct IsolatedStore {
    std::map<std::string, std::string> entries;
    std::string legacyKey;
};
std::map<std::wstring, IsolatedStore> g_isolatedStores;

enum class ScopeKind { Production, Isolated, Error };
struct Scope {
    ScopeKind kind = ScopeKind::Error;
    std::wstring directory;
};

Scope ResolveScope() {
    SetLastError(ERROR_SUCCESS);
    const DWORD size = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", nullptr, 0);
    if (!size) {
        return {GetLastError() == ERROR_ENVVAR_NOT_FOUND ? ScopeKind::Production : ScopeKind::Error, {}};
    }
    std::wstring directory(size, L'\0');
    const DWORD read = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", directory.data(), size);
    if (!read || read >= size) return {};
    directory.resize(read);
    return {ScopeKind::Isolated, std::move(directory)};
}

std::wstring Target(const std::string& ref) {
    if (ref.empty()) return kLegacyTarget;
    return std::wstring(kLegacyTarget) + L"/" + std::wstring(ref.begin(), ref.end());
}

credentials::LoadResult Checked(credentials::LoadResult result) {
    if (result.status == credentials::LoadStatus::Found && !credentials::IsValidKey(result.key)) return {};
    if (result.status != credentials::LoadStatus::Found) result.key.clear();
    return result;
}

struct CredentialDeleter {
    void operator()(CREDENTIALW* value) const { CredFree(value); }
};

credentials::LoadResult ReadNative(const std::string& ref) {
    const auto target = Target(ref);
    PCREDENTIALW raw = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &raw)) {
        return {GetLastError() == ERROR_NOT_FOUND ? credentials::LoadStatus::NotFound
                                                : credentials::LoadStatus::Error, {}};
    }
    const std::unique_ptr<CREDENTIALW, CredentialDeleter> credential(raw);
    if (!credential || !credential->CredentialBlob || !credential->CredentialBlobSize ||
        credential->CredentialBlobSize > CRED_MAX_CREDENTIAL_BLOB_SIZE) return {};
    const auto* bytes = reinterpret_cast<const char*>(credential->CredentialBlob);
    return Checked({credentials::LoadStatus::Found, std::string(bytes, credential->CredentialBlobSize)});
}

bool WriteNative(const std::string& ref, const std::string& key) {
    const auto target = Target(ref);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(key.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(key.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"TypeTide");
    return CredWriteW(&credential, 0) != FALSE;
}

bool DeleteNative(const std::string& ref) {
    const auto target = Target(ref);
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE || GetLastError() == ERROR_NOT_FOUND;
}

credentials::LoadResult ReadIsolated(const Scope& scope, const std::string& ref) {
    const auto store = g_isolatedStores.find(scope.directory);
    if (store == g_isolatedStores.end()) return {credentials::LoadStatus::NotFound, {}};
    if (ref.empty()) {
        const auto& key = store->second.legacyKey;
        return {key.empty() ? credentials::LoadStatus::NotFound : credentials::LoadStatus::Found, key};
    }
    const auto entry = store->second.entries.find(ref);
    if (entry == store->second.entries.end()) return {credentials::LoadStatus::NotFound, {}};
    return Checked({credentials::LoadStatus::Found, entry->second});
}

bool WriteIsolated(const Scope& scope, const std::string& ref, const std::string& key) {
    auto next = g_isolatedStores;
    if (ref.empty()) next[scope.directory].legacyKey = key;
    else next[scope.directory].entries.insert_or_assign(ref, key);
    g_isolatedStores.swap(next);
    return true;
}

bool DeleteIsolated(const Scope& scope, const std::string& ref) {
    auto next = g_isolatedStores;
    const auto store = next.find(scope.directory);
    if (store == next.end()) return true;
    if (ref.empty()) store->second.legacyKey.clear();
    else store->second.entries.erase(ref);
    g_isolatedStores.swap(next);
    return true;
}

credentials::LoadResult ReadStored(const std::string& ref) {
    if (g_testBackend) {
        return Checked(ref.empty() ? g_testBackend->ReadLegacyFixedKey() : g_testBackend->ReadKey(ref));
    }
    const auto scope = ResolveScope();
    if (scope.kind == ScopeKind::Isolated) return ReadIsolated(scope, ref);
    return scope.kind == ScopeKind::Production ? ReadNative(ref) : credentials::LoadResult{};
}

bool WriteStored(const std::string& ref, const std::string& key) {
    if (g_testBackend) {
        return ref.empty() ? g_testBackend->SaveLegacyFixedKey(key) : g_testBackend->StageKey(ref, key);
    }
    const auto scope = ResolveScope();
    if (scope.kind == ScopeKind::Isolated) return WriteIsolated(scope, ref, key);
    return scope.kind == ScopeKind::Production && WriteNative(ref, key);
}

bool DeleteStored(const std::string& ref) {
    if (g_testBackend) return ref.empty() ? g_testBackend->DeleteLegacyFixedKey() : g_testBackend->DeleteKey(ref);
    const auto scope = ResolveScope();
    if (scope.kind == ScopeKind::Isolated) return DeleteIsolated(scope, ref);
    return scope.kind == ScopeKind::Production && DeleteNative(ref);
}
} // namespace

namespace credentials {

void SetStoreBackendForTesting(StoreBackend* backend) { g_testBackend = backend; }
StoreBackend* GetStoreBackendForTesting() { return g_testBackend; }

LoadResult ReadKey(const std::string& ref) {
    if (!IsValidRef(ref)) return {};
    return ReadStored(CanonicalRef(ref));
}

LoadResult ReadLegacyFixedKey() { return ReadStored({}); }
std::string LoadCloudApiKey() { return ReadLegacyFixedKey().key; }
std::string LoadKey(const std::string& ref) { return ReadKey(ref).key; }
std::string LoadLegacyFixedKey() { return ReadLegacyFixedKey().key; }

bool SaveCloudApiKey(const std::string& key) {
    if (key.empty()) return DeleteStored({});
    return IsValidKey(key) && WriteStored({}, key);
}

bool StageKey(const std::string& ref, const std::string& key) {
    return IsValidRef(ref) && IsValidKey(key) && WriteStored(CanonicalRef(ref), key);
}

bool DeleteKey(const std::string& ref) {
    return IsValidRef(ref) && DeleteStored(CanonicalRef(ref));
}

bool DeleteLegacyFixedKey() { return DeleteStored({}); }

} // namespace credentials
