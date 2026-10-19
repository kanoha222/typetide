#include "TranslationProfile.h"
#include <utility>

namespace profiles {

ValidationResult Validate(const TranslationProfile& /*profile*/) {
    return {false, "Validation not implemented", {}};
}

ValidationResult Validate(const ProfileConfiguration& /*configuration*/) {
    return {false, "Configuration validation not implemented", {}};
}

ValidationResult ValidateCapability(const TranslationProfile& /*profile*/) {
    return {false, "Capability validation not implemented", {}};
}

ProfileConfiguration Defaults() {
    TranslationProfile read;
    read.id = "default-read";
    read.name = "默认翻译";
    read.nativeLanguage = Language::Chinese;
    read.foreignLanguage = Language::English;
    read.direction = TranslationDirection::Auto;
    read.promptMode = PromptMode::Preset;
    read.presetStyle = RewriteStyle::Faithful;
    read.backend = TranslationBackend::BuiltIn;

    TranslationProfile rewrite;
    rewrite.id = "default-rewrite";
    rewrite.name = "默认改写";
    rewrite.nativeLanguage = Language::Chinese;
    rewrite.foreignLanguage = Language::English;
    rewrite.direction = TranslationDirection::NativeToForeign;
    rewrite.promptMode = PromptMode::Preset;
    rewrite.presetStyle = RewriteStyle::Faithful;
    rewrite.backend = TranslationBackend::BuiltIn;

    ProfileBindings bindings;
    bindings.shortcut1ProfileId = read.id;
    bindings.shortcut2ProfileId = rewrite.id;
    bindings.selectionProfileId = read.id;

    return {{std::move(read), std::move(rewrite)}, std::move(bindings)};
}

const TranslationProfile* Find(const ProfileConfiguration& configuration,
                               const std::string& id) {
    for (const auto& profile : configuration.profiles) {
        if (profile.id == id) {
            return &profile;
        }
    }
    return nullptr;
}

const TranslationProfile* BoundProfile(const ProfileConfiguration& configuration,
                                       ProfileEntry entry) {
    const std::string* id = nullptr;
    switch (entry) {
    case ProfileEntry::Shortcut1:
        id = &configuration.bindings.shortcut1ProfileId;
        break;
    case ProfileEntry::Shortcut2:
        id = &configuration.bindings.shortcut2ProfileId;
        break;
    case ProfileEntry::Selection:
        id = &configuration.bindings.selectionProfileId;
        break;
    default:
        return nullptr;
    }
    return id ? Find(configuration, *id) : nullptr;
}

} // namespace profiles
