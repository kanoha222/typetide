#include "SettingsStorage.h"
#include "SettingsDiagnostics.h"
#include "Util.h"
#include <utility>

namespace settings_storage {
namespace {

class FileHandle {
public:
    explicit FileHandle(HANDLE handle) : handle_(handle) {}
    ~FileHandle() {
        if (!Close()) settings_diagnostics::Log("settings: file close failed (%lu)", GetLastError());
    }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    HANDLE get() const { return handle_; }
    bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
    bool Close() {
        if (!valid()) return true;
        const HANDLE handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
        return CloseHandle(handle) != FALSE;
    }
private:
    HANDLE handle_;
};

class TemporaryFile {
public:
    explicit TemporaryFile(const std::wstring& path) : path_(path) {}
    ~TemporaryFile() { Cleanup(); }
    void Own() { owned_ = true; }
    void Published() { owned_ = false; }
    bool Cleanup() {
        if (!owned_) return true;
        if (DeleteFileW(path_.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) {
            owned_ = false;
            return true;
        }
        settings_diagnostics::Log("settings: owned temporary cleanup failed (%lu)", GetLastError());
        return false;
    }
private:
    const std::wstring& path_;
    bool owned_ = false;
};

ReadResult ReadOpened(FileHandle& file) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size)) {
        settings_diagnostics::Log("settings: file size query failed (%lu)", GetLastError());
        return {StorageState::IoError, false, {}, "Cannot size settings file"};
    }
    if (size.QuadPart <= 0 || size.QuadPart > static_cast<LONGLONG>(profiles::MaxSettingsBytes)) {
        return {StorageState::Invalid, false, {}, "Settings file is empty or exceeds 1 MiB"};
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool readOk = ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) != FALSE;
    if (!readOk || read != bytes.size()) {
        settings_diagnostics::Log("settings: complete file read failed (%lu)", readOk ? ERROR_READ_FAULT : GetLastError());
        return {StorageState::IoError, false, {}, "Cannot read complete settings file"};
    }
    return {StorageState::Ready, false, std::move(bytes), {}};
}

bool WriteOpened(FileHandle& file, const std::string& bytes, DWORD& failureCode) {
    DWORD written = 0;
    const bool wrote = WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE;
    if (!wrote) failureCode = GetLastError();
    else if (written != bytes.size()) failureCode = ERROR_WRITE_FAULT;
    const bool complete = wrote && written == bytes.size();
    const bool flushed = complete && FlushFileBuffers(file.get()) != FALSE;
    if (complete && !flushed) failureCode = GetLastError();
    const bool closed = file.Close();
    if (!closed && failureCode == ERROR_SUCCESS) failureCode = GetLastError();
    return complete && flushed && closed;
}

} // namespace

ReadResult Read(const std::wstring& path) {
    FileHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {StorageState::Ready, true, {}, {}};
        settings_diagnostics::Log("settings: file open failed (%lu)", error);
        return {StorageState::IoError, false, {}, "Cannot open settings file"};
    }
    auto result = ReadOpened(file);
    if (!file.Close()) {
        settings_diagnostics::Log("settings: file close failed (%lu)", GetLastError());
        return {StorageState::IoError, false, {}, "Cannot close settings file"};
    }
    return result;
}

bool Replace(const std::wstring& path, const std::string& bytes, std::string& error) {
    if (bytes.empty() || bytes.size() > profiles::MaxSettingsBytes) {
        error = "Complete settings exceed the 1 MiB byte limit";
        return false;
    }
    const auto temporaryPath = path + L".tmp";
    TemporaryFile temporary(temporaryPath);
    FileHandle file(CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) {
        error = "Cannot create settings temporary file";
        settings_diagnostics::Log("settings: temporary creation failed (%lu)", GetLastError());
        return false;
    }
    temporary.Own();
    DWORD failureCode = ERROR_SUCCESS;
    const bool written = WriteOpened(file, bytes, failureCode);
    if (written) {
        if (MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            temporary.Published();
            return true;
        }
        failureCode = GetLastError();
    }
    settings_diagnostics::Log("settings: temporary write/flush/close or atomic replacement failed (%lu)", failureCode);
    error = temporary.Cleanup() ? "Cannot atomically replace settings; predecessor preserved"
                                : "Cannot replace settings; owned temporary cleanup incomplete";
    return false;
}

} // namespace settings_storage
