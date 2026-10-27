#include "ProfileEditor.h"
#include <utility>

namespace profileeditor {

// RED scaffold: every edit is ignored and every query reports nothing until the editor model
// tests have been observed failing on CI.

Editor::Editor(ProfileConfiguration committed) : baseline_(committed), draft_(std::move(committed)) {
    if (draft_.profiles.empty()) draft_.profiles.push_back(TranslationProfile{});
}

bool Editor::isDirty() const { return false; }

const TranslationProfile& Editor::current() const { return draft_.profiles.front(); }

void Editor::select(std::size_t /*index*/) {}

void Editor::setName(std::string /*utf8*/) {}
void Editor::setLanguages(Language /*native*/, Language /*foreign*/) {}
void Editor::setDirection(TranslationDirection /*direction*/) {}
void Editor::setPromptMode(PromptMode /*mode*/) {}
void Editor::setPresetStyle(RewriteStyle /*style*/) {}
void Editor::setSystemPrompt(std::string /*utf8*/) {}
void Editor::setBackend(TranslationBackend /*backend*/) {}
void Editor::setModel(std::string /*utf8*/) {}
void Editor::setBinding(ProfileEntry /*entry*/, const std::string& /*profileId*/) {}

bool Editor::add(const std::string& /*newId*/) { return false; }
bool Editor::duplicate(const std::string& /*newId*/) { return false; }
DeleteCheck Editor::canDelete() const { return {}; }
bool Editor::remove(const std::string& /*replacementId*/) { return false; }

std::vector<ProfileEntry> Editor::brokenBindings() const { return {}; }
profiles::ValidationResult Editor::validate() const { return {}; }
execution::PreparedRequest Editor::preview(ShortcutAction /*action*/) const { return {}; }

void Editor::discard() {}
void Editor::markSaved(ProfileConfiguration /*committed*/) {}

TranslationProfile& Editor::mutableCurrent() { return draft_.profiles.front(); }
void Editor::reselect(const std::string& /*id*/) {}

} // namespace profileeditor
