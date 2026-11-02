#include "ProfileBinding.h"
#include "Settings.h"
#include <type_traits>
#include <utility>

namespace profilebinding {

bool CommitConfiguration(Settings& settings, ProfileConfiguration configuration, std::string* error,
                         const ProfileConfiguration* expectedBaseline) {
    if (expectedBaseline && *expectedBaseline != settings.committedData().profileConfig) {
        if (error) *error = "Profiles or bindings changed elsewhere. Close and reopen this editor to reload; your draft was not saved.";
        return false;
    }
    return CommitChange(settings, [&configuration](SettingsData& data) { data.profileConfig = configuration; }, error);
}

bool CommitChange(Settings& settings, const std::function<void(SettingsData&)>& change, std::string* error) {
    try {
        // Prepare BOTH ledgers before persistence: restoration cannot allocate after commit.
        SettingsData restored = settings;
        change(restored);
        Settings candidate = settings;
        static_cast<SettingsData&>(candidate) = settings.committedData();
        change(candidate);
        if (!settings.commit(candidate, error)) return false;
        static_assert(std::is_nothrow_move_assignable_v<SettingsData>);
        static_cast<SettingsData&>(settings) = std::move(restored);
        return true;
    } catch (...) {
        if (error) *error = "Cannot prepare the settings change; committed settings retained.";
        return false;
    }
}

bool Commit(Settings& settings, ProfileEntry entry, const std::string& profileId, std::string* error) {
    const ProfileConfiguration& committed = settings.committedData().profileConfig;
    if (!profiles::Find(committed, profileId)) {
        if (error) *error = "The selected profile no longer exists.";
        return false;
    }
    return CommitConfiguration(settings, profiles::WithBinding(committed, entry, profileId), error);
}

} // namespace profilebinding
