#include "TestCredentialsDouble.h"
#include "../src/Credentials.h"
#include <atomic>

namespace {
std::atomic<size_t> g_unexpectedLoadCalls{0};
std::atomic<size_t> g_unexpectedSaveCalls{0};
}

namespace test_credentials {

size_t UnexpectedLoadCalls() {
    return g_unexpectedLoadCalls.load();
}

size_t UnexpectedSaveCalls() {
    return g_unexpectedSaveCalls.load();
}

void ResetCounters() {
    g_unexpectedLoadCalls.store(0);
    g_unexpectedSaveCalls.store(0);
}

} // namespace test_credentials

namespace credentials {

void SetStoreBackendForTesting(StoreBackend*) {
    g_unexpectedSaveCalls.fetch_add(1);
}

StoreBackend* GetStoreBackendForTesting() {
    g_unexpectedLoadCalls.fetch_add(1);
    return nullptr;
}

std::string LoadCloudApiKey() {
    g_unexpectedLoadCalls.fetch_add(1);
    return {};
}

bool SaveCloudApiKey(const std::string&) {
    g_unexpectedSaveCalls.fetch_add(1);
    return false;
}

std::string LoadKey(const std::string&) {
    g_unexpectedLoadCalls.fetch_add(1);
    return {};
}

bool StageKey(const std::string&, const std::string&) {
    g_unexpectedSaveCalls.fetch_add(1);
    return false;
}

bool DeleteKey(const std::string&) {
    g_unexpectedSaveCalls.fetch_add(1);
    return false;
}

std::string LoadLegacyFixedKey() {
    g_unexpectedLoadCalls.fetch_add(1);
    return {};
}

bool DeleteLegacyFixedKey() {
    g_unexpectedSaveCalls.fetch_add(1);
    return false;
}

} // namespace credentials
