#include "ProfileBinding.h"
#include "Settings.h"

namespace profilebinding {

bool Commit(Settings& settings, ProfileEntry entry, const std::string& profileId, std::string* error) {
    const ProfileConfiguration committed = settings.snapshot().profileConfig;
    if (!profiles::Find(committed, profileId)) {
        if (error) *error = "The selected profile no longer exists.";
        return false;
    }
    Settings candidate = settings;
    candidate.profileConfig = profiles::WithBinding(committed, entry, profileId);
    return settings.commit(candidate, error);
}

} // namespace profilebinding
