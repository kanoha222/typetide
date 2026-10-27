//
//  ProfileBinding.h — commit profile changes (the tray binding submenus, the profile editor's
//  Save) through the settings transaction. Main thread.
//
#pragma once
#include "TranslationProfile.h"
#include "TranslationTypes.h"
#include <string>

struct Settings;

namespace profilebinding {

/// Commits `configuration` as the profile configuration of `settings`, built on the committed
/// values so unrelated unsaved drafts are neither committed nor discarded. Triggers already in
/// flight keep their frozen profile; later triggers use the new configuration. On failure
/// (invalid configuration, read-only or failed storage) nothing changes and `error` says why.
/// An editor supplies its baseline for optimistic conflict refusal; tray commits omit it.
bool CommitConfiguration(Settings& settings, ProfileConfiguration configuration, std::string* error,
                         const ProfileConfiguration* expectedBaseline = nullptr);

/// Commits `entry` → `profileId` (an existing committed profile) via CommitConfiguration.
bool Commit(Settings& settings, ProfileEntry entry, const std::string& profileId, std::string* error);

} // namespace profilebinding
