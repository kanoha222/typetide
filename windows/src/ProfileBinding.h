//
//  ProfileBinding.h — commit profile changes (the tray binding submenus, the profile editor's
//  Save) through the settings transaction. Main thread.
//
#pragma once
#include "TranslationProfile.h"
#include "TranslationTypes.h"
#include <functional>
#include <string>

struct Settings;
struct SettingsData;

namespace profilebinding {

/// Commits `configuration` as the profile configuration of `settings`, built on the committed
/// values so unrelated unsaved drafts are neither committed nor discarded. Triggers already in
/// flight keep their frozen profile; later triggers use the new configuration. On failure
/// (invalid configuration, read-only or failed storage) nothing changes and `error` says why.
/// An editor supplies its baseline for optimistic conflict refusal; tray commits omit it.
bool CommitConfiguration(Settings& settings, ProfileConfiguration configuration, std::string* error,
                         const ProfileConfiguration* expectedBaseline = nullptr);

/// Commits one change to the settings the same way: `change` is applied to a copy of the
/// committed values, so a failed save leaves the file and the live configuration as they were,
/// and unrelated unsaved drafts are neither committed nor discarded. On success the change is
/// also applied to the drafts. Settings window edits, the tray toggle, first-run completion and
/// the update-check timestamp all commit through it.
bool CommitChange(Settings& settings, const std::function<void(SettingsData&)>& change, std::string* error);

/// Commits `entry` → `profileId` (an existing committed profile) via CommitConfiguration.
bool Commit(Settings& settings, ProfileEntry entry, const std::string& profileId, std::string* error);

} // namespace profilebinding
