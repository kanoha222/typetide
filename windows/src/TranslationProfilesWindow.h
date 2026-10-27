//
//  TranslationProfilesWindow.h — single-instance native editor for translation profiles
//  (list / add / duplicate / delete, fields, multiline prompt, preview, bindings, test).
//  Edits a profileeditor::Editor draft; only Save commits it through the settings
//  transaction. Main thread only.
//
#pragma once
#include <windows.h>

namespace profileswindow {

/// Opens the editor, or brings the existing one to the front.
void open(HWND appWindow);

/// Call from the message loop before TranslateMessage/DispatchMessage: gives the editor
/// dialog-style keyboard navigation (Tab / Shift+Tab). True when the message was handled.
bool handleDialogMessage(MSG* msg);

} // namespace profileswindow
