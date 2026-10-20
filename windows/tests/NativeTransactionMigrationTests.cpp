#include "NativeTransactionSupport.h"
#include "../src/TranslationProfileStore.h"
#include <utility>

namespace transaction_test {
namespace {

void TestV2NoRef(ScopedStorageFixture& fixture, FakeCredentialStore& store, bool includeEmptyRef) {
    store.legacyFixedKey = "unrelated-fixed-key-do-not-read";
    const Settings candidate = CreateValidCandidateSettings();
    auto object = profiles::store::Encode(candidate.profileConfig);
    object["backend"] = "openai";
    object["openAIBaseURL"] = candidate.openAIBaseURL;
    object["openAIModel"] = candidate.openAIModel;
    if (includeEmptyRef) object["openAICredentialRef"] = "";
    const json::Value root(std::move(object));
    const auto decoded = profiles::store::Decode(root);
    if (!Check(decoded.state == ProfileLoadState::Current &&
               decoded.configuration == candidate.profileConfig,
               "NoKey fixture has VALID complete v2 profiles and all bindings")) return;
    const std::string original = root.dump();
    if (!WriteFileBinary(fixture.SettingsFile(), original)) throw InfraException(307, GetLastError());
    Settings receiver;
    receiver.load();
    Check(receiver.storageState() == StorageState::Ready, "v2 missing/empty ref loads Ready");
    Check(receiver.openAIKey.empty() && receiver.credentialRef().empty(),
          "v2 missing/empty ref is NoKey, never an unrelated fixed key");
    Check(receiver.profileConfig == candidate.profileConfig, "NoKey v2 preserves all profiles/bindings");
    Check(store.loadLegacyCalls == 0 && store.loadKeyCalls == 0 && store.events.empty(),
          "v2 missing/empty ref makes ZERO legacy AND versioned reads");
    Check(store.legacyFixedKey == "unrelated-fixed-key-do-not-read", "unrelated fixed record remains intact");
    const auto after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data == original, "NoKey v2 load leaves exact file bytes unchanged");
}

void TestV2MissingRef(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    TestV2NoRef(fixture, store, false);
}

void TestV2EmptyRef(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    TestV2NoRef(fixture, store, true);
}

std::string LegacyJson(bool inlineKey) {
    json::Object root;
    root["enabled"] = true;
    root["backend"] = "openai";
    root["openAIBaseURL"] = "https://legacy.invalid/v1";
    root["openAIModel"] = "legacy-model";
    root["nativeLanguage"] = "zh";
    root["foreignLanguage"] = "en";
    root["readShortcutAction"] = "smartPopup";
    root["rewriteShortcutAction"] = "smartReplace";
    if (inlineKey) root["openAIKey"] = "legacy-inline-key";
    return json::Value(std::move(root)).dump();
}

ProfileConfiguration LoadLegacyFixture(Settings& receiver, const ScopedStorageFixture& fixture,
                                       const std::string& original) {
    if (!WriteFileBinary(fixture.SettingsFile(), original)) throw InfraException(310, GetLastError());
    bool parsed = false;
    const auto root = json::Parse(original, &parsed);
    const auto decoded = profiles::store::Decode(root);
    Check(parsed && decoded.state == ProfileLoadState::MigratedLegacy &&
          profiles::Validate(decoded.configuration).ok, "legacy fixture decodes to valid migrated profiles");
    receiver.load();
    Check(receiver.profileConfig == decoded.configuration,
          "legacy load materializes migrated profiles and action-derived bindings");
    return decoded.configuration;
}

void CheckMigratedRecord(const Settings& receiver, const ProfileConfiguration& expectedProfiles,
                         const std::string& expectedKey, const ScopedStorageFixture& fixture,
                         FakeCredentialStore& store) {
    const std::string ref = receiver.credentialRef();
    Check(credentials::IsValidRef(ref), "migration ref is a NONEMPTY 1..64 ASCII opaque suffix, not a target");
    const auto record = store.entries.find(ref);
    Check(record != store.entries.end() && record->second == expectedKey && receiver.openAIKey == expectedKey,
          "opaque ref maps to the EXACT selected source key in fake storage and runtime");
    Check(receiver.profileConfig == expectedProfiles, "migration publishes the decoded legacy profiles/bindings");
    CheckV2File(receiver, fixture);
    const auto file = ReadFileBinary(fixture.SettingsFile());
    Check(file.readable && file.data.find(expectedKey) == std::string::npos,
          "successful migration writes v2 profiles/ref but no plaintext key");
    const size_t legacyReads = store.loadLegacyCalls;
    const size_t stages = store.stageKeyCalls;
    Settings reloaded;
    reloaded.load();
    Check(reloaded.storageState() == StorageState::Ready && reloaded.openAIKey == expectedKey &&
          reloaded.credentialRef() == ref && reloaded.profileConfig == expectedProfiles &&
          reloaded.openAIBaseURL == receiver.openAIBaseURL && reloaded.openAIModel == receiver.openAIModel,
          "v2 reload consistently resolves the migrated ref/key/profiles/connections");
    Check(store.loadLegacyCalls == legacyReads && store.stageKeyCalls == stages,
          "v2 reload neither falls back to fixed credentials nor re-stages the key");
}

void TestLegacyInlinePriority(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    store.legacyFixedKey = "unrelated-fixed-key";
    Settings receiver;
    const std::string original = LegacyJson(true);
    const auto expected = LoadLegacyFixture(receiver, fixture, original);
    Check(receiver.openAIKey == "legacy-inline-key", "inline legacy key takes priority over unrelated fixed record");
    Check(store.loadLegacyCalls == 0 && store.saveLegacyCalls == 0 && store.deleteLegacyCalls == 0,
          "inline-key load NEVER reads/writes/deletes the unrelated fixed record");
    const Settings candidate = receiver;
    std::string error;
    if (!Check(receiver.commit(candidate, &error), "inline legacy migration commit succeeds")) {
        Check(store.deleteLegacyCalls == 0 && store.legacyFixedKey == "unrelated-fixed-key",
              "rejected inline migration leaves unrelated fixed storage untouched");
        return;
    }
    CheckMigratedRecord(receiver, expected, "legacy-inline-key", fixture, store);
    Check(store.loadLegacyCalls == 0 && store.saveLegacyCalls == 0 && store.deleteLegacyCalls == 0 &&
          store.legacyFixedKey == "unrelated-fixed-key",
          "inline migration and reload NEVER access the unrelated fixed record");
}

void TestLegacyFixedOriginMigration(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    store.legacyFixedKey = "known-legacy-fixed-key";
    store.entries.emplace("unrelated-ref", "unrelated-versioned-key");
    Settings receiver;
    const auto expected = LoadLegacyFixture(receiver, fixture, LegacyJson(false));
    Check(store.loadLegacyCalls > 0 && receiver.openAIKey == "known-legacy-fixed-key",
          "legacy WITHOUT inline key may resolve the known fixed origin");
    const Settings candidate = receiver;
    std::string error;
    if (!Check(receiver.commit(candidate, &error), "known-fixed-origin migration commit succeeds")) {
        Check(store.deleteLegacyCalls == 0 && store.legacyFixedKey == "known-legacy-fixed-key",
              "NotReady/rejected migration does not delete its known fixed source");
        return;
    }
    CheckMigratedRecord(receiver, expected, "known-legacy-fixed-key", fixture, store);
    Check(store.deleteLegacyCalls == 1 && store.legacyFixedKey.empty(),
          "successful persisted migration deletes its known fixed source exactly once");
    const auto unrelated = store.entries.find("unrelated-ref");
    Check(unrelated != store.entries.end() && unrelated->second == "unrelated-versioned-key",
          "fixed-origin migration retains unrelated versioned records");
    const size_t staged = FindEvent(store, CredentialOperation::Stage, 0);
    const size_t removed = FindEvent(store, CredentialOperation::DeleteLegacy, 0);
    Check(staged < removed && removed < store.events.size() && store.events[staged].succeeded,
          "fixed-source deletion follows successful staging, never the other way around");
}

void TestLegacyFixedStageFailure(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    store.legacyFixedKey = "known-legacy-fixed-key";
    store.simulateStageFailure = true;
    Settings receiver;
    const std::string original = LegacyJson(false);
    LoadLegacyFixture(receiver, fixture, original);
    const Settings before = receiver;
    const Settings candidate = receiver;
    std::string error;
    Check(!receiver.commit(candidate, &error), "failed fixed-source staging rejects migration commit");
    Check(store.loadLegacyCalls > 0 && store.stageKeyCalls > 0,
          "fixed-source failure case actually reads the source and attempts staging");
    Check(store.deleteLegacyCalls == 0 && store.legacyFixedKey == "known-legacy-fixed-key" &&
          store.entries.empty(), "failed migration preserves fixed source and publishes no versioned record");
    CheckUnchanged(before, receiver, fixture, {true, original});
}

void TestLegacyFixedRenameFailure(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    store.legacyFixedKey = "known-legacy-fixed-key";
    // If load auto-migrates, first reject that attempt so a real legacy
    // predecessor remains for the explicit locked transaction below.
    store.simulateStageFailure = true;
    Settings receiver;
    const std::string original = LegacyJson(false);
    LoadLegacyFixture(receiver, fixture, original);
    const auto predecessor = ReadFileBinary(fixture.SettingsFile());
    const bool fileReady = Check(predecessor.readable && predecessor.data == original,
                                 "failed automatic migration preserves the legacy predecessor");
    const bool sourceReady = Check(receiver.openAIKey == "known-legacy-fixed-key" &&
                                   store.deleteLegacyCalls == 0,
                                   "known fixed origin remains available before locked commit");
    if (!fileReady || !sourceReady) return;
    const Settings before = receiver;
    const auto entries = store.entries;
    const size_t stages = store.stageKeyCalls;
    const size_t eventStart = store.events.size();
    store.simulateStageFailure = false; // Preserve ALL counters/events across attempts.
    ScopedFileLock lock;
    if (!lock.Acquire(fixture.SettingsFile())) throw InfraException(316, GetLastError());
    std::string error;
    const bool committed = receiver.commit(before, &error);
    if (!lock.CloseChecked()) throw InfraException(317, GetLastError());
    Check(!committed, "locked legacy predecessor rejects migration replacement");
    const size_t staged = FindEvent(store, CredentialOperation::Stage, eventStart);
    const bool stagedOk = staged < store.events.size() && store.events[staged].succeeded &&
                          credentials::IsValidRef(store.events[staged].ref);
    Check(store.stageKeyCalls == stages + 1 && stagedOk,
          "fixed-origin rename failure follows a successful new credential stage");
    if (stagedOk) {
        Check(HasEvent(store, CredentialOperation::Delete, store.events[staged].ref, staged + 1),
              "failed fixed-origin rename rolls back the successfully staged new ref");
    }
    Check(store.deleteLegacyCalls == 0 && store.legacyFixedKey == "known-legacy-fixed-key" &&
          store.entries == entries, "failed atomic migration retains fixed source and old versioned records");
    CheckUnchanged(before, receiver, fixture, predecessor);
}

} // namespace

void RunMigrationCases() {
    RunCase(L"v2-missing-ref", TestV2MissingRef);
    RunCase(L"v2-empty-ref", TestV2EmptyRef);
    RunCase(L"legacy-inline", TestLegacyInlinePriority);
    RunCase(L"legacy-fixed", TestLegacyFixedOriginMigration);
    RunCase(L"legacy-stage-fail", TestLegacyFixedStageFailure);
    RunCase(L"legacy-rename-fail", TestLegacyFixedRenameFailure);
}

} // namespace transaction_test
