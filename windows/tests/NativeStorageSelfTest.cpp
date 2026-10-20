#include "NativeStorageFixture.h"
#include "TestCredentialsDouble.h"
#include "../src/Json.h"
#include "../src/Settings.h"
#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const char* name) {
    if (!condition) ++g_failures;
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", name);
}

void TestFutureSchemaPreservation() {
    ScopedStorageFixture fixture(L"future-schema");
    const std::string original =
        "{\n"
        "  \"schemaVersion\": 3,\n"
        "  \"enabled\": true,\n"
        "  \"futureExtension\": {\n"
        "    \"engine\": \"experimental-v3\",\n"
        "    \"priority\": 42\n"
        "  }\n"
        "}\n";
    if (!WriteFileBinary(fixture.SettingsFile(), original)) {
        throw InfraException(201, GetLastError());
    }

    Settings s;
    s.load();
    s.enabled = !s.enabled;
    const bool saveOk = s.save();

    Check(!saveOk, "future schema v3 save must be rejected");
    const FileReadResult after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data == original,
          "future schema v3 bytes must remain strictly unchanged");
    if (!fixture.Finalize()) throw InfraException(202, GetLastError());
}

void TestCorruptJsonPreservation() {
    ScopedStorageFixture fixture(L"corrupt-json");
    const std::string original =
        "{\n"
        "  \"enabled\": true,\n"
        "  \"backend\": \"openai\",\n"
        "  \"unclosed\": {";
    if (!WriteFileBinary(fixture.SettingsFile(), original)) {
        throw InfraException(203, GetLastError());
    }

    Settings s;
    s.load();
    s.enabled = !s.enabled;
    const bool saveOk = s.save();

    Check(!saveOk, "corrupt JSON save must be rejected");
    const FileReadResult after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data == original,
          "corrupt JSON bytes must remain strictly unchanged");
    if (!fixture.Finalize()) throw InfraException(204, GetLastError());
}

void TestZeroByteFilePreservation() {
    ScopedStorageFixture fixture(L"zero-byte");
    const std::string original = "";
    if (!WriteFileBinary(fixture.SettingsFile(), original)) {
        throw InfraException(205, GetLastError());
    }

    Settings s;
    s.load();
    s.enabled = !s.enabled;
    const bool saveOk = s.save();

    Check(!saveOk, "zero-byte file save must be rejected");
    const FileReadResult after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data.empty(),
          "zero-byte file must remain readable and strictly zero bytes");
    if (!fixture.Finalize()) throw InfraException(206, GetLastError());
}

void TestOversizedFilePreservation() {
    ScopedStorageFixture fixture(L"oversized");
    constexpr size_t targetSize = 1024 * 1024 + 16;
    std::string original = "{\"large\":\"";
    original.reserve(targetSize + 16);
    while (original.size() + 3 < targetSize) {
        original.push_back('A');
    }
    original += "\"}";
    if (!WriteFileBinary(fixture.SettingsFile(), original)) {
        throw InfraException(207, GetLastError());
    }

    Settings s;
    s.load();
    s.enabled = !s.enabled;
    const bool saveOk = s.save();

    Check(!saveOk, "oversized file save must be rejected");
    const FileReadResult after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data.size() == original.size() && after.data == original,
          "oversized file must remain readable and content strictly unchanged");
    if (!fixture.Finalize()) throw InfraException(208, GetLastError());
}

void TestReadLockIOErrorPreservation() {
    ScopedStorageFixture fixture(L"read-lock-io");
    const std::string original =
        "{\n"
        "  \"enabled\": true,\n"
        "  \"backend\": \"builtin\"\n"
        "}\n";
    if (!WriteFileBinary(fixture.SettingsFile(), original)) {
        throw InfraException(209, GetLastError());
    }

    Settings s;
    ScopedFileLock lock;
    if (!lock.Acquire(fixture.SettingsFile())) {
        throw InfraException(210, GetLastError());
    }

    s.load(); // Must fail to open due to exclusive lock
    if (!lock.CloseChecked()) {
        throw InfraException(211, GetLastError());
    }

    s.enabled = !s.enabled;
    const bool saveOk = s.save();

    Check(!saveOk, "save must be rejected after load encountered read IO error");
    const FileReadResult after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data == original,
          "file bytes must remain strictly unchanged after read IO error");
    if (!fixture.Finalize()) throw InfraException(212, GetLastError());
}

void TestMissingFileFreshInstallControl() {
    ScopedStorageFixture fixture(L"missing-file");

    Settings s;
    s.load(); // Missing file must be treated as clean fresh install
    s.hasCompletedFirstLaunch = true;
    const bool saveOk = s.save();

    Check(saveOk, "missing file fresh install must allow initial save");
    const FileReadResult after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && !after.data.empty(),
          "fresh install save must create non-empty settings file");

    bool jsonOk = false;
    json::Value root = json::Parse(after.data, &jsonOk);
    Check(jsonOk && root.isObject(), "fresh install output is valid JSON object");
    Check(root["hasCompletedFirstLaunch"].asBool() == true,
          "fresh install sets hasCompletedFirstLaunch to true");

    if (!fixture.Finalize()) throw InfraException(213, GetLastError());
}

} // namespace

int main() {
    test_credentials::ResetCounters();
    std::printf("[native settings storage self-test]\n");

    try {
        TestFutureSchemaPreservation();
        TestCorruptJsonPreservation();
        TestZeroByteFilePreservation();
        TestOversizedFilePreservation();
        TestReadLockIOErrorPreservation();
        TestMissingFileFreshInstallControl();
    } catch (const InfraException& ex) {
        std::fprintf(stderr, "[infra error step %d] win32=%lu\n", ex.stepCode, ex.win32Error);
        return 2;
    } catch (...) {
        std::fprintf(stderr, "[infra error unknown]\n");
        return 2;
    }

    Check(test_credentials::UnexpectedLoadCalls() == 0,
          "no unexpected credential load calls (env isolation intact)");
    Check(test_credentials::UnexpectedSaveCalls() == 0,
          "no unexpected credential save calls (env isolation intact)");

    std::printf("[native storage] %d failure(s)\n", g_failures);
    return g_failures;
}
