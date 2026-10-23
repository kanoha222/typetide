#pragma once
#include "NativeStorageFixture.h"
#include "TestCredentialStoreSeam.h"
#include "../src/Json.h"
#include "../src/Settings.h"

namespace transaction_test {

using TestCase = void (*)(ScopedStorageFixture&, FakeCredentialStore&);

bool Check(bool condition, const char* name);
int FailureCount();
void RunCase(const wchar_t* name, TestCase test);
Settings CreateValidCandidateSettings();
bool SameRuntime(const Settings& left, const Settings& right);
void CheckUnchanged(const Settings& before, const Settings& after,
                    const ScopedStorageFixture& fixture, const FileReadResult& original);
void CheckNoTemporaryFile(const ScopedStorageFixture& fixture);
json::Value CheckV2File(const Settings& settings, const ScopedStorageFixture& fixture);
bool SeedCommitted(Settings& receiver, const Settings& candidate,
                   const ScopedStorageFixture& fixture, const FakeCredentialStore& store);
size_t FindEvent(const FakeCredentialStore& store, CredentialOperation operation, size_t begin);
bool HasEvent(const FakeCredentialStore& store, CredentialOperation operation,
              const std::string& ref, size_t begin);

} // namespace transaction_test
