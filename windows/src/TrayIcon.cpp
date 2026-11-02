#include "TrayIcon.h"
#include "Settings.h"
#include "Util.h"
#include "resource.h"
#include <string>
#include <vector>

namespace {
enum MenuId : UINT {
    kMenuUpdate = 1,
    kMenuToggle,
    kMenuReadHint,
    kMenuRewriteHint,
    kMenuSettings,
    kMenuProfiles,
    kMenuQuit,
    kMenuBindBase = 1000,  // + slot * kBindStride + profile index
};
constexpr UINT kBindStride = 100;  // > profiles::MaxProfiles

struct BindingSlot {
    ProfileEntry entry;
    std::wstring label;
};

/// Menu text for a free-form profile name: "&" would otherwise become a mnemonic.
std::wstring menuText(const std::string& utf8) {
    std::wstring text;
    for (wchar_t c : util::Widen(utf8)) {
        text += c;
        if (c == L'&') text += L'&';
    }
    return text;
}

void dispatchCommand(const TrayIcon& tray, UINT cmd, const std::vector<BindingSlot>& slots,
                     const ProfileConfiguration& config) {
    switch (cmd) {
    case kMenuUpdate:
        if (tray.onOpenReleases) tray.onOpenReleases();
        break;
    case kMenuToggle:
        if (tray.onToggleEnabled) tray.onToggleEnabled();
        break;
    case kMenuSettings:
        if (tray.onOpenSettings) tray.onOpenSettings();
        break;
    case kMenuProfiles:
        if (tray.onOpenProfiles) tray.onOpenProfiles();
        break;
    case kMenuQuit:
        if (tray.onQuit) tray.onQuit();
        break;
    default:
        if (cmd >= kMenuBindBase && tray.onBindProfile) {
            const UINT slot = (cmd - kMenuBindBase) / kBindStride;
            const UINT index = (cmd - kMenuBindBase) % kBindStride;
            if (slot < slots.size() && index < config.profiles.size())
                tray.onBindProfile(slots[slot].entry, config.profiles[index].id);
        }
        break;
    }
}

/// One submenu per physical entry, listing the committed profiles with the bound one checked.
void appendBindingMenus(HMENU menu, const std::vector<BindingSlot>& slots, const ProfileConfiguration& config) {
    for (UINT slot = 0; slot < slots.size(); ++slot) {
        HMENU sub = CreatePopupMenu();
        const TranslationProfile* bound = profiles::BoundProfile(config, slots[slot].entry);
        for (UINT i = 0; i < config.profiles.size() && i < kBindStride; ++i) {
            const TranslationProfile& profile = config.profiles[i];
            const UINT checked = bound && bound->id == profile.id ? MF_CHECKED : 0;
            AppendMenuW(sub, MF_STRING | checked, kMenuBindBase + slot * kBindStride + i,
                        menuText(profile.name).c_str());
        }
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, slots[slot].label.c_str());
    }
}

const wchar_t* actionName(ShortcutAction action) {
    switch (action) {
    case ShortcutAction::NativePopup: return L"To native · popup";
    case ShortcutAction::ForeignPopup: return L"To foreign · popup";
    case ShortcutAction::SmartReplace: return L"Smart translate · replace";
    case ShortcutAction::NativeReplace: return L"To native · replace";
    case ShortcutAction::ForeignReplace: return L"To foreign · replace";
    case ShortcutAction::SmartPopup:
    default: return L"Smart translate · popup";
    }
}
}

void TrayIcon::add(HWND owner, UINT callbackMessage) {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = owner;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = callbackMessage;
    nid_.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON),
                                   IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    if (!nid_.hIcon) nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid_.szTip, L"TypeTide");
    added_ = Shell_NotifyIconW(NIM_ADD, &nid_);
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
}

void TrayIcon::remove() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        added_ = false;
    }
}

void TrayIcon::showMenu(HWND owner) {
    const Settings& s = Settings::shared();
    HMENU menu = CreatePopupMenu();

    if (updateAvailable_) {
        AppendMenuW(menu, MF_STRING, kMenuUpdate, L"⬇ New update available");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }

    AppendMenuW(menu, MF_STRING | (s.enabled ? MF_CHECKED : 0), kMenuToggle,
                s.enabled ? L"TypeTide is On" : L"TypeTide is Off");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    std::wstring readHint = std::wstring(actionName(s.readShortcutAction)) + L":  " + s.readShortcut.displayString();
    std::wstring rewriteHint = std::wstring(actionName(s.rewriteShortcutAction)) + L":  " + s.rewriteShortcut.displayString();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, kMenuReadHint, readHint.c_str());
    AppendMenuW(menu, MF_STRING | MF_GRAYED, kMenuRewriteHint, rewriteHint.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // 绑定只影响之后的触发；用于解码的 profile 列表与菜单显示的是同一份已提交快照
    const ProfileConfiguration config = s.snapshot().profileConfig;
    const std::vector<BindingSlot> slots = {
        {ProfileEntry::Shortcut1, L"Shortcut 1 profile  (" + s.readShortcut.displayString() + L")"},
        {ProfileEntry::Shortcut2, L"Shortcut 2 profile  (" + s.rewriteShortcut.displayString() + L")"},
        {ProfileEntry::Selection, L"Selection profile"},
    };
    appendBindingMenus(menu, slots, config);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, kMenuProfiles, L"Manage profiles…");
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings…");
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit TypeTide");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(owner);  // 官方要求：否则菜单点外部不消失
    UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                    pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(menu);

    dispatchCommand(*this, cmd, slots, config);
}
