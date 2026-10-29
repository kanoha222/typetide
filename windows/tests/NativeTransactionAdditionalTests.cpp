#include "NativeTransactionSupport.h"
#include "../src/TranslationProfileStore.h"
#include <utility>
#include <vector>

namespace transaction_test {
namespace {

void TestReferencedReadFailure(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    if (!SeedCommitted(receiver, CreateValidCandidateSettings(), fixture, store)) return;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    const auto entries = store.entries;
    const size_t stages = store.stageKeyCalls;
    store.simulateReadFailure = true;
    Settings failed;
    failed.load();
    Check(failed.storageState() == StorageState::IoError && failed.isReadOnly(),
          "referenced credential read ERROR locks storage, not Ready+NoKey");
    const size_t events = store.events.size();
    Check(!failed.save() && !failed.commit(receiver), "credential read error locks ALL writes");
    Check(store.events.size() == events && store.stageKeyCalls == stages && store.entries == entries,
          "locked credential read failure cannot dispatch further credential operations");
    const auto after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data == original.data, "credential read error preserves exact v2 bytes/ref");
    Check(store.loadLegacyCalls == 0, "credential read error never falls back to fixed storage");
}

void TestReferencedNotFound(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    if (!SeedCommitted(receiver, CreateValidCandidateSettings(), fixture, store)) return;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    store.entries.clear(); // Deliberately simulate external removal of the referenced record.
    const auto result = credentials::ReadKey(receiver.credentialRef());
    Check(result.status == credentials::LoadStatus::NotFound, "missing record has distinct NotFound status");
    Settings missing;
    missing.load();
    Check(missing.isReadOnly() && !missing.storageError().empty(),
          "missing referenced record is explicit locked failure, never silent NoKey");
    const size_t events = store.events.size();
    Check(!missing.save() && !missing.commit(receiver), "missing referenced record cannot erase its disk ref");
    Check(store.events.size() == events && store.loadLegacyCalls == 0,
          "missing referenced record has no write/fixed fallback");
    const auto after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data == original.data, "missing record preserves original file and reference");
}

void TestMalformedRefBeforeRead(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    auto object = profiles::store::Encode(CreateValidCandidateSettings().profileConfig);
    object["openAICredentialRef"] = "outside/namespace";
    const auto original = json::Value(std::move(object)).dump();
    if (!WriteFileBinary(fixture.SettingsFile(), original)) throw InfraException(410, GetLastError());
    Settings receiver;
    receiver.load();
    Check(receiver.storageState() == StorageState::Invalid && !receiver.save(),
          "malformed v2 ref locks load and save");
    Check(store.events.empty(), "malformed ref is rejected before ALL backend access");
    const auto after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data == original, "malformed ref file bytes are preserved");
}

void TestGlobalRoundtripAndCapability(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    Settings candidate = CreateValidCandidateSettings();
    candidate.enabled = false;
    candidate.ollamaHost = "http://127.0.0.2";
    candidate.ollamaPort = 12001;
    candidate.ollamaModel = "other-local-model";
    candidate.openAIBaseURL = "https://metadata.invalid/v1";
    candidate.openAIModel = "other-cloud-model";
    candidate.nativeLanguage = Language::French;
    candidate.foreignLanguage = Language::Russian;
    candidate.readDirection = TranslationDirection::ForeignToNative;
    candidate.rewriteDirection = TranslationDirection::NativeToForeign;
    candidate.readShortcut = {MOD_CONTROL | MOD_SHIFT, 'T'};
    candidate.rewriteShortcut = {0, 0};
    candidate.readShortcutAction = ShortcutAction::ForeignPopup;
    candidate.rewriteShortcutAction = ShortcutAction::NativeReplace;
    candidate.selectionTrigger = SelectionTrigger::Icon;
    candidate.rewritePreview = true;
    candidate.readStyle = RewriteStyle::Casual;
    candidate.rewriteStyle = RewriteStyle::Polished;
    candidate.skipApps = {L"notepad", L"code.exe"};
    candidate.hasCompletedFirstLaunch = true;
    candidate.lastUpdateCheck = 1700000001;
    candidate.profileConfig.profiles[0].backend = TranslationBackend::BuiltIn;
    candidate.profileConfig.profiles[0].model.clear();
    candidate.profileConfig.profiles[0].presetStyle = RewriteStyle::Formal;
    candidate.profileConfig.profiles[0].promptMode = PromptMode::Custom;
    Check(!profiles::ValidateCapability(candidate.profileConfig.profiles[0]).ok,
          "fixture preserves a structurally valid unsupported BuiltIn capability");
    if (!SeedCommitted(receiver, candidate, fixture, store)) return;
    Settings reloaded;
    reloaded.load();
    const SettingsData& expected = receiver;
    const SettingsData& actual = reloaded;
    Check(actual == expected, "v2 roundtrip preserves ALL global values and complete profiles/bindings");
}

void TestConstSaveBoundaries(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    if (!SeedCommitted(receiver, CreateValidCandidateSettings(), fixture, store)) return;
    const Settings stable = receiver;
    const size_t events = store.events.size();
    Check(stable.save(), "const already-committed identical v2 save is safe/idempotent");
    Check(stable.connectionEpoch() == receiver.connectionEpoch() && store.events.size() == events,
          "const idempotent save never changes epoch/ref or credentials");
    Settings draft = receiver;
    draft.openAIKey = "unsaved-const-key";
    draft.openAIBaseURL = "https://unsaved.invalid/v1";
    const Settings dirty = draft;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    Check(!dirty.save(), "const dirty candidate cannot publish a file under an old ref/epoch");
    CheckUnchanged(receiver, receiver, fixture, original);
    Check(store.events.size() == events, "rejected const save does not stage or probe credentials");
}

void TestConstLegacyMigrationRejected(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    const std::string original = "{\"backend\":\"openai\",\"openAIKey\":\"legacy-const-key\"}";
    if (!WriteFileBinary(fixture.SettingsFile(), original)) throw InfraException(411, GetLastError());
    Settings loaded;
    loaded.load();
    const Settings receiver = loaded;
    Check(!receiver.save(), "const legacy key without versioned ref requires mutable commit");
    Check(store.events.empty(), "const legacy rejection never creates/refers/deletes credentials");
    const auto after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data == original, "const legacy rejection preserves plaintext predecessor");
}

void TestCleanupWarningAfterCommit(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    if (!SeedCommitted(receiver, CreateValidCandidateSettings(), fixture, store)) return;
    const auto oldRef = receiver.credentialRef();
    const auto epoch = receiver.connectionEpoch();
    store.simulateDeleteFailure = true;
    Settings candidate = receiver;
    candidate.openAIKey = "new-key-despite-old-cleanup-failure";
    Check(receiver.commit(candidate), "old-record cleanup failure does NOT reverse a committed file");
    Check(receiver.openAIKey == candidate.openAIKey && receiver.credentialRef() != oldRef &&
          receiver.connectionEpoch() > epoch && !receiver.storageWarning().empty(),
          "successful file/runtime/key/ref/epoch publication retains an operational cleanup warning");
    Check(store.entries.count(oldRef) == 1 && store.entries.count(receiver.credentialRef()) == 1,
          "failed cleanup leaves old orphan and correct new referenced record");
    CheckV2File(receiver, fixture);
    Settings reloaded;
    reloaded.load();
    Check(reloaded.storageState() == StorageState::Ready && reloaded.openAIKey == candidate.openAIKey,
          "cleanup warning does not invalidate newly persisted credential resolution");
}

void TestBlobLimitBeforeBackend(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    const Settings before = receiver;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    Settings candidate = CreateValidCandidateSettings();
    candidate.openAIKey = std::string(credentials::MaxKeyBytes + 1, 'K');
    Check(!receiver.commit(candidate), "credential blob bound rejects oversized key");
    Check(!credentials::StageKey("valid-ref", candidate.openAIKey) && store.events.empty(),
          "blob bound is checked before backend dispatch in both commit and facade");
    CheckUnchanged(before, receiver, fixture, original);
}

void TestMetadataInWholeFileLimit(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    const Settings before = receiver;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    Settings candidate = CreateValidCandidateSettings();
    const auto prototype = candidate.profileConfig.profiles.front();
    std::vector<TranslationProfile> list;
    for (size_t index = 0; index < 31; ++index) {
        auto profile = prototype;
        profile.id = "metadata-profile-" + std::to_string(index);
        profile.systemPrompt = std::string(profiles::MaxPromptUtf16, 'P');
        list.push_back(std::move(profile));
    }
    candidate.profileConfig = {std::move(list),
        {"metadata-profile-0", "metadata-profile-1", "metadata-profile-0"}};
    candidate.skipApps = std::vector<std::wstring>(100, std::wstring(260, L'A'));
    Check(profiles::Validate(candidate.profileConfig).ok &&
          json::Value(profiles::store::Encode(candidate.profileConfig)).dump().size() <= profiles::MaxSettingsBytes,
          "profiles alone fit byte limit before global metadata is appended");
    Check(!receiver.commit(candidate), "FINAL JSON including global metadata exceeds whole-file bound");
    Check(store.events.empty(), "final JSON size failure precedes credential backend access");
    CheckUnchanged(before, receiver, fixture, original);
}

void TestForeignCandidateRefNotAuthority(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    if (!SeedCommitted(receiver, CreateValidCandidateSettings(), fixture, store)) return;
    const auto oldRef = receiver.credentialRef();
    Settings foreign;
    auto different = CreateValidCandidateSettings();
    different.openAIKey = "foreign-receiver-key";
    if (!SeedCommitted(foreign, different, fixture, store)) return;
    const auto foreignRef = foreign.credentialRef();
    const auto stages = store.stageKeyCalls;
    Check(receiver.commit(foreign), "foreign value candidate can be committed by actual receiver");
    Check(receiver.credentialRef() != foreignRef && receiver.credentialRef() != oldRef &&
          store.stageKeyCalls == stages + 1 && store.entries.count(foreignRef) == 1,
          "candidate private ref is not authority; receiver stages its own ref and retains foreign record");
    CheckV2File(receiver, fixture);
}

void TestLegacyReadError(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    const std::string original = "{\"backend\":\"openai\"}";
    if (!WriteFileBinary(fixture.SettingsFile(), original)) throw InfraException(412, GetLastError());
    store.simulateReadFailure = true;
    Settings receiver;
    receiver.load();
    Check(receiver.storageState() == StorageState::IoError && !receiver.save(),
          "legacy credential ERROR is not silently materialized/persisted as NoKey");
    Check(store.loadLegacyCalls == 1 && store.stageKeyCalls == 0 && store.deleteLegacyCalls == 0,
          "legacy read error never writes or deletes its unknown source");
    const auto after = ReadFileBinary(fixture.SettingsFile());
    Check(after.readable && after.data == original, "legacy credential error preserves predecessor bytes");
}

void TestInvalidGlobalsBeforeRead(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    auto object = profiles::store::Encode(CreateValidCandidateSettings().profileConfig);
    object["openAICredentialRef"] = "otherwise-valid-ref";
    object["ollamaPort"] = 70000;
    const auto original = json::Value(std::move(object)).dump();
    if (!WriteFileBinary(fixture.SettingsFile(), original)) throw InfraException(413, GetLastError());
    Settings receiver;
    receiver.load();
    Check(receiver.storageState() == StorageState::Invalid && !receiver.save(),
          "invalid v2 global metadata locks load/save");
    Check(store.events.empty(), "complete metadata validation precedes referenced credential reads");
}

void TestPreexistingTemporaryOwnership(ScopedStorageFixture& fixture, FakeCredentialStore& store) {
    Settings receiver;
    if (!SeedCommitted(receiver, CreateValidCandidateSettings(), fixture, store)) return;
    const Settings before = receiver;
    const auto original = ReadFileBinary(fixture.SettingsFile());
    const auto entries = store.entries;
    const auto temporary = fixture.SettingsFile() + L".tmp";
    const std::string sentinel = "pre-existing-unowned-temporary";
    if (!WriteFileBinary(temporary, sentinel)) throw InfraException(414, GetLastError());
    Settings candidate = receiver;
    candidate.openAIKey = "rejected-key-with-unowned-temp";
    Check(!receiver.commit(candidate), "transaction refuses to overwrite an unowned pre-existing temporary");
    const auto after = ReadFileBinary(fixture.SettingsFile());
    const auto retained = ReadFileBinary(temporary);
    Check(SameRuntime(before, receiver) && after.readable && after.data == original.data && store.entries == entries,
          "temporary collision rolls back only this transaction's staged key and preserves predecessor");
    Check(retained.readable && retained.data == sentinel, "unowned temporary is neither overwritten nor deleted");
}

void TestCredentialBoundaryAndCanonicalization(ScopedStorageFixture&, FakeCredentialStore& store) {
    Check(credentials::ReadKey("invalid/ref").status == credentials::LoadStatus::Error,
          "typed read rejects invalid namespace suffix before dispatch");
    Check(!credentials::StageKey("valid-ref", std::string("a\0b", 3)) && store.events.empty(),
          "embedded NUL key cannot reach a backend");
    const std::string bounded(credentials::MaxKeyBytes, 'K');
    Check(credentials::StageKey("Ab09_-", bounded), "exact blob byte bound is accepted");
    const auto result = credentials::ReadKey("aB09_-");
    Check(result.status == credentials::LoadStatus::Found && result.key == bounded &&
          store.entries.count("ab09_-") == 1,
          "ASCII case aliases resolve the same canonical namespaced record as WinCred");
    Check(credentials::DeleteKey("AB09_-") && store.entries.empty(), "canonical delete removes only that record");
}

} // namespace

void RunAdditionalCases() {
    RunCase(L"credential-read-error", TestReferencedReadFailure);
    RunCase(L"credential-not-found", TestReferencedNotFound);
    RunCase(L"malformed-ref", TestMalformedRefBeforeRead);
    RunCase(L"global-roundtrip", TestGlobalRoundtripAndCapability);
    RunCase(L"const-save", TestConstSaveBoundaries);
    RunCase(L"const-legacy", TestConstLegacyMigrationRejected);
    RunCase(L"cleanup-warning", TestCleanupWarningAfterCommit);
    RunCase(L"blob-limit", TestBlobLimitBeforeBackend);
    RunCase(L"metadata-byte-limit", TestMetadataInWholeFileLimit);
    RunCase(L"foreign-ref-authority", TestForeignCandidateRefNotAuthority);
    RunCase(L"legacy-read-error", TestLegacyReadError);
    RunCase(L"invalid-global-before-read", TestInvalidGlobalsBeforeRead);
    RunCase(L"temporary-ownership", TestPreexistingTemporaryOwnership);
    RunCase(L"credential-boundary", TestCredentialBoundaryAndCanonicalization);
}

} // namespace transaction_test
