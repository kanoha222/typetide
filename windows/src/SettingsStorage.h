#pragma once
#include "Settings.h"
#include <string>

namespace settings_storage {

struct ReadResult {
    StorageState state = StorageState::Ready;
    bool missing = false;
    std::string bytes;
    std::string error;
};

ReadResult Read(const std::wstring& path);
// Caller validates the complete final byte sequence before invoking this function.
// Temp ownership is exclusive; a pre-existing .tmp is never overwritten/deleted.
bool Replace(const std::wstring& path, const std::string& bytes, std::string& error);

} // namespace settings_storage
