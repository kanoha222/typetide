#include "TranslationProfileStore.h"

namespace profiles::store {

ProfileLoadResult Decode(const json::Value& /*root*/) {
    return {ProfileLoadState::Invalid, {}, "Store codec not implemented"};
}

json::Object Encode(const ProfileConfiguration& /*configuration*/) {
    return {};
}

} // namespace profiles::store
