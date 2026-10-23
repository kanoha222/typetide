#include "NativeTransactionSupport.h"
#include "../src/TranslationProfileStore.h"
#include <cstdio>

namespace transaction_test {
namespace {
int g_failures = 0;
}

bool Check(bool condition, const char* name) {
    if (!condition) ++g_failures;
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", name);
    return condition;
}

int FailureCount() {
    return g_failures;
}

void RunCase(const wchar_t* name, TestCase test) {
    ScopedStorageFixture fixture(name);
    FakeCredentialStore store;
    ScopedCredentialStore installed(store);
    test(fixture, store);
    // A test may return after a behavioral seed failure. Still check cleanup,
    // restore the prior backend via RAII, and run the next independent case.
    if (!fixture.Finalize()) throw InfraException(390, GetLastError());
}

Settings CreateValidCandidateSettings() {
    Settings settings;
    settings.backend = TranslationBackend::OpenAI;
    settings.openAIKey = "candidate-secret-token-123";
    TranslationProfile reader;
    reader.id = "p-read";
    reader.name = "Reader Profile";
    reader.systemPrompt = "Translate faithfully.";
    reader.backend = TranslationBackend::OpenAI;
    reader.model = settings.openAIModel;
    TranslationProfile writer = reader;
    writer.id = "p-write";
    writer.name = "Writer Profile";
    settings.profileConfig = {{reader, writer}, {reader.id, writer.id, reader.id}};
    return settings;
}

bool SameRuntime(const Settings& a, const Settings& b) {
    return a.enabled == b.enabled && a.backend == b.backend &&
           a.ollamaHost == b.ollamaHost && a.ollamaPort == b.ollamaPort &&
           a.ollamaModel == b.ollamaModel && a.openAIBaseURL == b.openAIBaseURL &&
           a.openAIKey == b.openAIKey && a.openAIModel == b.openAIModel &&
           a.nativeLanguage == b.nativeLanguage && a.foreignLanguage == b.foreignLanguage &&
           a.readDirection == b.readDirection && a.rewriteDirection == b.rewriteDirection &&
           a.readShortcut == b.readShortcut && a.rewriteShortcut == b.rewriteShortcut &&
           a.readShortcutAction == b.readShortcutAction &&
           a.rewriteShortcutAction == b.rewriteShortcutAction &&
           a.selectionTrigger == b.selectionTrigger && a.rewritePreview == b.rewritePreview &&
           a.readStyle == b.readStyle && a.rewriteStyle == b.rewriteStyle &&
           a.skipApps == b.skipApps && a.hasCompletedFirstLaunch == b.hasCompletedFirstLaunch &&
           a.lastUpdateCheck == b.lastUpdateCheck && a.profileConfig == b.profileConfig &&
           a.storageState() == b.storageState() && a.storageError() == b.storageError() &&
           a.connectionEpoch() == b.connectionEpoch() && a.credentialRef() == b.credentialRef();
}

void CheckNoTemporaryFile(const ScopedStorageFixture& fixture) {
    const std::wstring temporary = fixture.SettingsFile() + L".tmp";
    const DWORD attributes = GetFileAttributesW(temporary.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            throw InfraException(391, error);
        }
    }
    Check(attributes == INVALID_FILE_ATTRIBUTES, "no settings temporary file remains");
}

void CheckUnchanged(const Settings& before, const Settings& after,
                    const ScopedStorageFixture& fixture, const FileReadResult& original) {
    Check(SameRuntime(before, after),
          "complete runtime/profile/connection/key/ref/storage/epoch unchanged");
    const auto current = ReadFileBinary(fixture.SettingsFile());
    Check(current.readable == original.readable && current.data == original.data,
          "predecessor file presence and exact bytes unchanged");
    CheckNoTemporaryFile(fixture);
}

json::Value CheckV2File(const Settings& settings, const ScopedStorageFixture& fixture) {
    const auto file = ReadFileBinary(fixture.SettingsFile());
    if (!Check(file.readable && !file.data.empty(), "committed settings file is readable")) return {};
    bool parsed = false;
    const auto root = json::Parse(file.data, &parsed);
    if (!Check(parsed && root.isObject(), "committed settings file is a JSON object")) return {};
    Check(root["schemaVersion"].asInt() == 2, "committed schema is v2");
    const auto decoded = profiles::store::Decode(root);
    Check(decoded.state == ProfileLoadState::Current &&
          decoded.configuration == settings.profileConfig,
          "disk v2 profiles and all three bindings match committed runtime");
    Check(root["openAIKey"].isNull(), "disk has no plaintext openAIKey field");
    if (!settings.openAIKey.empty()) {
        Check(file.data.find(settings.openAIKey) == std::string::npos,
              "disk contains no plaintext secret bytes");
        Check(credentials::IsValidRef(settings.credentialRef()) &&
              root["openAICredentialRef"].asString() == settings.credentialRef(),
              "disk has the nonempty opaque committed credential ref");
    } else {
        Check(root["openAICredentialRef"].isNull() ||
              (root["openAICredentialRef"].isString() &&
               root["openAICredentialRef"].asString().empty()),
              "disk explicitly represents NoKey without a stale ref");
    }
    return root;
}

bool SeedCommitted(Settings& receiver, const Settings& candidate,
                   const ScopedStorageFixture& fixture, const FakeCredentialStore& store) {
    const uint64_t priorEpoch = receiver.connectionEpoch();
    if (!Check(profiles::Validate(candidate.profileConfig).ok,
               "seed profiles are structurally valid")) return false;
    std::string error;
    if (!Check(receiver.commit(candidate, &error), "seed commit succeeds")) return false;
    const auto file = ReadFileBinary(fixture.SettingsFile());
    const bool fileOk = Check(file.readable && !file.data.empty(), "seed persisted predecessor exists");
    const bool refOk = Check(credentials::IsValidRef(receiver.credentialRef()),
                             "seed ref is 1..64 ASCII alphanumeric/underscore/hyphen");
    const auto entry = store.entries.find(receiver.credentialRef());
    const bool keyOk = Check(!candidate.openAIKey.empty() &&
                             receiver.openAIKey == candidate.openAIKey &&
                             entry != store.entries.end() && entry->second == candidate.openAIKey,
                             "seed runtime key and referenced fake record equal the candidate key");
    const bool runtimeOk = Check(receiver.profileConfig == candidate.profileConfig &&
                                 receiver.backend == candidate.backend &&
                                 receiver.ollamaHost == candidate.ollamaHost &&
                                 receiver.ollamaPort == candidate.ollamaPort &&
                                 receiver.ollamaModel == candidate.ollamaModel &&
                                 receiver.openAIBaseURL == candidate.openAIBaseURL &&
                                 receiver.openAIModel == candidate.openAIModel &&
                                 receiver.storageState() == StorageState::Ready,
                                 "seed committed profiles/connections are published and Ready");
    const bool epochOk = Check(receiver.connectionEpoch() > priorEpoch,
                               "seed connection/key change advances runtime epoch");
    CheckV2File(receiver, fixture);
    return fileOk && refOk && keyOk && runtimeOk && epochOk;
}

size_t FindEvent(const FakeCredentialStore& store, CredentialOperation operation, size_t begin) {
    for (size_t index = begin; index < store.events.size(); ++index) {
        if (store.events[index].operation == operation) return index;
    }
    return store.events.size();
}

bool HasEvent(const FakeCredentialStore& store, CredentialOperation operation,
              const std::string& ref, size_t begin) {
    for (size_t index = begin; index < store.events.size(); ++index) {
        const auto& event = store.events[index];
        if (event.operation == operation && event.ref == ref && event.succeeded) return true;
    }
    return false;
}

} // namespace transaction_test
