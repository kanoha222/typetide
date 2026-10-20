#include "NativeTransactionSupport.h"
#include "../src/TranslationProfileStore.h"
#include <cstdio>
#include <utility>
#include <vector>

namespace transaction_test {
void RunMigrationCases();
}

namespace {
using namespace transaction_test;

void TestReferenceGuards(ScopedStorageFixture&, FakeCredentialStore& store) {
    Check(credentials::IsValidRef("a") && credentials::IsValidRef(std::string(64, 'Z')) &&
          credentials::IsValidRef("Ab09_-"), "opaque ref accepts exact ASCII bounds");
    const std::vector<std::string> invalid = {
        "", std::string(65, 'x'), "TypeTide/OpenAICompatibleApiKey/ref", "a/b", "a\\b",
        "a b", "a.b", "a:b", std::string("a\0b", 3), "\xc3\xa9"
    };
    for (const auto& ref : invalid) {
        Check(!credentials::IsValidRef(ref), "invalid opaque ref rejected by pure guard");
        Check(credentials::LoadKey(ref).empty(), "invalid ref cannot load a record");
        Check(!credentials::StageKey(ref, "synthetic-key"), "invalid ref cannot stage a record");
        Check(!credentials::DeleteKey(ref), "invalid ref cannot delete a record");
    }
    Check(!credentials::StageKey("valid-ref", ""), "empty key cannot be staged");
    Check(store.events.empty(), "all invalid refs rejected BEFORE fake backend dispatch");
    Check(credentials::StageKey("valid-ref", "synthetic-key"), "valid ref dispatches to fake stage");
    Check(credentials::LoadKey("valid-ref") == "synthetic-key", "fake versioned record roundtrip");
    Check(credentials::DeleteKey("valid-ref"), "valid ref dispatches to fake delete");
}

void TestBackendLifetimeAndLegacyFacade(ScopedStorageFixture&, FakeCredentialStore& store) {
    auto* prior = credentials::GetStoreBackendForTesting();
    FakeCredentialStore nested;
    bool caught = false;
    try {
        ScopedCredentialStore installed(nested);
        Check(credentials::GetStoreBackendForTesting() == &nested, "nested backend is installed");
        throw 7;
    } catch (int) {
        caught = true;
    }
    Check(caught && credentials::GetStoreBackendForTesting() == prior,
          "backend RAII restores prior installation after an exception");
    Check(credentials::SaveCloudApiKey("fixed-only-key"), "legacy facade saves a fixed record");
    Check(credentials::LoadCloudApiKey() == "fixed-only-key" &&
          credentials::LoadLegacyFixedKey() == "fixed-only-key",
          "both legacy read facades use the same fixed record");
    Check(store.entries.empty() && store.stageKeyCalls == 0,
          "legacy save is NOT a versioned stage with ref named legacy");
    Check(credentials::SaveCloudApiKey("") && store.legacyFixedKey.empty(),
          "legacy facade clear deletes only the fixed record");
}

void TestStageFailureRollback(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    receiver.load();
    const Settings before = receiver;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    store.simulateStageFailure = true;
    const Settings candidate = CreateValidCandidateSettings();
    std::string error;
    Check(!receiver.commit(candidate, &error), "stage failure rejects the commit");
    Check(store.stageKeyCalls == 1, "stage-failure case actually attempts credential staging");
    const size_t staged = FindEvent(store, CredentialOperation::Stage, 0);
    Check(staged < store.events.size() && !store.events[staged].succeeded &&
          credentials::IsValidRef(store.events[staged].ref),
          "ordered fake trace observes a failed stage with a valid opaque ref");
    Check(store.entries.empty(), "failed stage leaves no persisted fake record");
    Check(store.loadLegacyCalls == 0 && store.deleteLegacyCalls == 0,
          "failed fresh transaction never touches unrelated legacy storage");
    CheckUnchanged(before, receiver, fixture, original);
}

void CheckRenameStaging(const FakeCredentialStore& store, size_t eventStart,
                        const std::string& oldRef) {
    const size_t staged = FindEvent(store, CredentialOperation::Stage, eventStart);
    const bool stagedOk = staged < store.events.size() && store.events[staged].succeeded &&
                          credentials::IsValidRef(store.events[staged].ref) &&
                          store.events[staged].ref != oldRef;
    Check(stagedOk, "rename-failure trace observes a successful NEW credential stage");
    if (!stagedOk) return;
    const auto& newRef = store.events[staged].ref;
    Check(HasEvent(store, CredentialOperation::Delete, newRef, staged + 1),
          "rename failure deletes that staged ref AFTER its successful stage");
    Check(store.entries.find(newRef) == store.entries.end(), "staged new ref is absent after rollback");
    Check(!HasEvent(store, CredentialOperation::Delete, oldRef, eventStart),
          "rename rollback never deletes the predecessor credential ref");
}

void TestRenameFailureRollback(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    if (!SeedCommitted(receiver, CreateValidCandidateSettings(), fixture, store)) return;
    const Settings before = receiver;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    const auto entries = store.entries;
    const size_t stages = store.stageKeyCalls;
    const size_t eventStart = store.events.size();
    Settings candidate = receiver;
    candidate.openAIKey = "replacement-key-for-rename-failure";
    ScopedFileLock lock;
    // OPEN_EXISTING is intentional: a successful committed predecessor is
    // required above. This is a real exclusive file lock, not a fake rename flag.
    if (!lock.Acquire(fixture.SettingsFile())) throw InfraException(302, GetLastError());
    std::string error;
    const bool committed = receiver.commit(candidate, &error);
    if (!lock.CloseChecked()) throw InfraException(303, GetLastError());
    Check(!committed, "exclusive predecessor lock rejects atomic replacement");
    Check(store.stageKeyCalls == stages + 1, "rename failure happens after one attempted new stage");
    CheckRenameStaging(store, eventStart, before.credentialRef());
    Check(store.entries == entries, "rollback retains predecessor ref and exact old key");
    CheckUnchanged(before, receiver, fixture, original);
}

Settings CreateOversizedValidCandidate() {
    Settings candidate = CreateValidCandidateSettings();
    const TranslationProfile prototype = candidate.profileConfig.profiles.front();
    std::vector<TranslationProfile> profileList;
    constexpr size_t profileCount = 33;
    for (size_t index = 0; index < profileCount; ++index) {
        TranslationProfile profile = prototype;
        profile.id = "large-profile-" + std::to_string(index);
        profile.systemPrompt = std::string(profiles::MaxPromptUtf16, 'X');
        profileList.push_back(std::move(profile));
    }
    candidate.profileConfig = {std::move(profileList),
        {"large-profile-0", "large-profile-1", "large-profile-0"}};
    return candidate;
}

void TestOversizePreCheckNoCredWrite(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    receiver.load();
    const Settings before = receiver;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    const Settings candidate = CreateOversizedValidCandidate();
    const bool valid = Check(profiles::Validate(candidate.profileConfig).ok,
                             "33 individually bounded profiles are structurally VALID");
    const auto encoded = json::Value(profiles::store::Encode(candidate.profileConfig)).dump();
    const bool oversized = Check(encoded.size() > profiles::MaxSettingsBytes,
                                 "actual encoded JSON exceeds 1 MiB (not a per-prompt error)");
    if (!valid || !oversized) return;
    std::string error;
    Check(!receiver.commit(candidate, &error), "whole-file byte limit rejects valid oversize candidate");
    Check(store.stageKeyCalls == 0 && store.events.empty(),
          "serialization size rejection precedes ALL credential access/staging");
    CheckUnchanged(before, receiver, fixture, original);
}

void TestFutureSchemaNoCredentialRead(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    auto root = profiles::store::Encode(CreateValidCandidateSettings().profileConfig);
    root["schemaVersion"] = 3;
    root["openAICredentialRef"] = "future-ref";
    const std::string original = json::Value(std::move(root)).dump();
    if (!WriteFileBinary(fixture.SettingsFile(), original)) throw InfraException(305, GetLastError());
    Settings receiver;
    receiver.load();
    Check(receiver.storageState() == StorageState::UnsupportedSchema,
          "future schema is UnsupportedSchema");
    Check(store.loadKeyCalls == 0 && store.loadLegacyCalls == 0 && store.events.empty(),
          "future schema is rejected before ANY credential dispatch");
    const auto after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data == original, "future schema preserves exact original bytes");
}

void TestFreshInstallReadyNoKey(ScopedStorageFixture&, FakeCredentialStore& store) {
    Settings receiver;
    receiver.load();
    Check(receiver.storageState() == StorageState::Ready, "missing file is fresh Ready");
    Check(receiver.credentialRef().empty() && receiver.openAIKey.empty(), "fresh install is NoKey");
    Check(store.events.empty(), "fresh install makes zero legacy/versioned credential accesses");
}

void TestFreshNoKeyCommit(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    receiver.load();
    const auto epoch = receiver.connectionEpoch();
    Settings candidate = receiver;
    candidate.hasCompletedFirstLaunch = true; // No connection or key change.
    std::string error;
    if (!Check(receiver.commit(candidate, &error), "fresh NoKey metadata commit succeeds")) return;
    Check(receiver.connectionEpoch() == epoch, "NoKey to empty key does NOT advance the epoch");
    Check(receiver.openAIKey.empty() && receiver.credentialRef().empty(), "fresh commit remains NoKey");
    Check(store.events.empty(), "fresh NoKey commit does not read/write/delete credentials");
    CheckV2File(receiver, fixture);
}

void TestSecretPrivacy(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    Settings candidate = CreateValidCandidateSettings();
    candidate.openAIKey = "ultra-secret-test-token-xyz";
    if (!SeedCommitted(receiver, candidate, fixture, store)) return;
    const auto root = CheckV2File(receiver, fixture);
    Check(root["openAICredentialRef"].isString() &&
          root["openAICredentialRef"].asString() == receiver.credentialRef(),
          "privacy assertion requires a successful readable commit with a real ref");
}

void CheckNoKeyReload(const Settings& committed, FakeCredentialStore& store) {
    const size_t legacyReads = store.loadLegacyCalls;
    const size_t versionedReads = store.loadKeyCalls;
    const size_t eventCount = store.events.size();
    Settings reloaded;
    reloaded.load();
    Check(reloaded.storageState() == StorageState::Ready && reloaded.openAIKey.empty() &&
          reloaded.credentialRef().empty() && reloaded.profileConfig == committed.profileConfig,
          "cleared v2 reload is Ready+NoKey with the committed profiles");
    Check(store.loadLegacyCalls == legacyReads && store.loadKeyCalls == versionedReads &&
          store.events.size() == eventCount,
          "NoKey reload never reads/writes/deletes legacy or versioned credentials");
}

void TestExplicitKeyClear(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    store.entries.emplace("unrelated-ref", "unrelated-key");
    store.legacyFixedKey = "unrelated-fixed-key";
    Settings receiver;
    if (!SeedCommitted(receiver, CreateValidCandidateSettings(), fixture, store)) return;
    const auto unrelated = store.entries.find("unrelated-ref");
    Check(receiver.credentialRef() != "unrelated-ref" && unrelated != store.entries.end() &&
          unrelated->second == "unrelated-key", "seed retains the unrelated versioned record");
    const Settings before = receiver;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    const size_t stages = store.stageKeyCalls;
    const size_t deletes = store.deleteKeyCalls;
    const size_t eventStart = store.events.size();
    auto expectedEntries = store.entries;
    expectedEntries.erase(before.credentialRef());
    Settings candidate = receiver;
    candidate.openAIKey.clear();
    std::string error;
    if (!Check(receiver.commit(candidate, &error), "explicit clear of a NONEMPTY committed key succeeds")) {
        CheckUnchanged(before, receiver, fixture, original);
        return;
    }
    Check(receiver.openAIKey.empty() && receiver.credentialRef().empty(), "explicit clear publishes NoKey");
    Check(receiver.connectionEpoch() > before.connectionEpoch(), "real key removal advances the epoch");
    Check(store.stageKeyCalls == stages && store.deleteKeyCalls == deletes + 1 &&
          HasEvent(store, CredentialOperation::Delete, before.credentialRef(), eventStart),
          "clear deletes ONLY the known old ref and does not stage");
    Check(store.entries == expectedEntries && store.legacyFixedKey == "unrelated-fixed-key" &&
          store.deleteLegacyCalls == 0, "clear retains unrelated versioned and fixed records");
    CheckV2File(receiver, fixture);
    const auto cleared = ReadFileBinary(fixture.SettingsFile());
    Check(cleared.readable && cleared.data.find(before.openAIKey) == std::string::npos &&
          cleared.data.find(before.credentialRef()) == std::string::npos,
          "cleared JSON contains neither old plaintext key nor a stale ref");
    CheckNoKeyReload(receiver, store);
}

void TestUnchangedKeyReuseNoCredWrite(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    if (!SeedCommitted(receiver, CreateValidCandidateSettings(), fixture, store)) return;
    const Settings before = receiver;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    const auto entries = store.entries;
    const size_t stages = store.stageKeyCalls;
    const size_t deletes = store.deleteKeyCalls;
    Settings candidate2 = receiver; // Copy the successfully COMMITTED receiver.
    candidate2.profileConfig.profiles[0].name = "Renamed Profile"; // Only this field changes.
    std::string error;
    if (!Check(receiver.commit(candidate2, &error), "second, profile-name-only commit succeeds")) {
        CheckUnchanged(before, receiver, fixture, original);
        return;
    }
    Check(!before.credentialRef().empty() && receiver.credentialRef() == before.credentialRef(),
          "successful second commit reuses the same NONEMPTY ref");
    Check(store.stageKeyCalls == stages && store.deleteKeyCalls == deletes && store.entries == entries,
          "unchanged key reuses its record without redundant stage/delete");
    Check(receiver.connectionEpoch() == before.connectionEpoch(), "profile-only commit keeps epoch stable");
    Check(receiver.profileConfig == candidate2.profileConfig, "second profile change is actually published");
    const auto root = CheckV2File(receiver, fixture);
    const auto saved = ReadFileBinary(fixture.SettingsFile());
    Check(saved.readable && saved.data != original.data &&
          root["profiles"].at(0)["name"].asString() == "Renamed Profile",
          "second commit actually updates the JSON profile name");
}

void TestInvalidCandidateNotPublished(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    const std::string legacy = "{\"enabled\":false,\"backend\":\"openai\","
                               "\"openAIKey\":\"predecessor-inline-key\"}";
    if (!WriteFileBinary(fixture.SettingsFile(), legacy)) throw InfraException(315, GetLastError());
    Settings receiver;
    receiver.load();
    const Settings before = receiver;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    const auto entries = store.entries;
    const size_t eventCount = store.events.size();
    Settings candidate = CreateValidCandidateSettings();
    candidate.enabled = !before.enabled;
    candidate.ollamaHost = "http://candidate.invalid";
    candidate.ollamaPort = 12345;
    candidate.ollamaModel = "candidate-model";
    candidate.openAIBaseURL = "https://candidate.invalid/v1";
    candidate.openAIModel = "candidate-model";
    candidate.skipApps = {L"candidate.exe"};
    candidate.profileConfig.profiles.clear();
    Check(!profiles::Validate(candidate.profileConfig).ok, "invalid candidate really fails structural validation");
    std::string error;
    Check(!receiver.commit(candidate, &error), "invalid candidate is rejected");
    Check(store.events.size() == eventCount && store.entries == entries,
          "invalid candidate does not access or change any credential record");
    CheckUnchanged(before, receiver, fixture, original);
}

void CheckSnapshotMatches(const SettingsSnapshot& snapshot, const Settings& committed) {
    Check(snapshot.profileConfig == committed.profileConfig &&
          snapshot.connectionEpoch == committed.connectionEpoch(),
          "snapshot contains last COMMITTED profiles and epoch, not unsaved live drafts");
    Check(snapshot.backend == committed.backend && snapshot.ollamaHost == committed.ollamaHost &&
          snapshot.ollamaPort == committed.ollamaPort && snapshot.ollamaModel == committed.ollamaModel &&
          snapshot.openAIBaseURL == committed.openAIBaseURL && snapshot.openAIModel == committed.openAIModel &&
          snapshot.openAIKey == committed.openAIKey &&
          snapshot.openAICredentialRef == committed.credentialRef() &&
          snapshot.hasCompletedFirstLaunch == committed.hasCompletedFirstLaunch,
          "snapshot resolves committed endpoint/model/key/ref entirely in memory");
}

void TestSnapshotIsolation(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    if (!SeedCommitted(receiver, CreateValidCandidateSettings(), fixture, store)) return;
    const Settings committed = receiver;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    const size_t eventCount = store.events.size();
    const SettingsSnapshot earlier = receiver.snapshot();
    receiver.openAIBaseURL = "https://unsaved-draft.invalid/v1";
    receiver.openAIKey = "unsaved-draft-key";
    receiver.profileConfig.profiles[0].name = "Unsaved Draft Profile";
    receiver.profileConfig.bindings.shortcut1ProfileId = receiver.profileConfig.profiles[1].id;
    // Deliberately NO save/commit: the public draft is dirty BEFORE this snapshot.
    const SettingsSnapshot afterDraft = receiver.snapshot();
    CheckSnapshotMatches(afterDraft, committed);
    CheckSnapshotMatches(earlier, committed);
    Check(store.events.size() == eventCount, "snapshot creation has no credential I/O");
    const auto after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data == original.data, "unsaved draft and snapshots leave disk unchanged");
}

} // namespace

int main() {
    std::printf("[native settings transaction self-test: RED scaffold]\n");
    try {
        RunCase(L"ref-guards", TestReferenceGuards);
        RunCase(L"backend-lifetime", TestBackendLifetimeAndLegacyFacade);
        RunCase(L"stage-fail", TestStageFailureRollback);
        RunCase(L"rename-fail", TestRenameFailureRollback);
        RunCase(L"oversize-precheck", TestOversizePreCheckNoCredWrite);
        RunCase(L"future-nocred", TestFutureSchemaNoCredentialRead);
        RunCase(L"fresh-nokey", TestFreshInstallReadyNoKey);
        RunCase(L"fresh-nokey-commit", TestFreshNoKeyCommit);
        RunMigrationCases();
        RunCase(L"secret-privacy", TestSecretPrivacy);
        RunCase(L"key-clear", TestExplicitKeyClear);
        RunCase(L"key-reuse", TestUnchangedKeyReuseNoCredWrite);
        RunCase(L"invalid-candidate", TestInvalidCandidateNotPublished);
        RunCase(L"snapshot", TestSnapshotIsolation);
    } catch (const InfraException& exception) {
        std::fprintf(stderr, "[infra error step %d] win32=%lu\n", exception.stepCode, exception.win32Error);
        return 2;
    } catch (...) {
        std::fprintf(stderr, "[infra error unknown]\n");
        return 2;
    }
    std::printf("[native transaction] %d failure(s)\n", FailureCount());
    return FailureCount();
}
