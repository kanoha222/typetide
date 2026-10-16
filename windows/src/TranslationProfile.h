// Value-only profile configuration. Strings are strict UTF-8, not UI buffers.
#pragma once
#include "Language.h"
#include "TranslationTypes.h"
#include <cstddef>
#include <string>
#include <vector>

struct TranslationProfile {
    std::string id;
    std::string name;
    Language nativeLanguage = Language::Chinese;
    Language foreignLanguage = Language::English;
    TranslationDirection direction = TranslationDirection::Auto;
    PromptMode promptMode = PromptMode::Preset;
    RewriteStyle presetStyle = RewriteStyle::Faithful;
    std::string systemPrompt;
    TranslationBackend backend = TranslationBackend::BuiltIn;
    // Empty for the fixed BuiltIn model; explicit for Ollama/OpenAI.
    std::string model;
    bool operator==(const TranslationProfile&) const = default;
};

struct ProfileBindings {
    std::string shortcut1ProfileId;
    std::string shortcut2ProfileId;
    std::string selectionProfileId;
    bool operator==(const ProfileBindings&) const = default;
};

struct ProfileConfiguration {
    std::vector<TranslationProfile> profiles;
    ProfileBindings bindings;
    bool operator==(const ProfileConfiguration&) const = default;
};

namespace profiles {

inline constexpr std::size_t MaxProfiles = 64;
inline constexpr std::size_t MaxNameUtf16 = 80;
inline constexpr std::size_t MaxPromptUtf16 = 32 * 1024;
inline constexpr std::size_t MaxIdBytes = 128;
inline constexpr std::size_t MaxModelBytes = 256;
inline constexpr std::size_t MaxSettingsBytes = 1024 * 1024;

struct ValidationResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;
};

// Structural validation does not reject a preserved, unsupported backend choice.
ValidationResult Validate(const TranslationProfile& profile);
ValidationResult Validate(const ProfileConfiguration& configuration);
ValidationResult ValidateCapability(const TranslationProfile& profile);
ProfileConfiguration Defaults();

// Returned pointers are short-lived views. Copy before asynchronous use.
const TranslationProfile* Find(const ProfileConfiguration& configuration,
                               const std::string& id);
const TranslationProfile* BoundProfile(const ProfileConfiguration& configuration,
                                       ProfileEntry entry);

// Implemented by the Windows persistence layer using CoCreateGuid.
std::string NewId();

} // namespace profiles
