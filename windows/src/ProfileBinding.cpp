#include "ProfileBinding.h"
#include "Settings.h"
#include <utility>

namespace profilebinding {

bool Commit(Settings& settings, ProfileEntry entry, const std::string& profileId, std::string* error) {
    const SettingsData& committed = settings.committedData();
    if (!profiles::Find(committed.profileConfig, profileId)) {
        if (error) *error = "The selected profile no longer exists.";
        return false;
    }
    // The candidate is the committed state plus this one binding, so unrelated unsaved edits
    // are neither committed nor allowed to make the binding fail.
    const SettingsData drafts = settings;
    Settings candidate = settings;
    static_cast<SettingsData&>(candidate) = committed;
    candidate.profileConfig = profiles::WithBinding(committed.profileConfig, entry, profileId);
    if (!settings.commit(candidate, error)) return false;
    // commit() replaced the drafts with the candidate: put the unrelated drafts back.
    SettingsData restored = drafts;
    restored.profileConfig = settings.committedData().profileConfig;
    static_cast<SettingsData&>(settings) = std::move(restored);
    return true;
}

} // namespace profilebinding
