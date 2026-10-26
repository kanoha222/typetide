//
//  ProfileBinding.h — switch which profile a physical entry (shortcut slot 1/2, selection)
//  runs, through the settings transaction. Used by the tray binding submenus. Main thread.
//
#pragma once
#include "TranslationTypes.h"
#include <string>

struct Settings;

namespace profilebinding {

/// Commits `entry` → `profileId` as a candidate of `settings`. Triggers already in flight
/// keep their frozen profile; later triggers use the new binding. On failure (unknown id,
/// read-only or failed storage) nothing changes and `error` explains why.
bool Commit(Settings& settings, ProfileEntry entry, const std::string& profileId, std::string* error);

} // namespace profilebinding
