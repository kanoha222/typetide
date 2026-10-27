//
//  ProfileEditor.h — pure draft model behind the native profile editor window. It edits a
//  copy of the committed ProfileConfiguration: nothing here reads Settings, writes files or
//  touches the network, and nothing reaches the runtime until the window commits draft().
//
#pragma once
#include "TranslationExecution.h"
#include "TranslationProfile.h"
#include <cstddef>
#include <string>
#include <vector>

namespace profileeditor {

/// Name given to a newly added profile.
inline constexpr char kNewProfileName[] = "New profile";
/// Suffix appended to a duplicated profile's name (dropped if the name would overflow).
inline constexpr char kCopySuffix[] = " (copy)";
/// Fixed sample used by the effective-prompt preview; never a user selection.
inline constexpr wchar_t kPreviewSample[] = L"The quick brown fox jumps over the lazy dog.";

struct DeleteCheck {
    bool allowed = false;                // false for the last remaining profile
    std::vector<ProfileEntry> boundBy;   // entries that must be given a replacement first
};

class Editor {
public:
    /// Starts from the committed configuration; the first profile is selected.
    explicit Editor(ProfileConfiguration committed);

    const ProfileConfiguration& draft() const { return draft_; }
    const ProfileConfiguration& baseline() const { return baseline_; }
    /// True when draft() differs from the last committed configuration.
    bool isDirty() const;

    std::size_t selected() const { return selected_; }
    /// The selected profile. The draft always holds at least one profile.
    const TranslationProfile& current() const;
    /// Ignored when out of range.
    void select(std::size_t index);

    // Edits of the selected profile. Text is stored exactly (no trimming, no truncation);
    // limits are reported by validate() rather than silently enforced here.
    void setName(std::string utf8);
    void setLanguages(Language native, Language foreign);
    void setDirection(TranslationDirection direction);
    /// Switching modes keeps both the preset style and the custom prompt text.
    void setPromptMode(PromptMode mode);
    void setPresetStyle(RewriteStyle style);
    void setSystemPrompt(std::string utf8);
    /// Keeps the model text so switching back and forth loses nothing.
    void setBackend(TranslationBackend backend);
    void setModel(std::string utf8);
    void setBinding(ProfileEntry entry, const std::string& profileId);

    /// Appends a profile with defaults and selects it. False at profiles::MaxProfiles or when
    /// `newId` is empty or already used.
    bool add(const std::string& newId);
    /// Appends a copy of the selected profile under `newId` and selects it. Same limits as add().
    bool duplicate(const std::string& newId);
    DeleteCheck canDelete() const;
    /// Deletes the selected profile. When it is bound, every entry bound to it is rebound to
    /// `replacementId`, which must name another existing profile. False (nothing changes) for
    /// the last profile or a missing/invalid replacement.
    bool remove(const std::string& replacementId);

    /// Entries whose binding does not name a profile in draft() (visible repair needed).
    std::vector<ProfileEntry> brokenBindings() const;
    /// Full structural validation of draft(); this is what must pass before saving.
    profiles::ValidationResult validate() const;
    /// Effective request of the selected profile for kPreviewSample (no endpoint, key or model
    /// needed). Shows the rendered system prompt, capability refusal and placeholder warnings.
    execution::PreparedRequest preview(ShortcutAction action = ShortcutAction::SmartPopup) const;

    /// Drops every unsaved edit; the selection stays on the same profile id when it still exists.
    void discard();
    /// After a successful commit: `committed` becomes both baseline and draft.
    void markSaved(ProfileConfiguration committed);

private:
    TranslationProfile& mutableCurrent();
    void reselect(const std::string& id);

    ProfileConfiguration baseline_;
    ProfileConfiguration draft_;
    std::size_t selected_ = 0;
};

} // namespace profileeditor
