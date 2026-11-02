// AccessibilityProbe — checks how a running TypeTide window exposes its controls through native
// UI Automation (UIAutomationCore, the API screen readers such as Narrator use): every control's
// role, its accessible name (for edits and combo boxes, the label Windows associates with it),
// keyboard reachability of enabled controls, value access for text fields, password masking and
// the profile list's items. Reads only; it never changes a control.
//
// Usage: AccessibilityProbe editor <pid>      (the profile editor, in custom-prompt mode)
//        AccessibilityProbe settings <pid>    (the Settings window; switches to its Translation page)
// Exit codes: 0 = all checks passed, 1 = failures, 2 = the window or UI Automation was unavailable.
#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const std::wstring& name, const std::wstring& detail = {}) {
    if (condition) {
        std::wprintf(L"  ok   %ls\n", name.c_str());
    } else {
        ++g_failures;
        std::wprintf(L"  FAIL %ls%ls%ls\n", name.c_str(), detail.empty() ? L"" : L" -- ", detail.c_str());
    }
}

struct Spec {
    int id;
    CONTROLTYPEID type;
    const wchar_t* name;  // the accessible name must contain this (case-insensitive)
    bool value = false;   // a text field: the Value pattern must be available
    bool password = false;
};

struct FindData {
    DWORD pid;
    const wchar_t* cls;
    HWND found;
};

BOOL CALLBACK FindTop(HWND hwnd, LPARAM lp) {
    auto* data = reinterpret_cast<FindData*>(lp);
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    wchar_t cls[128]{};
    GetClassNameW(hwnd, cls, 128);
    if (owner == data->pid && IsWindowVisible(hwnd) && wcscmp(cls, data->cls) == 0) {
        data->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND WaitForWindow(DWORD pid, const wchar_t* cls) {
    for (int i = 0; i < 75; ++i) {
        FindData data{pid, cls, nullptr};
        EnumWindows(FindTop, reinterpret_cast<LPARAM>(&data));
        if (data.found) return data.found;
        Sleep(200);
    }
    return nullptr;
}

const wchar_t* TypeName(CONTROLTYPEID type) {
    switch (type) {
    case UIA_ButtonControlTypeId: return L"Button";
    case UIA_CheckBoxControlTypeId: return L"CheckBox";
    case UIA_ComboBoxControlTypeId: return L"ComboBox";
    case UIA_EditControlTypeId: return L"Edit";
    case UIA_ListControlTypeId: return L"List";
    case UIA_ListItemControlTypeId: return L"ListItem";
    case UIA_PaneControlTypeId: return L"Pane";
    case UIA_RadioButtonControlTypeId: return L"RadioButton";
    case UIA_TextControlTypeId: return L"Text";
    case UIA_WindowControlTypeId: return L"Window";
    default: return L"(other)";
    }
}

std::wstring Lower(std::wstring text) {
    CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    return text;
}

std::wstring NameOf(IUIAutomationElement* element) {
    BSTR name = nullptr;
    std::wstring result;
    if (SUCCEEDED(element->get_CurrentName(&name)) && name) result.assign(name, SysStringLen(name));
    SysFreeString(name);
    return result;
}

void CheckControl(IUIAutomation* automation, HWND window, const Spec& spec) {
    const std::wstring label = std::wstring(L"[") + std::to_wstring(spec.id) + L"] " + spec.name;
    HWND child = GetDlgItem(window, spec.id);
    IUIAutomationElement* element = nullptr;
    if (!child || FAILED(automation->ElementFromHandle(child, &element)) || !element) {
        Check(false, label + L": the control is exposed to UI Automation", L"not found");
        return;
    }
    CONTROLTYPEID type = 0;
    element->get_CurrentControlType(&type);
    const std::wstring name = NameOf(element);
    Check(type == spec.type, label + L": role is " + TypeName(spec.type), std::wstring(L"got ") + TypeName(type));
    Check(Lower(name).find(Lower(spec.name)) != std::wstring::npos,
          label + L": accessible name names the control", L"got '" + name + L"'");
    BOOL enabled = FALSE, focusable = FALSE;
    element->get_CurrentIsEnabled(&enabled);
    element->get_CurrentIsKeyboardFocusable(&focusable);
    if (spec.type != UIA_TextControlTypeId && enabled)
        Check(focusable != FALSE, label + L": reachable with the keyboard");
    if (spec.value) {
        IUnknown* pattern = nullptr;
        const bool hasValue = SUCCEEDED(element->GetCurrentPattern(UIA_ValuePatternId, &pattern)) && pattern;
        Check(hasValue, label + L": exposes its text (Value pattern)");
        if (pattern) pattern->Release();
    }
    if (spec.password) {
        BOOL isPassword = FALSE;
        element->get_CurrentIsPassword(&isPassword);
        Check(isPassword != FALSE, label + L": is marked as a password field");
    }
    element->Release();
}

void CheckListItems(IUIAutomation* automation, HWND window, int id) {
    HWND list = GetDlgItem(window, id);
    IUIAutomationElement* element = nullptr;
    IUIAutomationCondition* condition = nullptr;
    IUIAutomationElementArray* items = nullptr;
    int count = 0;
    VARIANT type;
    type.vt = VT_I4;
    type.lVal = UIA_ListItemControlTypeId;
    if (list && SUCCEEDED(automation->ElementFromHandle(list, &element)) && element &&
        SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &condition)) &&
        SUCCEEDED(element->FindAll(TreeScope_Children, condition, &items)) && items)
        items->get_Length(&count);
    const int expected = list ? static_cast<int>(SendMessageW(list, LB_GETCOUNT, 0, 0)) : -1;
    Check(expected > 0 && count == expected, L"the profile list exposes one list item per profile",
          L"items " + std::to_wstring(count) + L", profiles " + std::to_wstring(expected));
    if (items) items->Release();
    if (condition) condition->Release();
    if (element) element->Release();
}

// Control IDs of TranslationProfilesWindow.
const Spec kEditor[] = {
    {3001, UIA_ListControlTypeId, L"Profiles"},
    {3002, UIA_ButtonControlTypeId, L"Add"},
    {3003, UIA_ButtonControlTypeId, L"Duplicate"},
    {3004, UIA_ButtonControlTypeId, L"Delete"},
    {3010, UIA_EditControlTypeId, L"Name", true},
    {3011, UIA_ComboBoxControlTypeId, L"Native language"},
    {3012, UIA_ComboBoxControlTypeId, L"Foreign language"},
    {3013, UIA_ComboBoxControlTypeId, L"Direction"},
    {3014, UIA_ComboBoxControlTypeId, L"Backend"},
    {3015, UIA_EditControlTypeId, L"Model", true},
    {3020, UIA_RadioButtonControlTypeId, L"Preset style"},
    {3021, UIA_RadioButtonControlTypeId, L"Custom system prompt"},
    {3022, UIA_ComboBoxControlTypeId, L"Style"},
    {3023, UIA_EditControlTypeId, L"Custom system prompt", true},
    {3025, UIA_EditControlTypeId, L"Effective request", true},
    {3030, UIA_ComboBoxControlTypeId, L"Shortcut 1"},
    {3031, UIA_ComboBoxControlTypeId, L"Shortcut 2"},
    {3032, UIA_ComboBoxControlTypeId, L"Selection"},
    {3040, UIA_ButtonControlTypeId, L"Test fixed text"},
    {3041, UIA_ButtonControlTypeId, L"Save"},
    {3042, UIA_ButtonControlTypeId, L"Close"},
};

// SettingsWindow's Translation page (fixed control IDs).
const Spec kSettings[] = {
    {1901, UIA_TextControlTypeId, L"Shortcut 1"},
    {1902, UIA_TextControlTypeId, L"Shortcut 2"},
    {1903, UIA_TextControlTypeId, L"Selection"},
    {1904, UIA_ButtonControlTypeId, L"Manage profiles"},
    {1905, UIA_ButtonControlTypeId, L"Test bound models"},
    {1921, UIA_ButtonControlTypeId, L"model"},  // "Download model · 2.49 GB" / "Model ready"
    {1924, UIA_ButtonControlTypeId, L"Refresh"},
    {1927, UIA_ComboBoxControlTypeId, L"Cloud provider"},
    {1929, UIA_EditControlTypeId, L"Base URL", true},
    {1931, UIA_EditControlTypeId, L"API key", false, true},
};

constexpr int kSettingsNav = 1000;
constexpr int kTranslationPage = 1;

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || (wcscmp(argv[1], L"editor") != 0 && wcscmp(argv[1], L"settings") != 0)) {
        std::wprintf(L"usage: AccessibilityProbe editor|settings <pid>\n");
        return 2;
    }
    const bool editor = wcscmp(argv[1], L"editor") == 0;
    const DWORD pid = static_cast<DWORD>(_wtoi(argv[2]));
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    IUIAutomation* automation = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)))) {
        std::wprintf(L"UI Automation is not available\n");
        return 2;
    }
    HWND window = WaitForWindow(pid, editor ? L"TypeTideProfiles" : L"TypeTideSettings");
    if (!window) {
        std::wprintf(L"window not found\n");
        automation->Release();
        return 2;
    }
    std::wprintf(L"[%ls accessibility (native UI Automation)]\n", editor ? L"profile editor" : L"settings window");
    if (editor) {
        for (const Spec& spec : kEditor) CheckControl(automation, window, spec);
        CheckListItems(automation, window, 3001);
    } else {
        // Show the Translation page the way a click on the navigation list would.
        HWND nav = GetDlgItem(window, kSettingsNav);
        SendMessageW(nav, LB_SETCURSEL, kTranslationPage, 0);
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(kSettingsNav, LBN_SELCHANGE), reinterpret_cast<LPARAM>(nav));
        Sleep(300);
        for (const Spec& spec : kSettings) CheckControl(automation, window, spec);
    }
    automation->Release();
    CoUninitialize();
    std::wprintf(g_failures ? L"%d FAILURE(S)\n" : L"all accessibility checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
