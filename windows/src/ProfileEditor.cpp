#include "ProfileEditor.h"
#include <cstddef>
#include <utility>

namespace profileeditor {

namespace {

constexpr ProfileEntry kEntries[] = {ProfileEntry::Shortcut1, ProfileEntry::Shortcut2, ProfileEntry::Selection};

const std::string& BindingOf(const ProfileBindings& bindings, ProfileEntry entry) {
    switch (entry) {
    case ProfileEntry::Shortcut1: return bindings.shortcut1ProfileId;
    case ProfileEntry::Shortcut2: return bindings.shortcut2ProfileId;
    case ProfileEntry::Selection: break;
    }
    return bindings.selectionProfileId;
}

bool CanAppend(const ProfileConfiguration& config, const std::string& newId) {
    return !newId.empty() && config.profiles.size() < profiles::MaxProfiles && !profiles::Find(config, newId);
}

/// UTF-16 code units of valid UTF-8 text (4-byte sequences need a surrogate pair).
std::size_t Utf16Length(const std::string& utf8) {
    std::size_t units = 0;
    for (unsigned char byte : utf8) {
        if ((byte & 0xC0) == 0x80) continue;  // continuation byte
        units += byte >= 0xF0 ? 2 : 1;
    }
    return units;
}

/// The copy's name: "<name> (copy)", or the unchanged name if the suffix would exceed the limit.
std::string CopyName(const std::string& name) {
    std::string copy = name + kCopySuffix;
    return Utf16Length(copy) <= profiles::MaxNameUtf16 ? copy : name;
}

} // namespace

Editor::Editor(ProfileConfiguration committed) : baseline_(committed), draft_(std::move(committed)) {
    // A committed configuration always holds a profile; keep the model usable regardless.
    if (draft_.profiles.empty()) draft_.profiles.push_back(profiles::Defaults().profiles.front());
}

bool Editor::isDirty() const {
    return draft_ != baseline_;
}

const TranslationProfile& Editor::current() const {
    return draft_.profiles[selected_];
}

TranslationProfile& Editor::mutableCurrent() {
    return draft_.profiles[selected_];
}

void Editor::select(std::size_t index) {
    if (index < draft_.profiles.size()) selected_ = index;
}

void Editor::setName(std::string utf8) { mutableCurrent().name = std::move(utf8); }

void Editor::setLanguages(Language native, Language foreign) {
    mutableCurrent().nativeLanguage = native;
    mutableCurrent().foreignLanguage = foreign;
}

void Editor::setDirection(TranslationDirection direction) { mutableCurrent().direction = direction; }
void Editor::setPromptMode(PromptMode mode) { mutableCurrent().promptMode = mode; }
void Editor::setPresetStyle(RewriteStyle style) { mutableCurrent().presetStyle = style; }
void Editor::setSystemPrompt(std::string utf8) { mutableCurrent().systemPrompt = std::move(utf8); }
void Editor::setBackend(TranslationBackend backend) { mutableCurrent().backend = backend; }
void Editor::setModel(std::string utf8) { mutableCurrent().model = std::move(utf8); }

void Editor::setBinding(ProfileEntry entry, const std::string& profileId) {
    draft_ = profiles::WithBinding(std::move(draft_), entry, profileId);
}

bool Editor::add(const std::string& newId) {
    if (!CanAppend(draft_, newId)) return false;
    TranslationProfile profile = profiles::Defaults().profiles.front();
    profile.id = newId;
    profile.name = kNewProfileName;
    draft_.profiles.push_back(std::move(profile));
    selected_ = draft_.profiles.size() - 1;
    return true;
}

bool Editor::duplicate(const std::string& newId) {
    if (!CanAppend(draft_, newId)) return false;
    TranslationProfile copy = current();
    copy.id = newId;
    copy.name = CopyName(copy.name);
    draft_.profiles.push_back(std::move(copy));
    selected_ = draft_.profiles.size() - 1;
    return true;
}

DeleteCheck Editor::canDelete() const {
    DeleteCheck check;
    check.allowed = draft_.profiles.size() > 1;
    for (ProfileEntry entry : kEntries)
        if (BindingOf(draft_.bindings, entry) == current().id) check.boundBy.push_back(entry);
    return check;
}

bool Editor::remove(const std::string& replacementId) {
    const DeleteCheck check = canDelete();
    if (!check.allowed) return false;
    const std::string deletedId = current().id;
    if (!check.boundBy.empty()) {
        if (replacementId.empty() || replacementId == deletedId || !profiles::Find(draft_, replacementId)) return false;
        for (ProfileEntry entry : check.boundBy) draft_ = profiles::WithBinding(std::move(draft_), entry, replacementId);
    }
    draft_.profiles.erase(draft_.profiles.begin() + static_cast<std::ptrdiff_t>(selected_));
    if (selected_ >= draft_.profiles.size()) selected_ = draft_.profiles.size() - 1;
    return true;
}

std::vector<ProfileEntry> Editor::brokenBindings() const {
    std::vector<ProfileEntry> broken;
    for (ProfileEntry entry : kEntries)
        if (!profiles::BoundProfile(draft_, entry)) broken.push_back(entry);
    return broken;
}

profiles::ValidationResult Editor::validate() const {
    return profiles::Validate(draft_);
}

execution::PreparedRequest Editor::preview(ShortcutAction action) const {
    const execution::ContextPtr context = execution::Freeze(current(), action, execution::ConnectionSnapshot{});
    return execution::Preview(*context, kPreviewSample);
}

void Editor::discard() {
    const std::string id = current().id;
    draft_ = baseline_;
    if (draft_.profiles.empty()) draft_.profiles.push_back(profiles::Defaults().profiles.front());
    reselect(id);
}

void Editor::markSaved(ProfileConfiguration committed) {
    const std::string id = current().id;
    baseline_ = committed;
    draft_ = std::move(committed);
    if (draft_.profiles.empty()) draft_.profiles.push_back(profiles::Defaults().profiles.front());
    reselect(id);
}

void Editor::reselect(const std::string& id) {
    for (std::size_t i = 0; i < draft_.profiles.size(); ++i) {
        if (draft_.profiles[i].id == id) {
            selected_ = i;
            return;
        }
    }
    if (selected_ >= draft_.profiles.size()) selected_ = 0;
}

} // namespace profileeditor
