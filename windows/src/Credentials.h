#pragma once

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
};

void SetStoreBackendForTesting(StoreBackend* backend);
StoreBackend* GetStoreBackendForTesting();

// Existing production facade, unchanged when no test backend is installed.
std::string LoadCloudApiKey();
bool SaveCloudApiKey(const std::string& key);

// RED compile shells: valid refs dispatch only to an injected test backend.
// Without one, these NEW operations fail closed; no live versioned WinCred API.
// LoadKey's empty result cannot yet distinguish NotFound from Failure. A typed
// result and real target conversion belong to the implementation phase after RED.
std::string LoadKey(const std::string& ref);
bool StageKey(const std::string& ref, const std::string& key);
bool DeleteKey(const std::string& ref);
std::string LoadLegacyFixedKey();
bool DeleteLegacyFixedKey();

} // namespace credentials
