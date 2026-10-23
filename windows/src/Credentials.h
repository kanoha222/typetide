#pragma once

#include <cstddef>
#include <string>

namespace credentials {

// Opaque suffix only, never a full Credential Manager target or a path.
inline bool IsValidRef(const std::string& ref) {
    constexpr std::string::size_type maxRefBytes = 64;
    if (ref.empty() || ref.size() > maxRefBytes) return false;
    for (unsigned char c : ref) {
        const bool alphanumeric = (c >= 'a' && c <= 'z') ||
                                  (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9');
        if (!alphanumeric && c != '_' && c != '-') return false;
    }
    return true;
}

inline constexpr std::size_t MaxKeyBytes = 5 * 512;
inline bool IsValidKey(const std::string& key) {
    return !key.empty() && key.size() <= MaxKeyBytes && key.find('\0') == std::string::npos;
}

// WinCred generic targets are case-insensitive; use the same canonical suffix in
// every facade, including injected stores. Validation must precede this conversion.
inline std::string CanonicalRef(const std::string& ref) {
    std::string result;
    result.reserve(ref.size());
    for (unsigned char c : ref) result.push_back(c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : char(c));
    return result;
}

enum class LoadStatus { Found, NotFound, Error };
struct LoadResult {
    LoadStatus status = LoadStatus::Error;
    std::string key;
};

// Injected in-memory seam. No ownership is transferred by SetStoreBackendForTesting.
class StoreBackend {
public:
    virtual ~StoreBackend() = default;
    virtual std::string LoadKey(const std::string& ref) = 0;
    virtual bool StageKey(const std::string& ref, const std::string& key) = 0;
    virtual bool DeleteKey(const std::string& ref) = 0;
    virtual std::string LoadLegacyFixedKey() = 0;
    virtual bool SaveLegacyFixedKey(const std::string& key) = 0;
    virtual bool DeleteLegacyFixedKey() = 0;
    // Explicit outcomes are mandatory: an empty legacy string cannot prove NotFound.
    virtual LoadResult ReadKey(const std::string& ref) = 0;
    virtual LoadResult ReadLegacyFixedKey() = 0;
};

void SetStoreBackendForTesting(StoreBackend* backend);
StoreBackend* GetStoreBackendForTesting();

// Legacy string facades remain source-compatible; Settings uses typed reads.
// Injected backends take precedence. TYPETIDE_DATA_DIR without one uses only a
// directory-scoped process-local store, never the user's Credential Manager.
std::string LoadCloudApiKey();
bool SaveCloudApiKey(const std::string& key);

LoadResult ReadKey(const std::string& ref);
LoadResult ReadLegacyFixedKey();
std::string LoadKey(const std::string& ref);
bool StageKey(const std::string& ref, const std::string& key);
bool DeleteKey(const std::string& ref);
std::string LoadLegacyFixedKey();
bool DeleteLegacyFixedKey();

} // namespace credentials
