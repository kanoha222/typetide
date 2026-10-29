#include "../src/ProfileEditor.h"
#include "TranslationExecutionSupport.h"
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

// Draft model behind the native profile editor: isolation, CRUD, deletion rules, broken
// bindings, validation limits and the effective-prompt preview.
namespace execution_test {
namespace {

using profileeditor::Editor;

bool Has(const std::vector<ProfileEntry>& entries, ProfileEntry entry) {
    for (ProfileEntry e : entries)
        if (e == entry) return true;
    return false;
}

void TestStartsClean() {
    const Editor editor(profiles::Defaults());
    Check(!editor.isDirty() && editor.selected() == 0 && editor.current().id == "default-read",
          "a new editor starts clean on the first profile");
    Check(editor.validate().ok && editor.brokenBindings().empty(), "the default configuration validates");
}

void TestEditsAreExactAndIsolated() {
    Editor editor(profiles::Defaults());
    const std::string name = "  Mixed \xE5\x90\x8D\xE5\xAD\x97 \xF0\x9F\x98\x80 ";
    editor.setName(name);
    Check(editor.current().name == name, "a name is stored exactly, spaces and emoji included");
    Check(editor.isDirty(), "an edit makes the draft dirty");
    Check(editor.baseline().profiles[0].name != name, "edits never touch the committed baseline");
    const std::string prompt = "Line one\r\n\tindented \"quoted\"\n\n\xE4\xBD\xA0\xE5\xA5\xBD  ";
    editor.setPromptMode(PromptMode::Custom);
    editor.setSystemPrompt(prompt);
    Check(editor.current().systemPrompt == prompt, "a multiline prompt is stored exactly");
    editor.setLanguages(Language::Spanish, Language::French);
    editor.setDirection(TranslationDirection::ForeignToNative);
    Check(editor.current().nativeLanguage == Language::Spanish && editor.current().foreignLanguage == Language::French &&
              editor.current().direction == TranslationDirection::ForeignToNative,
          "language pair and direction are edited on the selected profile");
}

void TestModesAndBackendKeepDrafts() {
    Editor editor(profiles::Defaults());
    editor.setPresetStyle(RewriteStyle::Formal);
    editor.setSystemPrompt("Custom draft");
    editor.setPromptMode(PromptMode::Custom);
    Check(editor.current().presetStyle == RewriteStyle::Formal, "switching to custom keeps the preset style");
    editor.setPromptMode(PromptMode::Preset);
    Check(editor.current().systemPrompt == "Custom draft", "switching to preset keeps the custom prompt");
    editor.setBackend(TranslationBackend::Ollama);
    editor.setModel("synthetic-model:1b");
    editor.setBackend(TranslationBackend::OpenAI);
    editor.setBackend(TranslationBackend::Ollama);
    Check(editor.current().backend == TranslationBackend::Ollama && editor.current().model == "synthetic-model:1b",
          "switching backends keeps the model text");
}

void TestSelectionKeepsEdits() {
    Editor editor(profiles::Defaults());
    editor.setName("Edited read");
    editor.select(1);
    Check(editor.selected() == 1 && editor.current().id == "default-rewrite", "selection moves to another profile");
    editor.setName("Edited rewrite");
    editor.select(0);
    Check(editor.current().name == "Edited read", "edits survive switching between profiles");
    editor.select(99);
    Check(editor.selected() == 0, "an out-of-range selection is ignored");
}

void TestAddAndDuplicate() {
    Editor editor(profiles::Defaults());
    Check(editor.add("new-1") && editor.draft().profiles.size() == 3 && editor.selected() == 2,
          "add appends a profile and selects it");
    Check(editor.current().id == "new-1" && editor.current().name == profileeditor::kNewProfileName,
          "a new profile has the given id and the default name");
    Check(editor.validate().ok, "a new profile validates");
    Check(!editor.add("") && !editor.add("new-1"), "add refuses an empty or duplicate id");
    editor.select(0);
    editor.setPromptMode(PromptMode::Custom);
    editor.setSystemPrompt("Keep me");
    Check(editor.duplicate("dup-1") && editor.current().id == "dup-1", "duplicate selects the copy");
    Check(editor.current().name == std::string("\xE9\xBB\x98\xE8\xAE\xA4\xE7\xBF\xBB\xE8\xAF\x91") +
                                       profileeditor::kCopySuffix &&
              editor.current().systemPrompt == "Keep me" && editor.current().promptMode == PromptMode::Custom,
          "a duplicate copies every field and marks its name");
    // Bounded: a non-growing add() must fail the check below, never hang the run.
    for (std::size_t i = 0; i < profiles::MaxProfiles && editor.draft().profiles.size() < profiles::MaxProfiles; ++i)
        editor.add("fill-" + std::to_string(i));
    Check(editor.draft().profiles.size() == profiles::MaxProfiles, "add fills up to the profile limit");
    Check(!editor.add("one-too-many") && !editor.duplicate("one-too-many"), "the profile limit is enforced");
}

void TestDeleteRules() {
    Editor editor(profiles::Defaults());
    const profileeditor::DeleteCheck check = editor.canDelete();
    Check(check.allowed && Has(check.boundBy, ProfileEntry::Shortcut1) && Has(check.boundBy, ProfileEntry::Selection) &&
              !Has(check.boundBy, ProfileEntry::Shortcut2),
          "deleting a bound profile lists the entries that need a replacement");
    Check(!editor.remove("") && !editor.remove("default-read") && !editor.remove("missing"),
          "a bound profile needs an explicit replacement that is another existing profile");
    Check(editor.draft().profiles.size() == 2, "a refused delete changes nothing");
    Check(editor.remove("default-rewrite"), "a bound profile is deleted with a replacement");
    Check(editor.draft().bindings.shortcut1ProfileId == "default-rewrite" &&
              editor.draft().bindings.selectionProfileId == "default-rewrite" && editor.validate().ok,
          "every entry bound to the deleted profile moves to the replacement");
    Check(!editor.canDelete().allowed && !editor.remove(""), "the last profile cannot be deleted");
    editor.add("extra");
    Check(editor.canDelete().boundBy.empty() && editor.remove(""), "an unbound profile is deleted directly");
    Check(editor.selected() < editor.draft().profiles.size(), "the selection stays in range after a delete");
}

void TestBrokenBindings() {
    ProfileConfiguration broken = profiles::Defaults();
    broken.bindings.selectionProfileId = "missing-profile";
    Editor editor(broken);
    const std::vector<ProfileEntry> entries = editor.brokenBindings();
    Check(entries.size() == 1 && entries[0] == ProfileEntry::Selection, "a dangling binding is reported");
    Check(!editor.validate().ok, "a configuration with a dangling binding does not validate");
    editor.setBinding(ProfileEntry::Selection, "default-read");
    Check(editor.brokenBindings().empty() && editor.validate().ok, "rebinding repairs it");
}

void TestValidationLimits() {
    Editor editor(profiles::Defaults());
    editor.setName("");
    Check(!editor.validate().ok, "an empty name does not validate");
    editor.setName(std::string(profiles::MaxNameUtf16 + 1, 'n'));
    Check(!editor.validate().ok, "an over-long name does not validate");
    editor.setName("Fine");
    editor.setPromptMode(PromptMode::Custom);
    editor.setSystemPrompt(" \t\r\n ");
    Check(!editor.validate().ok, "a blank custom prompt does not validate");
    const std::string huge(profiles::MaxPromptUtf16 + 1, 'p');
    editor.setSystemPrompt(huge);
    Check(!editor.validate().ok, "an over-long custom prompt does not validate");
    Check(editor.current().systemPrompt.size() == huge.size(), "an over-long prompt is never silently truncated");
    editor.setSystemPrompt("Translate it.");
    editor.setBackend(TranslationBackend::OpenAI);
    editor.setModel("");
    Check(!editor.validate().ok, "a remote backend needs a model before saving");
}

void TestPreview() {
    Editor editor(profiles::Defaults());
    editor.setBackend(TranslationBackend::Ollama);
    editor.setPromptMode(PromptMode::Custom);
    editor.setSystemPrompt("Into {{target_language}} {{tone}}");
    const execution::PreparedRequest preview = editor.preview(ShortcutAction::NativePopup);
    Check(preview.ok() && preview.system == "Into Chinese {{tone}}", "the preview renders the custom prompt");
    Check(!preview.warnings.empty() && preview.warnings[0] == execution::kWarningUnknownPlaceholder,
          "the preview reports unknown placeholders");
    Check(preview.user == "The quick brown fox jumps over the lazy dog.", "the preview uses the fixed sample text");
    Check(preview.finalUrl.empty() && preview.cacheIdentity.empty(), "the preview needs no endpoint or key");
    editor.setBackend(TranslationBackend::BuiltIn);
    Check(editor.preview().status == execution::PrepareStatus::CapabilityRejected,
          "the preview explains that BuiltIn cannot run a custom prompt");
}

void TestDiscardAndSave() {
    Editor editor(profiles::Defaults());
    editor.select(1);
    editor.setName("Unsaved");
    editor.discard();
    Check(!editor.isDirty() && editor.draft() == editor.baseline(), "discard drops every unsaved edit");
    Check(editor.current().id == "default-rewrite", "discard keeps the selection on the same profile");
    editor.add("temporary");
    editor.discard();
    Check(editor.selected() < editor.draft().profiles.size() && editor.draft().profiles.size() == 2,
          "discarding an added profile moves the selection back into range");
    editor.setName("Saved name");
    ProfileConfiguration committed = editor.draft();
    editor.markSaved(committed);
    Check(!editor.isDirty() && editor.baseline() == committed && editor.current().name == "Saved name",
          "after a save the committed configuration is the new baseline");
}

} // namespace

void RunProfileEditorTests() {
    TestStartsClean();
    TestEditsAreExactAndIsolated();
    TestModesAndBackendKeepDrafts();
    TestSelectionKeepsEdits();
    TestAddAndDuplicate();
    TestDeleteRules();
    TestBrokenBindings();
    TestValidationLimits();
    TestPreview();
    TestDiscardAndSave();
}

} // namespace execution_test
