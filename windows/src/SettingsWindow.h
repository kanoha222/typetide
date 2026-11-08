//
//  SettingsWindow.h — 设置窗口（对应 macOS 的 SettingsView）。
//  Tab 页：General / Translation / Languages / Shortcuts / Behavior / Skip Apps / Diagnostics / About。
//  所有更改即改即存。Translation 与 Languages 页只汇总已绑定的 profile（编辑在 profile 编辑器里），
//  并保留共享的连接设置。
//
#pragma once
#include <windows.h>

namespace SettingsWindow {

/// 打开（或前置已打开的）设置窗口。appWindow 用于设置变更后通知重装配。
void open(HWND appWindow, bool showShortcuts = false);
void refreshShortcutStatus();

/// Self-test: verify all navigation icons retain the requested theme color.
bool testNavigationIconColors(bool dark, UINT dpi = 96);

/// Main thread: committed profiles or bindings changed elsewhere (profile editor, tray).
/// Refreshes the summaries of an open window; does nothing when it is closed.
void profilesChanged();

} // namespace SettingsWindow
