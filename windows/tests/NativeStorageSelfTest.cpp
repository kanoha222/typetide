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

const char* GetValidSchemaV2Json() {
    return "{\n"
           "  \"schemaVersion\": 2,\n"
           "  \"profiles\": [\n"
           "    {\n"
           "      \"id\": \"profile-read-01\",\n"
           "      \"name\": \"General Reader\",\n"
           "      \"nativeLanguage\": \"zh\",\n"
           "      \"foreignLanguage\": \"en\",\n"
           "      \"direction\": \"auto\",\n"
           "      \"promptMode\": \"preset\",\n"
           "      \"presetStyle\": \"faithful\",\n"
           "      \"systemPrompt\": \"Translate {{source_language}} to {{target_language}} faithfully.\",\n"
           "      \"backend\": \"ollama\",\n"
           "      \"model\": \"qwen2.5:3b\"\n"
           "    },\n"
           "    {\n"
           "      \"id\": \"profile-write-02\",\n"
           "      \"name\": \"Academic Polisher\",\n"
           "      \"nativeLanguage\": \"en\",\n"
           "      \"foreignLanguage\": \"zh\",\n"
           "      \"direction\": \"auto\",\n"
           "      \"promptMode\": \"custom\",\n"
           "      \"presetStyle\": \"faithful\",\n"
           "      \"systemPrompt\": \"Polish academic phrasing from {{source_language}} to {{target_language}}.\",\n"
           "      \"backend\": \"openai\",\n"
           "      \"model\": \"gpt-5-mini\"\n"
           "    }\n"
           "  ],\n"
           "  \"shortcut1ProfileId\": \"profile-read-01\",\n"
           "  \"shortcut2ProfileId\": \"profile-write-02\",\n"
           "  \"selectionProfileId\": \"profile-read-01\"\n"
           "}\n";
}

void TestSchemaV2Preservation() {
    ScopedStorageFixture fixture(L"schema-v2");
    const std::string original = GetValidSchemaV2Json();
    bool originalOk = false;
    json::Value originalRoot = json::Parse(original, &originalOk);
    if (!originalOk || !originalRoot.isObject()) {
        throw InfraException(214, 0);
    }
    if (!WriteFileBinary(fixture.SettingsFile(), original)) {
        throw InfraException(215, GetLastError());
    }

    Settings s;
    s.load();
    s.enabled = !s.enabled;
    const bool saveOk = s.save();

    const FileReadResult after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable, "schema v2 file must remain readable after save attempt");
    if (!saveOk) {
        Check(after.data == original,
              "schema v2 bytes must remain strictly unchanged when save is rejected");
    } else {
        bool jsonOk = false;
        json::Value root = json::Parse(after.data, &jsonOk);
        Check(jsonOk && root.isObject(), "saved schema v2 is valid JSON object");
        Check(root["schemaVersion"].asInt() == 2, "saved schemaVersion is 2");
        Check(root["profiles"].dump() == originalRoot["profiles"].dump(),
              "saved schema v2 preserves full profiles array semantics");
        Check(root["shortcut1ProfileId"].asString() == "profile-read-01",
              "saved shortcut1 binding preserved");
        Check(root["shortcut2ProfileId"].asString() == "profile-write-02",
              "saved shortcut2 binding preserved");
        Check(root["selectionProfileId"].asString() == "profile-read-01",
              "saved selection binding preserved");
    }

    if (!fixture.Finalize()) throw InfraException(216, GetLastError());
}

void TestInvalidSchemaVersionPreservation() {
    struct SchemaCase {
        const char* label;
        const char* jsonContent;
    };
    const SchemaCase cases[] = {
        {"huge-finite", "{\"schemaVersion\": 1e100, \"enabled\": true}"},
        {"non-integral", "{\"schemaVersion\": 2.5, \"enabled\": true}"},
        {"negative", "{\"schemaVersion\": -1, \"enabled\": true}"},
        {"zero", "{\"schemaVersion\": 0, \"enabled\": true}"},
        {"string", "{\"schemaVersion\": \"2\", \"enabled\": true}"},
        {"bool", "{\"schemaVersion\": true, \"enabled\": true}"},
        {"null", "{\"schemaVersion\": null, \"enabled\": true}"},
    };
    for (const auto& c : cases) {
        ScopedStorageFixture fixture(L"schema-invalid");
        if (!WriteFileBinary(fixture.SettingsFile(), c.jsonContent)) {
            throw InfraException(220, GetLastError());
        }
        Settings s;
        s.load();
        s.enabled = !s.enabled;
        const bool saveOk = s.save();

        Check(!saveOk, (std::string("schemaVersion ") + c.label + " save rejected").c_str());
        const FileReadResult after = ReadFileBinary(fixture.SettingsFile());
        Check(after.readable && after.data == c.jsonContent,
              (std::string("schemaVersion ") + c.label + " bytes preserved").c_str());
        if (!fixture.Finalize()) throw InfraException(221, GetLastError());
    }
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
        TestSchemaV2Preservation();
        TestInvalidSchemaVersionPreservation();
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
