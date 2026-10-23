#include "NativeStorageFixture.h"
#include "../src/Settings.h"
#include <cstdint>
#include <cstdio>
#include <fstream>

namespace {
constexpr size_t kMaxReadFileCap = 2 * 1024 * 1024; // 2 MiB test cap
}

FileReadResult ReadFileBinary(const std::wstring& path) {
    FileReadResult result;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return result;
    const auto endPos = f.tellg();
    if (endPos < 0) return result;
    const auto size = static_cast<size_t>(endPos);
    if (size > kMaxReadFileCap) return result;
    if (size == 0) {
        result.readable = true;
        result.data.clear();
        return result;
    }
    f.seekg(0, std::ios::beg);
    if (!f.good()) return result;
    result.data.resize(size);
    f.read(result.data.data(), static_cast<std::streamsize>(size));
    if (f.gcount() != static_cast<std::streamsize>(size) || f.fail()) {
        result.readable = false;
        result.data.clear();
        return result;
    }
    result.readable = true;
    return result;
}

bool WriteFileBinary(const std::wstring& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    f.flush();
    if (!f.good()) return false;
    f.close();
    return !f.fail();
}

ScopedFileLock::~ScopedFileLock() {
    CloseChecked();
}

bool ScopedFileLock::Acquire(const std::wstring& path) {
    handle_ = CreateFileW(path.c_str(),
                          GENERIC_READ | GENERIC_WRITE,
                          0 /* NO SHARE */,
                          nullptr,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          nullptr);
    return handle_ != INVALID_HANDLE_VALUE;
}

bool ScopedFileLock::CloseChecked() {
    if (handle_ == INVALID_HANDLE_VALUE) return true;
    BOOL ok = CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
    return ok != FALSE;
}

std::wstring ScopedStorageFixture::SettingsFile() const {
    return testDir_ + L"\\settings.json";
}

const std::wstring& ScopedStorageFixture::Directory() const {
    return testDir_;
}

void ScopedStorageFixture::ValidateRunnerTemp(std::wstring& outRunnerTemp) {
    DWORD runnerTempLen = GetEnvironmentVariableW(L"RUNNER_TEMP", nullptr, 0);
    if (runnerTempLen == 0) {
        throw InfraException(101, GetLastError());
    }
    outRunnerTemp.resize(runnerTempLen);
    DWORD readLen = GetEnvironmentVariableW(L"RUNNER_TEMP", outRunnerTemp.data(), runnerTempLen);
    if (readLen == 0 || readLen >= runnerTempLen) {
        throw InfraException(102, GetLastError());
    }
    outRunnerTemp.resize(readLen);

    const bool hasDrive = (outRunnerTemp.size() >= 3 &&
                           outRunnerTemp[1] == L':' &&
                           (outRunnerTemp[2] == L'\\' || outRunnerTemp[2] == L'/'));
    const bool isUnc = (outRunnerTemp.size() >= 2 &&
                        outRunnerTemp[0] == L'\\' && outRunnerTemp[1] == L'\\');
    if (!hasDrive && !isUnc) {
        throw InfraException(103, 0);
    }
}

void ScopedStorageFixture::CreateAndVerifyDirectory(const std::wstring& runnerTemp,
                                                   const wchar_t* subtestName) {
    static uint64_t s_counter = 0;
    std::wstring dirName = L"tt-ns-" + std::wstring(subtestName) + L"-" +
                           std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(++s_counter);
    testDir_ = runnerTemp + L"\\" + dirName;

    // Check MAX_PATH limit before invoking any Win32/Util calls
    const std::wstring expectedTmp = testDir_ + L"\\settings.json.tmp";
    if (expectedTmp.size() >= MAX_PATH) {
        throw InfraException(104, 0);
    }

    if (!CreateDirectoryW(testDir_.c_str(), nullptr)) {
        // Even ERROR_ALREADY_EXISTS is rejected; never reuse/own an existing folder
        throw InfraException(105, GetLastError());
    }
    ownsDirectory_ = true;

    DWORD attrs = GetFileAttributesW(testDir_.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        throw InfraException(106, GetLastError());
    }
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        RemoveDirectoryW(testDir_.c_str());
        ownsDirectory_ = false;
        throw InfraException(107, 0);
    }
}

void ScopedStorageFixture::CapturePriorEnvironment() {
    DWORD prevLen = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", nullptr, 0);
    if (prevLen == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_ENVVAR_NOT_FOUND) {
            hasPrevEnv_ = false;
            prevEnvValue_.clear();
        } else {
            throw InfraException(108, err);
        }
    } else {
        prevEnvValue_.resize(prevLen);
        SetLastError(ERROR_SUCCESS);
        DWORD actualLen = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR",
                                                  prevEnvValue_.data(), prevLen);
        DWORD err = GetLastError();
        if (actualLen == 0 && err != ERROR_SUCCESS) {
            throw InfraException(109, err);
        }
        if (actualLen >= prevLen) {
            throw InfraException(110, 0);
        }
        prevEnvValue_.resize(actualLen);
        hasPrevEnv_ = true;
    }
}

void ScopedStorageFixture::ApplyAndVerifyEnvironment() {
    if (!SetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", testDir_.c_str())) {
        throw InfraException(111, GetLastError());
    }
    envApplied_ = true;

    DWORD vLen = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", nullptr, 0);
    if (vLen == 0) {
        throw InfraException(112, GetLastError());
    }
    std::wstring readBack;
    readBack.resize(vLen);
    DWORD actualV = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", readBack.data(), vLen);
    if (actualV == 0 || actualV >= vLen) {
        throw InfraException(113, GetLastError());
    }
    readBack.resize(actualV);
    if (readBack != testDir_) {
        throw InfraException(114, 0);
    }

    if (Settings::filePath() != SettingsFile()) {
        throw InfraException(115, 0);
    }
}

bool ScopedStorageFixture::CleanupOwnedFiles() {
    if (!ownsDirectory_) return true;

    // Re-verify that testDir_ is still a directory and not a reparse point
    DWORD attrs = GetFileAttributesW(testDir_.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES ||
        !(attrs & FILE_ATTRIBUTE_DIRECTORY) ||
        (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return false;
    }

    auto deleteKnown = [&](const std::wstring& relName) -> bool {
        std::wstring p = testDir_ + L"\\" + relName;
        if (DeleteFileW(p.c_str())) return true;
        DWORD err = GetLastError();
        return (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND);
    };

    bool ok1 = deleteKnown(L"settings.json");
    bool ok2 = deleteKnown(L"settings.json.tmp");
    bool ok3 = deleteKnown(L"debug.log");

    BOOL rmOk = RemoveDirectoryW(testDir_.c_str());
    DWORD rmErr = GetLastError();
    bool dirRemoved = (rmOk != FALSE ||
                       rmErr == ERROR_FILE_NOT_FOUND ||
                       rmErr == ERROR_PATH_NOT_FOUND);
    if (dirRemoved) {
        ownsDirectory_ = false;
    }
    return ok1 && ok2 && ok3 && dirRemoved;
}

bool ScopedStorageFixture::RestoreEnvironment() {
    if (!envApplied_) return true;

    BOOL ok = FALSE;
    if (hasPrevEnv_) {
        ok = SetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", prevEnvValue_.c_str());
    } else {
        ok = SetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", nullptr);
    }
    if (ok) {
        envApplied_ = false;
        return true;
    }
    return false;
}

bool ScopedStorageFixture::Finalize() {
    if (finalized_) {
        return finalizeOk_;
    }
    finalized_ = true;
    const bool cleanOk = CleanupOwnedFiles();
    const bool envOk = RestoreEnvironment();
    finalizeOk_ = cleanOk && envOk;
    return finalizeOk_;
}

ScopedStorageFixture::ScopedStorageFixture(const wchar_t* subtestName) {
    std::wstring runnerTemp;
    ValidateRunnerTemp(runnerTemp);
    try {
        CreateAndVerifyDirectory(runnerTemp, subtestName);
        CapturePriorEnvironment();
        ApplyAndVerifyEnvironment();
    } catch (...) {
        bool cleanOk = CleanupOwnedFiles();
        bool restoreOk = RestoreEnvironment();
        if (!cleanOk || !restoreOk) {
            std::fprintf(stderr, "[infra error step 120] rollback failed (clean=%d, restore=%d)\n",
                         static_cast<int>(cleanOk), static_cast<int>(restoreOk));
        }
        throw;
    }
}

ScopedStorageFixture::~ScopedStorageFixture() {
    if (!finalized_) {
        if (!Finalize()) {
            std::fprintf(stderr, "[infra error step 121] dtor finalize failed\n");
        }
    } else if (!finalizeOk_) {
        std::fprintf(stderr, "[infra error step 122] dtor previously failed finalize\n");
    }
}
