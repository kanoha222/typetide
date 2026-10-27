#include "TranslationProfilesWindow.h"
#include "EndpointPolicy.h"
#include "ProfileBinding.h"
#include "ProfileEditor.h"
#include "Settings.h"
#include "Translator.h"
#include "Util.h"
#include "resource.h"
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace profileswindow {

namespace {

constexpr wchar_t kClassName[] = L"TypeTideProfiles";
constexpr wchar_t kTitle[] = L"TypeTide — Translation profiles";
constexpr UINT kMsgTestDone = WM_APP + 40;       // wParam = window lifetime, no heap payload
constexpr UINT kMsgConfirmClose = WM_APP + 41;   // modal UI runs outside the UIA click handler
constexpr UINT kMsgDeleteSelected = WM_APP + 42;
constexpr UINT kMsgSelect = WM_APP + 43;
constexpr UINT_PTR kTestTimer = 1;
constexpr UINT kTestTimerMs = 250;
constexpr UINT kMenuReplacementBase = 100;

// Control IDs double as UIA AutomationIds (windows/scripts/test-profiles-e2e.ps1).
enum Id : int {
    kList = 3001, kAdd = 3002, kDuplicate = 3003, kDelete = 3004,
    kName = 3010, kNative = 3011, kForeign = 3012, kDirection = 3013, kBackend = 3014, kModel = 3015,
    kRoute = 3016,
    kPresetMode = 3020, kCustomMode = 3021, kStyle = 3022, kPrompt = 3023, kPromptHelp = 3024, kPreview = 3025,
    kBindShortcut1 = 3030, kBindShortcut2 = 3031, kBindSelection = 3032,
    kTest = 3040, kSave = 3041, kClose = 3042, kStatus = 3043,
};

struct BindingControl {
    int id;
    ProfileEntry entry;
};
constexpr BindingControl kBindings[] = {
    {kBindShortcut1, ProfileEntry::Shortcut1},
    {kBindShortcut2, ProfileEntry::Shortcut2},
    {kBindSelection, ProfileEntry::Selection},
};

struct TestResult {
    uint64_t token = 0;
    uint64_t epoch = 0;
    bool done = false;  // false while deltas are still arriving
    bool ok = false;
    std::wstring text;
};
// Workers own this mailbox, never State. Closing clears the target and pending ownership
// under the same lock used to post. Queued messages carry no owning pointer to leak/reuse.
struct TestMailbox {
    std::mutex mutex;
    HWND target = nullptr;
    uint64_t token = 0;
    std::optional<TestResult> pending;
};
struct State {
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
    std::unique_ptr<profileeditor::Editor> editor;
    bool loading = true;
    std::wstring shownPrompt, shownName, shownModel; // actual control readback, not model text
    bool promptDisplayLossy = false;
    std::map<std::string, unsigned> rejectedInputs; // sticky per profile; clear the field to recover
    std::shared_ptr<TestMailbox> mailbox;
    int modalDepth = 0;  // > 0 while a MessageBox or popup menu runs its own message loop
    uint64_t testRequest = 0;
    uint64_t testEpoch = 0;  // never reset: a closed window's results can't match a new one
};

State g;

int px(int v) { return MulDiv(v, (int)g.dpi, 96); }
HWND item(int id) { return GetDlgItem(g.hwnd, id); }

HWND make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h),
                                   g.hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, (WPARAM)g.font, TRUE);
    return control;
}

void label(const wchar_t* text, int x, int y, int w) { make(L"STATIC", text, SS_LEFT, x, y, w, 20, -1); }

std::wstring windowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(copied));
    return text;
}

/// A multiline EDIT needs CRLF line breaks to display them.
std::wstring toCrlf(const std::wstring& text) {
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r')) out += L'\r';
        out += text[i];
    }
    return out;
}

std::wstring displayName(const TranslationProfile& profile) {
    return profile.name.empty() ? std::wstring(L"(unnamed)") : util::Widen(profile.name);
}

/// Menu text: "&" would otherwise become a mnemonic.
std::wstring menuText(const std::wstring& text) {
    std::wstring out;
    for (wchar_t c : text) {
        out += c;
        if (c == L'&') out += L'&';
    }
    return out;
}

void setStatus(const std::wstring& text) { SetWindowTextW(item(kStatus), text.c_str()); }

int modalMessage(const wchar_t* text, UINT type) {
    ++g.modalDepth;
    const int choice = MessageBoxW(g.hwnd, text, L"TypeTide", type);
    --g.modalDepth;
    return choice;
}

void invalidateTest() {
    if (g.testRequest) translator::Cancel(std::exchange(g.testRequest, 0));
    KillTimer(g.hwnd, kTestTimer);
    if (!g.mailbox) return;
    std::lock_guard lock(g.mailbox->mutex);
    ++g.mailbox->token;
    g.mailbox->pending.reset();
}

// Only a truncated prompt paste is sticky: its loss is invisible in a long text. An over-long
// name or model stays visible and simply fails validation.
unsigned inputBit(int id) { return id == kPrompt ? 1u : 0u; }
bool hasInputIssue() { return !g.rejectedInputs.empty(); }

/// True (with an explanation) while a truncated prompt paste still has to be fixed.
bool blockedByRejectedInput() {
    if (!hasInputIssue()) return false;
    setStatus(L"Fix the truncated prompt first: clear it completely and re-enter it, or close and Discard.");
    return true;
}
bool dirty() { return g.editor->isDirty() || hasInputIssue(); }

// Never replace an invalid UTF-16 edit with WideCharToMultiByte's substitution character.
bool lossless(const std::wstring& text) { return util::Widen(util::Narrow(text)) == text; }
bool unsupportedPrompt(const std::wstring& text) {
    for (wchar_t c : text)
        if ((c < 32 && c != L'\r' && c != L'\n' && c != L'\t') || (c >= 127 && c <= 159)) return true;
    return false;
}

int comboIndex(int id) { return (int)SendMessageW(item(id), CB_GETCURSEL, 0, 0); }

void fillCombo(int id, const std::vector<std::wstring>& items, int selected) {
    HWND combo = item(id);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const std::wstring& text : items) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text.c_str());
    SendMessageW(combo, CB_SETCURSEL, selected, 0);
}

// ---------- layout ----------

void createListPane() {
    label(L"Profiles", 16, 12, 200);
    make(L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
         16, 34, 200, 456, kList);
    make(L"BUTTON", L"Add", WS_TABSTOP | BS_PUSHBUTTON, 16, 498, 62, 28, kAdd);
    make(L"BUTTON", L"Duplicate", WS_TABSTOP | BS_PUSHBUTTON, 82, 498, 76, 28, kDuplicate);
    make(L"BUTTON", L"Delete", WS_TABSTOP | BS_PUSHBUTTON, 162, 498, 54, 28, kDelete);
}

void createFieldPane() {
    const int x = 236, cx = 350;
    label(L"Name", x, 13, 110);
    make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, cx, 10, 300, 24, kName);
    label(L"Native language", x, 47, 110);
    make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, cx, 44, 260, 300, kNative);
    label(L"Foreign language", x, 81, 110);
    make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, cx, 78, 260, 300, kForeign);
    label(L"Direction", x, 115, 110);
    make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, cx, 112, 260, 200, kDirection);
    label(L"Backend", x, 149, 110);
    make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, cx, 146, 260, 200, kBackend);
    label(L"Model", x, 183, 110);
    make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, cx, 180, 300, 24, kModel);
    make(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, x, 210, 648, 20, kRoute);
    SendMessageW(item(kName), EM_SETLIMITTEXT, profiles::MaxNameUtf16 + 1, 0);
    SendMessageW(item(kModel), EM_SETLIMITTEXT, profiles::MaxModelBytes + 1, 0);
}

void createPromptPane() {
    const int x = 236;
    make(L"BUTTON", L"Preset style", WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, x, 236, 120, 22, kPresetMode);
    make(L"BUTTON", L"Custom system prompt", BS_AUTORADIOBUTTON, x + 130, 236, 200, 22, kCustomMode);
    label(L"Style", x, 269, 110);
    make(L"COMBOBOX", L"", WS_TABSTOP | WS_GROUP | CBS_DROPDOWNLIST, 350, 266, 260, 200, kStyle);
    label(L"Custom system prompt (replaces the default instructions entirely)", x, 300, 648);
    make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
         x, 320, 648, 150, kPrompt);
    make(L"STATIC",
         L"Variables: {{source_language}} and {{target_language}} (exact, case-sensitive). Anything else "
         L"stays literal. Text is a separate user message. Test uses fixed text, never selection or clipboard.",
         SS_LEFT, x, 474, 648, 34, kPromptHelp);
    label(L"Effective request (fixed sample text)", x, 512, 648);
    make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
         x, 532, 648, 64, kPreview);
    // Allow CRLF expansion of a valid all-LF model and validation of modest oversize input.
    // A larger truncated paste sets a STICKY rejection, not a transient warning.
    SendMessageW(item(kPrompt), EM_SETLIMITTEXT, 2 * profiles::MaxPromptUtf16 + 1, 0);
}

void createFooter() {
    const Settings& s = Settings::shared();
    label((L"Shortcut 1  (" + s.readShortcut.displayString() + L")").c_str(), 16, 606, 280);
    label((L"Shortcut 2  (" + s.rewriteShortcut.displayString() + L")").c_str(), 312, 606, 280);
    label(L"Selection icon / auto-translate", 608, 606, 280);
    for (int i = 0; i < 3; ++i)
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 16 + 296 * i, 626, 280, 300,
             kBindings[i].id);
    make(L"BUTTON", L"Test fixed text", WS_TABSTOP | BS_PUSHBUTTON, 16, 666, 130, 30, kTest);
    make(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 156, 668, 540, 36, kStatus);
    make(L"BUTTON", L"Save", WS_TABSTOP | BS_PUSHBUTTON, 712, 666, 80, 30, kSave);
    make(L"BUTTON", L"Close", WS_TABSTOP | BS_PUSHBUTTON, 800, 666, 84, 30, kClose);
}

// ---------- model → controls ----------

void refreshList() {
    HWND list = item(kList);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const TranslationProfile& profile : g.editor->draft().profiles)
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)displayName(profile).c_str());
    SendMessageW(list, LB_SETCURSEL, g.editor->selected(), 0);
}

void refreshBindings() {
    const ProfileConfiguration& config = g.editor->draft();
    std::vector<std::wstring> names;
    for (const TranslationProfile& profile : config.profiles) names.push_back(displayName(profile));
    for (const BindingControl& binding : kBindings) {
        const TranslationProfile* bound = profiles::BoundProfile(config, binding.entry);
        std::vector<std::wstring> items = names;
        int selected = static_cast<int>(items.size());
        for (size_t i = 0; bound && i < config.profiles.size(); ++i)
            if (config.profiles[i].id == bound->id) selected = static_cast<int>(i);
        if (!bound) items.push_back(L"(missing profile: choose one)");
        fillCombo(binding.id, items, selected);
    }
}

std::wstring previewText() {
    const execution::PreparedRequest request = g.editor->preview();
    if (!request.ok()) return execution::PrepareErrorMessage(request.status, request.backend);
    std::wstring text;
    if (!request.unknownTokens.empty()) {
        text = L"Unknown placeholders stay literal:";
        for (const std::string& token : request.unknownTokens) text += L" " + util::Widen(token);
        text += L"\r\n\r\n";
    }
    if (request.system.empty()) return text + L"Prompt:\r\n" + toCrlf(util::Widen(request.user));
    return text + L"System prompt:\r\n" + toCrlf(util::Widen(request.system)) +
           L"\r\n\r\nUser message: the selected text, unchanged.";
}

std::wstring routeText(const TranslationProfile& profile) {
    switch (profile.backend) {
    case TranslationBackend::BuiltIn:
        return L"Local: runs with the built-in model on this PC.";
    case TranslationBackend::Ollama: {
        const std::wstring host = util::Widen(Settings::shared().committedData().ollamaHost);
        return execution::ClassifyEndpoint(host).kind == execution::EndpointKind::LoopbackHttp
                   ? L"Local: runs on Ollama on this PC."
                   : L"Network: selected text is sent to the configured Ollama host.";
    }
    case TranslationBackend::OpenAI:
        return L"Cloud: selected text is sent to the configured API endpoint.";
    }
    return {};
}

void refreshDerived() {
    SetWindowTextW(item(kPreview), previewText().c_str());
    SetWindowTextW(item(kRoute), routeText(g.editor->current()).c_str());
    refreshBindings();
    SetWindowTextW(g.hwnd, dirty() ? (std::wstring(kTitle) + L"  • unsaved").c_str() : kTitle);
}

void fillLanguageCombos(const TranslationProfile& profile) {
    std::vector<std::wstring> languages;
    int native = -1, foreign = -1;
    for (size_t i = 0; i < std::size(lang::All); ++i) {
        languages.push_back(lang::DisplayName(lang::All[i]));
        if (lang::All[i] == profile.nativeLanguage) native = static_cast<int>(i);
        if (lang::All[i] == profile.foreignLanguage) foreign = static_cast<int>(i);
    }
    fillCombo(kNative, languages, native);
    fillCombo(kForeign, languages, foreign);
}

void loadFields() {
    g.loading = true;
    const TranslationProfile& profile = g.editor->current();
    SetWindowTextW(item(kName), util::Widen(profile.name).c_str());
    g.shownName = windowText(item(kName));
    fillLanguageCombos(profile);
    fillCombo(kDirection, {L"Automatic (detect and swap)", L"Native → foreign", L"Foreign → native"},
              static_cast<int>(profile.direction));
    fillCombo(kBackend, {L"Built-in (offline)", L"Ollama", L"OpenAI-compatible API (cloud)"},
              static_cast<int>(profile.backend));
    SetWindowTextW(item(kModel), util::Widen(profile.model).c_str());
    g.shownModel = windowText(item(kModel));
    const bool custom = profile.promptMode == PromptMode::Custom;
    CheckRadioButton(g.hwnd, kPresetMode, kCustomMode, custom ? kCustomMode : kPresetMode);
    fillCombo(kStyle, {lang::StyleDisplayName(RewriteStyle::Faithful), lang::StyleDisplayName(RewriteStyle::Formal),
                       lang::StyleDisplayName(RewriteStyle::Casual), lang::StyleDisplayName(RewriteStyle::Polished)},
              static_cast<int>(profile.presetStyle));
    const std::wstring display = toCrlf(util::Widen(profile.systemPrompt));
    SetWindowTextW(item(kPrompt), display.c_str());
    g.shownPrompt = windowText(item(kPrompt));
    g.promptDisplayLossy = display != g.shownPrompt || unsupportedPrompt(display);
    EnableWindow(item(kStyle), !custom);
    EnableWindow(item(kPrompt), custom && !g.promptDisplayLossy);
    g.loading = false;
    refreshDerived();
    if (g.promptDisplayLossy)
        setStatus(L"Prompt contains content this control cannot edit losslessly. Original preserved unchanged; editing is disabled.");
}

void reloadAll() {
    refreshList();
    loadFields();
}

// ---------- controls → model ----------

void syncPromptMode() {
    const PromptMode mode =
        IsDlgButtonChecked(g.hwnd, kCustomMode) == BST_CHECKED ? PromptMode::Custom : PromptMode::Preset;
    if (mode != g.editor->current().promptMode) {
        invalidateTest();
        g.editor->setPromptMode(mode);  // keeps both the preset style and the custom text
    }
    EnableWindow(item(kStyle), mode == PromptMode::Preset);
    EnableWindow(item(kPrompt), mode == PromptMode::Custom && !g.promptDisplayLossy);
}

/// Some edits arrive without EN_CHANGE / BN_CLICKED (a multiline EDIT set via WM_SETTEXT, a radio
/// checked by assistive technology), so every decision point re-reads the controls first.
void pullEdits() {
    syncPromptMode();
    const std::wstring name = windowText(item(kName)), model = windowText(item(kModel));
    const std::wstring prompt = windowText(item(kPrompt));
    if (name != g.shownName && lossless(name)) {
        invalidateTest();
        g.editor->setName(util::Narrow(name));
        g.shownName = name;
    }
    if (model != g.shownModel && lossless(model)) {
        invalidateTest();
        g.editor->setModel(util::Narrow(model));
        g.shownModel = model;
    }
    if (prompt != g.shownPrompt) {
        invalidateTest();
        if (g.promptDisplayLossy) g.rejectedInputs[g.editor->current().id] |= inputBit(kPrompt);
        else if (lossless(prompt)) {
            g.editor->setSystemPrompt(util::Narrow(prompt));
            g.shownPrompt = prompt;
        }
    }
    auto rejected = g.rejectedInputs.find(g.editor->current().id);
    if (rejected == g.rejectedInputs.end()) return;
    unsigned bits = rejected->second;
    if (prompt.empty() && !g.promptDisplayLossy) bits &= ~inputBit(kPrompt);
    if (bits) rejected->second = bits;
    else g.rejectedInputs.erase(rejected);
}

/// Validates each profile first so the message can name the one that needs fixing.
std::optional<std::wstring> validationError() {
    if (hasInputIssue())
        return L"Input was rejected or truncated by a text control. Clear the affected field completely and re-enter it, or Discard. Deleting one character is not enough.";
    for (int id : {kName, kModel, kPrompt})
        if (!lossless(windowText(item(id)))) return L"Text contains unsupported UTF-16. Correct it before saving.";
    for (const TranslationProfile& profile : g.editor->draft().profiles) {
        const profiles::ValidationResult result = profiles::Validate(profile);
        if (!result.ok) return L"“" + displayName(profile) + L"”: " + util::Widen(result.error);
    }
    const profiles::ValidationResult result = g.editor->validate();
    if (!result.ok) return util::Widen(result.error);
    return std::nullopt;
}

bool save() {
    pullEdits();
    if (!dirty()) {
        setStatus(L"No changes to save.");
        return true;
    }
    if (const std::optional<std::wstring> error = validationError()) {
        setStatus(L"Not saved: " + *error);
        return false;
    }
    Settings& settings = Settings::shared();
    std::string error;
    if (!profilebinding::CommitConfiguration(settings, g.editor->draft(), &error, &g.editor->baseline())) {
        setStatus(L"Not saved: " + util::Widen(error));
        return false;
    }
    invalidateTest();
    g.editor->markSaved(settings.committedData().profileConfig);
    reloadAll();
    const std::string& warning = settings.storageWarning();
    setStatus(warning.empty() ? std::wstring(L"Saved.") : L"Saved. " + util::Widen(warning));
    return true;
}

/// Save / Discard / Cancel for unsaved edits. True when the caller may proceed.
bool resolveUnsaved() {
    pullEdits();
    if (!dirty()) return true;
    const int choice = modalMessage(L"Save changes to your translation profiles?", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (!g.editor) return false;
    if (choice == IDYES) return save();
    if (choice == IDNO) {
        invalidateTest();
        g.rejectedInputs.clear();
        g.editor->markSaved(Settings::shared().committedData().profileConfig);
        reloadAll();
        return true;
    }
    return false;
}

void onListSelection(int index) {
    if (index < 0 || static_cast<size_t>(index) >= g.editor->draft().profiles.size() ||
        static_cast<size_t>(index) == g.editor->selected()) return;
    pullEdits();
    if (blockedByRejectedInput()) {
        SendMessageW(item(kList), LB_SETCURSEL, g.editor->selected(), 0);
        return;
    }
    invalidateTest();
    const std::string target = g.editor->draft().profiles[static_cast<size_t>(index)].id;
    if (!resolveUnsaved()) {
        SendMessageW(item(kList), LB_SETCURSEL, g.editor->selected(), 0);
        return;
    }
    // Save or discard may have changed the list; select the same profile by id.
    const std::vector<TranslationProfile>& profiles = g.editor->draft().profiles;
    for (size_t i = 0; i < profiles.size(); ++i)
        if (profiles[i].id == target) g.editor->select(i);
    reloadAll();
}

void appendProfile(bool copy) {
    pullEdits();
    if (blockedByRejectedInput()) return;
    invalidateTest();
    const std::string id = profiles::NewId();
    if (!(copy ? g.editor->duplicate(id) : g.editor->add(id))) {
        setStatus(L"Profiles are limited to 64.");
        return;
    }
    reloadAll();
    setStatus(L"Added. Save to keep it.");
    SetFocus(item(kName));
}

std::optional<std::string> chooseReplacement() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"This profile is in use. Move its bindings to:");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const std::vector<TranslationProfile>& profiles = g.editor->draft().profiles;
    for (UINT i = 0; i < profiles.size(); ++i)
        if (i != g.editor->selected())
            AppendMenuW(menu, MF_STRING, kMenuReplacementBase + i, menuText(displayName(profiles[i])).c_str());
    RECT r{};
    GetWindowRect(item(kDelete), &r);
    SetForegroundWindow(g.hwnd);  // required for the menu to dismiss correctly
    ++g.modalDepth;
    const UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, r.left, r.bottom, 0, g.hwnd, nullptr);
    --g.modalDepth;
    DestroyMenu(menu);
    if (!g.editor) return std::nullopt;
    if (cmd < kMenuReplacementBase || cmd - kMenuReplacementBase >= profiles.size()) return std::nullopt;
    return profiles[cmd - kMenuReplacementBase].id;
}

void deleteSelected() {
    pullEdits();
    const profileeditor::DeleteCheck check = g.editor->canDelete();
    if (!check.allowed) {
        setStatus(L"The last profile can't be deleted.");
        return;
    }
    std::string replacement;
    if (!check.boundBy.empty()) {
        const std::optional<std::string> chosen = chooseReplacement();
        if (!chosen) return;
        replacement = *chosen;
    } else if (modalMessage(L"Delete this profile?", MB_YESNO | MB_ICONQUESTION) != IDYES || !g.editor) {
        return;
    }
    invalidateTest();
    const std::string deletedId = g.editor->current().id;
    if (!g.editor->remove(replacement)) {
        setStatus(L"The profile could not be deleted.");
        return;
    }
    g.rejectedInputs.erase(deletedId);
    reloadAll();
    setStatus(L"Deleted. Save to keep this change.");
}

void runTest() {
    pullEdits();
    invalidateTest();
    const uint64_t lifetime = ++g.testEpoch;
    uint64_t token = 0;
    {
        std::lock_guard lock(g.mailbox->mutex);
        token = ++g.mailbox->token;
        g.mailbox->pending = TestResult{token, lifetime, false, false, {}};
    }
    const execution::ContextPtr context = execution::Freeze(
        g.editor->current(), ShortcutAction::SmartPopup, translator::ConnectionFrom(Settings::shared().snapshot()));
    auto mailbox = g.mailbox;
    HWND hwnd = g.hwnd;
    setStatus(L"Testing with fixed text…");
    SetTimer(g.hwnd, kTestTimer, kTestTimerMs, nullptr);
    g.testRequest = translator::Stream(
        context, L"Hello! This is a TypeTide profile test.", std::nullopt,
        [mailbox, token](uint64_t, const std::wstring& delta) {
            std::lock_guard lock(mailbox->mutex);
            if (mailbox->token == token && mailbox->pending) mailbox->pending->text += delta;
        },
        [mailbox, hwnd, token, lifetime, context](uint64_t, bool ok, const std::wstring& error) {
            std::lock_guard lock(mailbox->mutex);
            if (mailbox->token != token || !mailbox->target || !mailbox->pending) return;
            TestResult& result = *mailbox->pending;
            const std::optional<std::wstring> out =
                ok ? execution::DeliveredOutput(context->profile, result.text) : std::nullopt;
            result.ok = out.has_value();
            result.text = out ? *out : (ok ? std::wstring(L"Empty translation") : error);
            result.done = true;
            PostMessageW(hwnd, kMsgTestDone, static_cast<WPARAM>(lifetime), 0);  // the timer is the fallback
        });
}

void showTestResult() {
    std::lock_guard lock(g.mailbox->mutex);
    if (!g.mailbox->pending || !g.mailbox->pending->done || g.mailbox->pending->epoch != g.testEpoch) return;
    const TestResult& result = *g.mailbox->pending;
    std::wstring line = result.text.substr(0, 160);
    for (wchar_t& c : line)
        if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    setStatus((result.ok ? L"Test passed: " : L"Test failed: ") + line);
    KillTimer(g.hwnd, kTestTimer);
}

void onBinding(const BindingControl& binding) {
    const int index = comboIndex(binding.id);
    const std::vector<TranslationProfile>& profiles = g.editor->draft().profiles;
    if (index < 0 || static_cast<size_t>(index) >= profiles.size()) return;
    invalidateTest();
    g.editor->setBinding(binding.entry, profiles[static_cast<size_t>(index)].id);
    refreshDerived();
}

void onPromptMode() {
    pullEdits();  // syncPromptMode() reads the radio button that was just checked
    refreshDerived();
}

void onFieldCommand(int id) {
    invalidateTest();
    switch (id) {
    case kNative:
    case kForeign: {
        const int ni = comboIndex(kNative), fi = comboIndex(kForeign);
        if (ni >= 0 && ni < (int)std::size(lang::All) && fi >= 0 && fi < (int)std::size(lang::All))
            g.editor->setLanguages(lang::All[ni], lang::All[fi]);
        break;
    }
    case kDirection: {
        const int i = comboIndex(kDirection);
        if (i >= 0 && i <= 2) g.editor->setDirection(static_cast<TranslationDirection>(i));
        break;
    }
    case kBackend: {
        const int i = comboIndex(kBackend);
        if (i >= 0 && i <= 2) g.editor->setBackend(static_cast<TranslationBackend>(i));
        break;
    }
    case kStyle: {
        const int i = comboIndex(kStyle);
        if (i >= 0 && i <= 3) g.editor->setPresetStyle(static_cast<RewriteStyle>(i));
        break;
    }
    default: return;
    }
    refreshDerived();
}

void onEditChange(int id) {
    if (g.loading) return;
    if (id == kName) {
        pullEdits();
        refreshList();
    } else if (id == kModel || id == kPrompt) {
        pullEdits();
    }
    refreshDerived();
}

void onCommand(int id, int code) {
    if (!g.editor) return;
    if (code == EN_MAXTEXT) {
        if (id == kPrompt) {
            g.rejectedInputs[g.editor->current().id] |= inputBit(kPrompt);
            return setStatus(L"Prompt truncated by paste. Clear it completely and re-enter; deleting one character is not enough.");
        }
        if (id == kName) return setStatus(L"A profile name can have at most 80 characters.");
        if (id == kModel) return setStatus(L"The model name is too long.");
    }
    if (code == EN_CHANGE) return onEditChange(id);
    if (id == kList) {
        if (code == LBN_SELCHANGE)
            PostMessageW(g.hwnd, kMsgSelect, (WPARAM)SendMessageW(item(kList), LB_GETCURSEL, 0, 0), 0);
        return;
    }
    if (code == CBN_SELCHANGE) {
        for (const BindingControl& binding : kBindings)
            if (binding.id == id) return onBinding(binding);
        return onFieldCommand(id);
    }
    if (code != BN_CLICKED) return;
    switch (id) {
    case kAdd: appendProfile(false); break;
    case kDuplicate: appendProfile(true); break;
    case kDelete: PostMessageW(g.hwnd, kMsgDeleteSelected, 0, 0); break;
    case kPresetMode:
    case kCustomMode: onPromptMode(); break;
    case kTest: runTest(); break;
    case kSave: save(); break;
    case kClose:
    case IDCANCEL: PostMessageW(g.hwnd, kMsgConfirmClose, 0, 0); break;
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        onCommand(LOWORD(wp), HIWORD(wp));
        return 0;
    case WM_TIMER:
        if (wp == kTestTimer) showTestResult();
        return 0;
    case kMsgTestDone:
        if (static_cast<uint64_t>(wp) == g.testEpoch) showTestResult();
        return 0;
    case kMsgDeleteSelected:
        if (g.editor && !g.modalDepth) deleteSelected();
        return 0;
    case kMsgSelect:
        if (g.editor && !g.modalDepth) onListSelection(static_cast<int>(static_cast<INT_PTR>(wp)));
        else if (g.editor) SendMessageW(item(kList), LB_SETCURSEL, g.editor->selected(), 0);
        return 0;
    case WM_CLOSE:
    case kMsgConfirmClose:
        if (g.editor && !g.modalDepth && resolveUnsaved()) DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        invalidateTest();
        if (g.mailbox) {
            std::lock_guard lock(g.mailbox->mutex);
            g.mailbox->target = nullptr;
        }
        g.mailbox.reset();
        g.editor.reset();
        g.hwnd = nullptr;
        return 0;
    case WM_NCDESTROY:  // after every child control is gone
        if (g.font) DeleteObject(g.font);
        g.font = nullptr;
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerClass() {
    static bool registered = [] {
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APPICON));
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        return true;
    }();
    (void)registered;
}

} // namespace

void open(HWND /*appWindow*/) {
    if (g.hwnd) {
        ShowWindow(g.hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g.hwnd);
        return;
    }
    registerClass();
    g.dpi = GetDpiForSystem();
    g.editor = std::make_unique<profileeditor::Editor>(Settings::shared().committedData().profileConfig);
    g.mailbox = std::make_shared<TestMailbox>();
    g.loading = true;
    g.rejectedInputs.clear();
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wr{0, 0, px(900), px(712)};
    AdjustWindowRectEx(&wr, style, FALSE, WS_EX_CONTROLPARENT);
    g.hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, kClassName, kTitle, style, CW_USEDEFAULT, CW_USEDEFAULT,
                             wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, GetModuleHandleW(nullptr),
                             nullptr);
    if (!g.hwnd) {
        g.mailbox.reset();
        g.editor.reset();
        return;
    }
    {
        std::lock_guard lock(g.mailbox->mutex);
        g.mailbox->target = g.hwnd;
    }
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    createListPane();
    createFieldPane();
    createPromptPane();
    createFooter();
    reloadAll();
    if (!g.editor->brokenBindings().empty())
        setStatus(L"A binding points to a missing profile. Choose a profile for it and save.");
    ShowWindow(g.hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g.hwnd);
    SetFocus(item(kList));
}

bool handleDialogMessage(MSG* msg) {
    return g.hwnd && IsDialogMessageW(g.hwnd, msg);
}

} // namespace profileswindow
