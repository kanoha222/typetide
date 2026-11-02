#include "TranslationProfile.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace profiles {
namespace {

bool IsUnicodeWhitespace(uint32_t cp) {
    if (cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == '\v' || cp == '\f') {
        return true;
    }
    if (cp == 0x00A0 || cp == 0x1680) {
        return true;
    }
    if (cp >= 0x2000 && cp <= 0x200A) {
        return true;
    }
    if (cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F ||
        cp == 0x3000 || cp == 0xFEFF) {
        return true;
    }
    return false;
}

bool DecodeScalar(const unsigned char*& p, const unsigned char* end, uint32_t& cp) {
    if (p >= end) return false;
    const std::size_t remaining = static_cast<std::size_t>(end - p);
    const unsigned char b0 = *p;

    if (b0 <= 0x7F) {
        cp = b0;
        p += 1;
        return true;
    }
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        if (remaining < 2) return false;
        const unsigned char b1 = p[1];
        if ((b1 & 0xC0) != 0x80) return false;
        cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
        p += 2;
        return true;
    }
    if (b0 >= 0xE0 && b0 <= 0xEF) {
        if (remaining < 3) return false;
        const unsigned char b1 = p[1];
        const unsigned char b2 = p[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return false;
        if (b0 == 0xE0 && b1 < 0xA0) return false;
        if (b0 == 0xED && b1 >= 0xA0) return false;
        cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
        p += 3;
        return true;
    }
    if (b0 >= 0xF0 && b0 <= 0xF4) {
        if (remaining < 4) return false;
        const unsigned char b1 = p[1];
        const unsigned char b2 = p[2];
        const unsigned char b3 = p[3];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return false;
        if (b0 == 0xF0 && b1 < 0x90) return false;
        if (b0 == 0xF4 && b1 > 0x8F) return false;
        cp = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
        p += 4;
        return true;
    }
    return false;
}

bool AnalyzeUtf8(const std::string& str, std::size_t* outUtf16,
                 bool* outHasControl, bool* outAllWhitespace) {
    std::size_t utf16Count = 0;
    bool hasControl = false;
    bool allWhitespace = true;

    const auto* p = reinterpret_cast<const unsigned char*>(str.data());
    const auto* end = p + str.size();

    while (p < end) {
        uint32_t cp = 0;
        if (!DecodeScalar(p, end, cp)) {
            return false;
        }

        utf16Count += (cp <= 0xFFFF) ? 1 : 2;

        if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F)) {
            hasControl = true;
        }

        if (!IsUnicodeWhitespace(cp)) {
            allWhitespace = false;
        }
    }

    if (str.empty()) {
        allWhitespace = true;
    }

    if (outUtf16) *outUtf16 = utf16Count;
    if (outHasControl) *outHasControl = hasControl;
    if (outAllWhitespace) *outAllWhitespace = allWhitespace;
    return true;
}

void CheckPromptVariables(const std::string& prompt, std::vector<std::string>& warnings) {
    std::size_t pos = 0;
    bool hasUnknown = false;
    while ((pos = prompt.find("{{", pos)) != std::string::npos) {
        const std::size_t end = prompt.find("}}", pos + 2);
        if (end == std::string::npos) break;
        const std::string var = prompt.substr(pos + 2, end - (pos + 2));
        if (var != "source_language" && var != "target_language") {
            hasUnknown = true;
        }
        pos = end + 2;
    }
    if (hasUnknown) {
        warnings.push_back("Custom prompt contains unknown template variable");
    }
}

bool IsValidLanguage(Language l) {
    switch (l) {
    case Language::English:
    case Language::Chinese:
    case Language::Hindi:
    case Language::Spanish:
    case Language::French:
    case Language::Arabic:
    case Language::Bengali:
    case Language::Russian:
    case Language::Portuguese:
    case Language::Indonesian:
        return true;
    }
    return false;
}

bool IsValidDirection(TranslationDirection d) {
    switch (d) {
    case TranslationDirection::Auto:
    case TranslationDirection::NativeToForeign:
    case TranslationDirection::ForeignToNative:
        return true;
    }
    return false;
}

bool IsValidRewriteStyle(RewriteStyle s) {
    switch (s) {
    case RewriteStyle::Faithful:
    case RewriteStyle::Formal:
    case RewriteStyle::Casual:
    case RewriteStyle::Polished:
        return true;
    }
    return false;
}

bool IsValidPromptMode(PromptMode m) {
    return m == PromptMode::Preset || m == PromptMode::Custom;
}

bool IsValidBackend(TranslationBackend b) {
    switch (b) {
    case TranslationBackend::BuiltIn:
    case TranslationBackend::Ollama:
    case TranslationBackend::OpenAI:
        return true;
    }
    return false;
}

ValidationResult ValidateEnums(const TranslationProfile& profile) {
    if (!IsValidLanguage(profile.nativeLanguage)) return {false, "Invalid native language", {}};
    if (!IsValidLanguage(profile.foreignLanguage)) return {false, "Invalid foreign language", {}};
    if (!IsValidDirection(profile.direction)) return {false, "Invalid translation direction", {}};
    if (!IsValidRewriteStyle(profile.presetStyle)) return {false, "Invalid preset style", {}};
    if (!IsValidPromptMode(profile.promptMode)) return {false, "Invalid prompt mode", {}};
    if (!IsValidBackend(profile.backend)) return {false, "Invalid backend", {}};
    return {true, "", {}};
}

ValidationResult ValidateIdentifiers(const TranslationProfile& profile) {
    if (profile.id.empty()) return {false, "Empty ID rejected", {}};
    if (profile.id.size() > MaxIdBytes) return {false, "ID exceeds byte limit", {}};
    bool idHasControl = false;
    if (!AnalyzeUtf8(profile.id, nullptr, &idHasControl, nullptr)) {
        return {false, "ID must be valid UTF-8", {}};
    }
    if (idHasControl) return {false, "ID must not contain control characters", {}};

    if (profile.name.empty()) return {false, "Empty name rejected", {}};
    std::size_t nameUtf16 = 0;
    bool nameHasControl = false;
    bool nameAllWhitespace = false;
    if (!AnalyzeUtf8(profile.name, &nameUtf16, &nameHasControl, &nameAllWhitespace)) {
        return {false, "Name must be valid UTF-8", {}};
    }
    if (nameAllWhitespace) return {false, "Spaces-only name rejected", {}};
    if (nameHasControl) return {false, "Name must not contain control characters", {}};
    if (nameUtf16 > MaxNameUtf16) return {false, "Name exceeds UTF-16 code unit limit", {}};
    return {true, "", {}};
}

ValidationResult ValidatePromptAndModel(const TranslationProfile& profile,
                                       std::vector<std::string>& warnings) {
    if (profile.model.size() > MaxModelBytes) return {false, "Model exceeds byte limit", {}};
    bool modelHasControl = false;
    bool modelAllWhitespace = false;
    if (!AnalyzeUtf8(profile.model, nullptr, &modelHasControl, &modelAllWhitespace)) {
        return {false, "Model must be valid UTF-8", {}};
    }
    if (modelHasControl) return {false, "Model must not contain control characters", {}};
    if (profile.backend != TranslationBackend::BuiltIn && (profile.model.empty() || modelAllWhitespace)) {
        return {false, "Blank remote model rejected", {}};
    }

    std::size_t promptUtf16 = 0;
    bool promptAllWhitespace = false;
    if (!AnalyzeUtf8(profile.systemPrompt, &promptUtf16, nullptr, &promptAllWhitespace)) {
        return {false, "System prompt must be valid UTF-8", {}};
    }
    if (promptUtf16 > MaxPromptUtf16) return {false, "Prompt exceeds UTF-16 code unit limit", {}};

    if (profile.promptMode == PromptMode::Custom) {
        if (profile.systemPrompt.empty() || promptAllWhitespace) {
            return {false, "Custom prompt cannot be blank", {}};
        }
        CheckPromptVariables(profile.systemPrompt, warnings);
    }
    return {true, "", {}};
}

} // namespace

bool IsBlankText(const std::string& utf8) {
    bool allWhitespace = false;
    return AnalyzeUtf8(utf8, nullptr, nullptr, &allWhitespace) && allWhitespace;
}

ValidationResult Validate(const TranslationProfile& profile) {
    auto enumRes = ValidateEnums(profile);
    if (!enumRes.ok) return enumRes;

    auto idRes = ValidateIdentifiers(profile);
    if (!idRes.ok) return idRes;

    std::vector<std::string> warnings;
    auto textRes = ValidatePromptAndModel(profile, warnings);
    if (!textRes.ok) return textRes;

    return {true, "", std::move(warnings)};
}

ValidationResult Validate(const ProfileConfiguration& configuration) {
    if (configuration.profiles.empty()) {
        return {false, "Cannot delete final profile", {}};
    }
    if (configuration.profiles.size() > MaxProfiles) {
        return {false, "Exceeded maximum profile count (64)", {}};
    }

    std::vector<std::string> allWarnings;
    std::vector<std::string> seenIds;
    seenIds.reserve(configuration.profiles.size());

    for (const auto& profile : configuration.profiles) {
        auto res = Validate(profile);
        if (!res.ok) return res;

        for (const auto& existing : seenIds) {
            if (existing == profile.id) {
                return {false, "Duplicate profile ID rejected", {}};
            }
        }
        seenIds.push_back(profile.id);

        allWarnings.insert(allWarnings.end(), res.warnings.begin(), res.warnings.end());
    }

    auto findId = [&](const std::string& id) {
        for (const auto& existing : seenIds) {
            if (existing == id) return true;
        }
        return false;
    };

    if (configuration.bindings.shortcut1ProfileId.empty() ||
        !findId(configuration.bindings.shortcut1ProfileId)) {
        return {false, "shortcut1ProfileId does not resolve to a profile", {}};
    }
    if (configuration.bindings.shortcut2ProfileId.empty() ||
        !findId(configuration.bindings.shortcut2ProfileId)) {
        return {false, "shortcut2ProfileId does not resolve to a profile", {}};
    }
    if (configuration.bindings.selectionProfileId.empty() ||
        !findId(configuration.bindings.selectionProfileId)) {
        return {false, "selectionProfileId does not resolve to a profile", {}};
    }

    return {true, "", std::move(allWarnings)};
}

ValidationResult ValidateCapability(const TranslationProfile& profile) {
    auto structural = Validate(profile);
    if (!structural.ok) {
        return structural;
    }
    if (profile.backend == TranslationBackend::BuiltIn) {
        if (profile.promptMode != PromptMode::Preset ||
            profile.presetStyle != RewriteStyle::Faithful) {
            return {false, "BuiltIn backend only supports faithful preset style", std::move(structural.warnings)};
        }
    }
    return structural;
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
    read.model = "";

    TranslationProfile rewrite;
    rewrite.id = "default-rewrite";
    rewrite.name = "默认改写";
    rewrite.nativeLanguage = Language::Chinese;
    rewrite.foreignLanguage = Language::English;
    rewrite.direction = TranslationDirection::NativeToForeign;
    rewrite.promptMode = PromptMode::Preset;
    rewrite.presetStyle = RewriteStyle::Faithful;
    rewrite.backend = TranslationBackend::BuiltIn;
    rewrite.model = "";

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

ProfileConfiguration WithBinding(ProfileConfiguration configuration, ProfileEntry entry,
                                 const std::string& profileId) {
    switch (entry) {
    case ProfileEntry::Shortcut1: configuration.bindings.shortcut1ProfileId = profileId; break;
    case ProfileEntry::Shortcut2: configuration.bindings.shortcut2ProfileId = profileId; break;
    case ProfileEntry::Selection: configuration.bindings.selectionProfileId = profileId; break;
    }
    return configuration;
}

} // namespace profiles
