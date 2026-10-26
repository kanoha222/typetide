#include "ProfileBinding.h"
#include "Settings.h"

namespace profilebinding {

// RED scaffold: binding changes are refused until the runtime binding test fails on CI.
bool Commit(Settings& /*settings*/, ProfileEntry /*entry*/, const std::string& /*profileId*/,
            std::string* error) {
    if (error) *error = "Binding changes are not implemented yet";
    return false;
}

} // namespace profilebinding
