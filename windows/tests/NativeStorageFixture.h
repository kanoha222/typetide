#pragma once
#include <windows.h>
#include <string>

struct InfraException {
    int stepCode = 0;
    DWORD win32Error = 0;
    InfraException(int step, DWORD err) : stepCode(step), win32Error(err) {}
};

struct FileReadResult {
    bool readable = false;
    std::string data;
};

FileReadResult ReadFileBinary(const std::wstring& path);
bool WriteFileBinary(const std::wstring& path, const std::string& data);

class ScopedFileLock {
public:
    ScopedFileLock() = default;
    ~ScopedFileLock();
    ScopedFileLock(const ScopedFileLock&) = delete;
    ScopedFileLock& operator=(const ScopedFileLock&) = delete;

    bool Acquire(const std::wstring& path);
    bool CloseChecked();

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

class ScopedStorageFixture {
public:
    explicit ScopedStorageFixture(const wchar_t* subtestName);
    ~ScopedStorageFixture();
    ScopedStorageFixture(const ScopedStorageFixture&) = delete;
    ScopedStorageFixture& operator=(const ScopedStorageFixture&) = delete;

    std::wstring SettingsFile() const;
    const std::wstring& Directory() const;
    bool Finalize();

private:
    void ValidateRunnerTemp(std::wstring& outRunnerTemp);
    void CreateAndVerifyDirectory(const std::wstring& runnerTemp, const wchar_t* subtestName);
    void CapturePriorEnvironment();
    void ApplyAndVerifyEnvironment();
    bool CleanupOwnedFiles();
    bool RestoreEnvironment();

    std::wstring testDir_;
    std::wstring prevEnvValue_;
    bool hasPrevEnv_ = false;
    bool envApplied_ = false;
    bool ownsDirectory_ = false;
    bool finalized_ = false;
    bool finalizeOk_ = false;
};
