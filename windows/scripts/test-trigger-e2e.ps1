# Trigger-path end-to-end test: the real TypeTide.exe, real global shortcuts (injected keys), a
# WinForms edit host (edit-host.ps1) and the loopback mock provider (mock-openai.ps1, Ollama
# /api/generate). It seeds an isolated TYPETIDE_DATA_DIR with Ollama profiles that use distinct
# mock model names, so the mock's request record shows which profile every trigger ran. It also
# runs the older test-e2e.ps1 (WinForms) and test-e2e-wpf.ps1 (WPF) smoke tests on a seeded
# configuration. Synthetic text only: no real provider, credential or user selection.
# Run with Windows PowerShell 5.1 while the mock listens on $Port with -RecordFile $RecordFile.
# Exit codes: 0 = all checks passed, 1 = failures, 2 = test environment problem.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$DataDir,
    [Parameter(Mandatory = $true)][string]$RecordFile,
    [int]$Port = 8199
)
$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class T {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr hwnd, StringBuilder name, int max);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr wParam, string lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr wParam, StringBuilder lParam);
    [DllImport("user32.dll", EntryPoint = "SendMessageW")] public static extern IntPtr SendMessageInt(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr menu, int position);
    [DllImport("user32.dll")] public static extern uint GetMenuState(IntPtr menu, uint item, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetMenuStringW(IntPtr menu, uint item, StringBuilder text, int max, uint flags);

    static string ClassOf(IntPtr hwnd) {
        StringBuilder name = new StringBuilder(256);
        GetClassNameW(hwnd, name, name.Capacity);
        return name.ToString();
    }

    /// Visible top-level windows of process `pid` with class `cls`, in z-order (top first).
    public static IntPtr[] Windows(uint pid, string cls) {
        List<IntPtr> found = new List<IntPtr>();
        EnumProc callback = delegate (IntPtr hwnd, IntPtr unused) {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner == pid && IsWindowVisible(hwnd) && ClassOf(hwnd) == cls) found.Add(hwnd);
            return true;
        };
        EnumWindows(callback, IntPtr.Zero);
        GC.KeepAlive(callback);
        return found.ToArray();
    }

    /// Text of a window in another process (WM_GETTEXT is marshaled; GetWindowText is not).
    public static string Text(IntPtr hwnd) {
        int length = SendMessageInt(hwnd, 0x000E, IntPtr.Zero, IntPtr.Zero).ToInt32();  // WM_GETTEXTLENGTH
        StringBuilder text = new StringBuilder(length + 1);
        SendMessageW(hwnd, 0x000D, (IntPtr)(length + 1), text);                           // WM_GETTEXT
        return text.ToString();
    }

    /// "class 'title'" of every visible top-level window of process `pid` (diagnostics).
    public static string Describe(uint pid) {
        StringBuilder all = new StringBuilder();
        EnumProc callback = delegate (IntPtr hwnd, IntPtr unused) {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner == pid && IsWindowVisible(hwnd)) all.AppendFormat("{0} '{1}'; ", ClassOf(hwnd), Text(hwnd));
            return true;
        };
        EnumWindows(callback, IntPtr.Zero);
        GC.KeepAlive(callback);
        return all.ToString();
    }

    public static string MenuText(IntPtr menu, int position) {
        StringBuilder text = new StringBuilder(512);
        GetMenuStringW(menu, (uint)position, text, text.Capacity, 0x400);  // MF_BYPOSITION
        return text.ToString();
    }
}
"@

$NUL = [NullString]::Value  # PowerShell marshals $null as "" for string parameters
$VK_MENU = 0x12; $VK_ESCAPE = 0x1B; $VK_DOWN = 0x28; $VK_RIGHT = 0x27; $VK_RETURN = 0x0D
$WM_SETTEXT = 0x000C; $WM_KEYDOWN = 0x0100; $WM_LBUTTONDOWN = 0x0201; $WM_RBUTTONUP = 0x0205
$EM_SETSEL = 0x00B1; $MN_GETHMENU = 0x01E1; $WM_APP_TRAY = 0x8000 + 1
$MF_BYPOSITION = 0x400; $MF_CHECKED = 0x8; $MF_HILITE = 0x80
$MOUSEEVENTF_LEFTDOWN = 0x0002; $MOUSEEVENTF_LEFTUP = 0x0004; $IDOK = 1

# Synthetic selected text: a Chinese sentence, kept as code points so this file stays ASCII.
$Source = -join ([char[]]@(0x4F60, 0x597D, 0x4E16, 0x754C, 0xFF0C, 0x4ECA, 0x5929, 0x5929, 0x6C14, 0x5F88, 0x597D, 0x3002))
$Clipboard = "CLIPBOARD_BEFORE_TRIGGER_E2E"
$Default = "MOCK_TRANSLATION_OK"          # the mock's reply for ordinary models
$Exact = "  EXACT_OUTPUT`n`tline 2`n"      # the mock's reply for mock-exact
$CustomPrompt = 'Translate {{source_language}} into {{target_language}}.'

$script:failures = 0
$script:app = $null
$script:editHost = $null
$script:hostHwnd = [IntPtr]::Zero
$script:editHwnd = [IntPtr]::Zero

function Check([bool]$cond, [string]$name, [string]$detail = "") {
    if ($cond) { Write-Output "  ok   $name" }
    else { Write-Output "  FAIL $name $detail"; $script:failures++ }
}

function Wait-For([scriptblock]$probe, [int]$timeoutMs = 10000, [int]$intervalMs = 200) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    while ($true) {
        $result = & $probe
        if ($result) { return $result }
        if ([DateTime]::UtcNow -ge $deadline) { return $null }
        Start-Sleep -Milliseconds $intervalMs
    }
}

function Visible-Text([string]$text) { return ($text -replace "`r", '\r' -replace "`n", '\n' -replace "`t", '\t') }

# ---------- configuration ----------

function Profile([string]$id, [string]$name, [string]$model, [string]$direction, [bool]$custom) {
    return [ordered]@{
        id = $id; name = $name; nativeLanguage = 'zh'; foreignLanguage = 'en'; direction = $direction
        promptMode = $(if ($custom) { 'custom' } else { 'preset' }); presetStyle = 'faithful'
        systemPrompt = $(if ($custom) { $CustomPrompt } else { '' }); backend = 'ollama'; model = $model
    }
}

# Three Ollama profiles with distinct mock models. mock-default and mock-exact are "installed"
# (the mock's /api/tags); mock-selection is not, which only matters for preload.
function Write-Settings([string]$shortcut1, [string]$shortcut2, [string]$action1, [string]$action2,
                        [int]$shortcut2Vk = 0x52, [string]$trigger = 'none') {
    $settings = [ordered]@{
        schemaVersion = 2
        profiles = @(
            (Profile 'p-popup' 'E2E popup' 'mock-default' 'auto' $false),
            (Profile 'p-exact' 'E2E exact' 'mock-exact' 'nativeToForeign' $true),
            (Profile 'p-select' 'E2E selection' 'mock-selection' 'auto' $false)
        )
        shortcut1ProfileId = $shortcut1; shortcut2ProfileId = $shortcut2; selectionProfileId = 'p-select'
        enabled = $true; backend = 'ollama'; ollamaHost = 'http://127.0.0.1'; ollamaPort = $Port
        readShortcut = [ordered]@{ modifiers = 1; vk = 0x44 }  # Alt+D
        rewriteShortcut = [ordered]@{ modifiers = $(if ($shortcut2Vk) { 1 } else { 0 }); vk = $shortcut2Vk }
        readShortcutAction = $action1; rewriteShortcutAction = $action2
        selectionTrigger = $trigger; rewritePreview = $false
        hasCompletedFirstLaunch = $true  # no first-run Settings window
        lastUpdateCheck = [int64]([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())  # no update check
    }
    $json = $settings | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText((Join-Path $DataDir 'settings.json'), $json, (New-Object System.Text.UTF8Encoding($false)))
}

function Read-Settings { return Get-Content -Raw -Encoding UTF8 (Join-Path $DataDir 'settings.json') | ConvertFrom-Json }

# ---------- processes and windows ----------

function Stop-App {
    if ($script:app) {
        Stop-Process -Id $script:app.Id -Force -ErrorAction SilentlyContinue
        $script:app.WaitForExit(5000) | Out-Null  # frees the single-instance mutex
        $script:app = $null
    }
}

function Start-App {
    Stop-App
    $script:app = Start-Process -FilePath $Exe -PassThru
    $window = Wait-For { $h = [T]::FindWindowW('TypeTideApp', $NUL); if ($h -ne [IntPtr]::Zero) { $h } } 15000
    Start-Sleep -Milliseconds 1500  # hotkeys, hooks and the start-up preload settle
    return $window
}

function Ensure-Foreground([IntPtr]$hwnd) {
    # A background process may not take the foreground; one harmless key press makes this
    # process the last input source so SetForegroundWindow is allowed.
    for ($i = 0; $i -lt 10; $i++) {
        [T]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero)
        [T]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero)
        [T]::SetForegroundWindow($hwnd) | Out-Null
        Start-Sleep -Milliseconds 150
        if ([T]::GetForegroundWindow() -eq $hwnd) { return $true }
    }
    return $false
}

function Start-EditHost {
    $script:editHost = Start-Process powershell -ArgumentList '-NoProfile', '-File', "`"$PSScriptRoot\edit-host.ps1`"" -PassThru
    $script:hostHwnd = Wait-For { $h = [T]::FindWindowW($NUL, 'TypeTideTestHost'); if ($h -ne [IntPtr]::Zero) { $h } } 15000
    if (-not $script:hostHwnd) { return $false }
    $script:editHwnd = [T]::FindWindowExW($script:hostHwnd, [IntPtr]::Zero, $NUL, $NUL)  # the text box
    return $script:editHwnd -ne [IntPtr]::Zero
}

function Edit-Text { return [T]::Text($script:editHwnd) }

# Source text, fully selected, in the foreground edit host; a known clipboard.
function Prepare-Text {
    [T]::SendMessageW($script:editHwnd, $WM_SETTEXT, [IntPtr]::Zero, $Source) | Out-Null
    Set-Clipboard -Value $Clipboard
    $foreground = Ensure-Foreground $script:hostHwnd
    Start-Sleep -Milliseconds 200
    [T]::SendMessageInt($script:editHwnd, $EM_SETSEL, [IntPtr]::Zero, [IntPtr](-1)) | Out-Null
    Start-Sleep -Milliseconds 200
    return $foreground -and (Edit-Text) -eq $Source
}

function Send-Combo([byte[]]$mods, [byte]$vk) {
    foreach ($m in $mods) { [T]::keybd_event($m, 0, 0, [UIntPtr]::Zero) }
    [T]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 50
    [T]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
    foreach ($m in $mods) { [T]::keybd_event($m, 0, 2, [UIntPtr]::Zero) }
}

function Find-Popup { $h = [T]::FindWindowW('TypeTidePopup', $NUL); if ($h -ne [IntPtr]::Zero -and [T]::IsWindowVisible($h)) { $h } }

# The popup's finished text (two equal non-empty reads in a row), or $null.
function Popup-Text {
    $popup = Wait-For { Find-Popup } 15000
    if (-not $popup) { return $null }
    $previous = $null
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Milliseconds 300
        $text = [T]::Text($popup)
        if ($text -and $text -eq $previous) { return $text }
        $previous = $text
    }
    return $previous
}

function Close-Popup {
    Send-Combo @() $VK_ESCAPE
    return [bool](Wait-For { -not (Find-Popup) } 5000)
}

# The popup's Replace button, at its layout position (PopupWindow::layout: right padding 16,
# bottom padding 12, button 84 x 32, all scaled by the window's DPI).
function Click-Replace {
    $popup = Find-Popup
    if (-not $popup) { return $false }
    $rect = New-Object T+RECT
    [T]::GetClientRect($popup, [ref]$rect) | Out-Null
    $scale = [T]::GetDpiForWindow($popup) / 96.0
    $x = [int]($rect.Right - (16 + 42) * $scale)
    $y = [int]($rect.Bottom - (12 + 16) * $scale)
    [T]::PostMessageW($popup, $WM_LBUTTONDOWN, [IntPtr]1, [IntPtr](($y -shl 16) -bor $x)) | Out-Null
    return $true
}

# The edit host's text once it differs from the source text, or the source after the timeout.
function Replaced-Text([int]$timeoutMs = 15000) {
    $changed = Wait-For { $t = Edit-Text; if ($t -ne $Source) { $t } } $timeoutMs 250
    Start-Sleep -Milliseconds 700  # the clipboard is restored after the paste
    if ($changed) { return $changed } else { return (Edit-Text) }
}

# ---------- the mock's request record ----------

function Record-Count {
    if (-not (Test-Path $RecordFile)) { return 0 }
    return @(Get-Content -Encoding UTF8 $RecordFile).Count
}

# Translation requests (/api/generate with a non-empty prompt: preload requests have none)
# recorded after line `$from`, as parsed request bodies.
function Translations([int]$from) {
    if (-not (Test-Path $RecordFile)) { return @() }
    $lines = @(Get-Content -Encoding UTF8 $RecordFile)
    $bodies = @()
    for ($i = $from; $i -lt $lines.Count; $i++) {
        $record = $lines[$i] | ConvertFrom-Json
        if ($record.path -ne '/api/generate') { continue }
        $body = $record.body | ConvertFrom-Json
        if ($body.prompt) { $bodies += $body }
    }
    return $bodies
}

function Models([int]$from) { return @(Translations $from | ForEach-Object { $_.model }) -join ',' }

# ---------- tray menu ----------

function Menu-Windows { if (-not $script:app) { return @() } return @([T]::Windows([uint32]$script:app.Id, '#32768')) }

function Menu-Index($menu, [string]$prefix) {
    for ($i = 0; $i -lt [T]::GetMenuItemCount($menu); $i++) {
        if ([T]::MenuText($menu, $i).StartsWith($prefix)) { return $i }
    }
    return -1
}

function Hilited($menu) {
    for ($i = 0; $i -lt [T]::GetMenuItemCount($menu); $i++) {
        if (([T]::GetMenuState($menu, [uint32]$i, $MF_BYPOSITION) -band $MF_HILITE) -ne 0) { return $i }
    }
    return -1
}

# Moves the highlight of the menu shown in `$window` down to `$index`.
function Highlight($window, $menu, [int]$index) {
    for ($i = 0; $i -lt 30; $i++) {
        if ((Hilited $menu) -eq $index) { return $true }
        [T]::PostMessageW($window, $WM_KEYDOWN, [IntPtr]$VK_DOWN, [IntPtr]::Zero) | Out-Null
        Start-Sleep -Milliseconds 150
    }
    return (Hilited $menu) -eq $index
}

# ---------- scenarios ----------

function Test-PopupAndReplace {
    Write-Output "[shortcut 1 popup, popup Replace, shortcut 2 direct replacement]"
    Write-Settings 'p-popup' 'p-exact' 'smartPopup' 'smartReplace'
    Check ($null -ne (Start-App)) "TypeTide starts with the seeded profiles"
    Check (Prepare-Text) "the edit host holds the selected source text"
    $from = Record-Count
    Send-Combo @($VK_MENU) 0x44  # Alt+D
    $popup = Popup-Text
    Check ($popup -eq $Default) "shortcut 1 opens a popup with its profile's translation" "got '$(Visible-Text $popup)'"
    Check ((Models $from) -eq 'mock-default') "shortcut 1 ran the profile bound to it" "models '$(Models $from)'"
    Check (Click-Replace) "the popup offers Replace"
    $replaced = Replaced-Text
    Check ($replaced -eq $Default) "Replace pastes the popup translation" "got '$(Visible-Text $replaced)'"
    Check ((Get-Clipboard -Raw) -eq $Clipboard) "the clipboard is restored after Replace"

    Check (Prepare-Text) "the source text is selected again"
    $from = Record-Count
    Send-Combo @($VK_MENU) 0x52  # Alt+R
    $replaced = (Replaced-Text) -replace "`r`n", "`n"
    Check ($replaced -eq $Exact) "direct replacement keeps the custom output exactly" "got '$(Visible-Text $replaced)'"
    $requests = @(Translations $from)
    Check ($requests.Count -eq 1 -and $requests[0].model -eq 'mock-exact') "shortcut 2 ran the profile bound to it" `
        "models '$(Models $from)'"
    if ($requests.Count -eq 1) {
        Check ($requests[0].system -eq 'Translate Chinese into English.' -and $requests[0].prompt -eq $Source) `
            "the custom prompt is rendered into system and the selection sent unchanged"
    }
    Check ((Get-Clipboard -Raw) -eq $Clipboard) "the clipboard is restored after direct replacement"
}

function Test-SwappedActions {
    Write-Output "[swapped actions: the physical slot still chooses the profile]"
    Write-Settings 'p-popup' 'p-exact' 'smartReplace' 'smartPopup'
    Check ($null -ne (Start-App)) "TypeTide restarts with swapped actions"
    Check (Prepare-Text) "swapped: source text selected"
    $from = Record-Count
    Send-Combo @($VK_MENU) 0x44
    $replaced = Replaced-Text
    Check ($replaced -eq $Default -and (Models $from) -eq 'mock-default') `
        "shortcut 1 now replaces, still with its own profile" "got '$(Visible-Text $replaced)', models '$(Models $from)'"
    Check (Prepare-Text) "swapped: source text selected again"
    $from = Record-Count
    Send-Combo @($VK_MENU) 0x52
    $popup = Popup-Text
    Check ($popup -eq $Exact -and (Models $from) -eq 'mock-exact') `
        "shortcut 2 now opens a popup, still with its own profile" "got '$(Visible-Text $popup)', models '$(Models $from)'"
    Close-Popup | Out-Null
}

function Test-SameAction {
    Write-Output "[both shortcuts with the same action]"
    Write-Settings 'p-popup' 'p-exact' 'smartPopup' 'smartPopup'
    Check ($null -ne (Start-App)) "TypeTide restarts with one action on both shortcuts"
    foreach ($case in @(@{ vk = 0x44; text = $Default; model = 'mock-default'; name = 'shortcut 1' },
                        @{ vk = 0x52; text = $Exact; model = 'mock-exact'; name = 'shortcut 2' })) {
        Check (Prepare-Text) "same action: source text selected for $($case.name)"
        $from = Record-Count
        Send-Combo @($VK_MENU) ([byte]$case.vk)
        $popup = Popup-Text
        Check ($popup -eq $case.text -and (Models $from) -eq $case.model) `
            "$($case.name) runs its own profile when both share an action" "got '$(Visible-Text $popup)', models '$(Models $from)'"
        Close-Popup | Out-Null
    }
}

function Test-ClearedShortcutAndSelection {
    Write-Output "[a cleared shortcut, and the selection trigger]"
    Write-Settings 'p-popup' 'p-exact' 'smartPopup' 'smartReplace' -shortcut2Vk 0 -trigger 'auto'
    Check ($null -ne (Start-App)) "TypeTide restarts with shortcut 2 cleared and auto selection"
    Check (Prepare-Text) "cleared: source text selected"
    $from = Record-Count
    Send-Combo @($VK_MENU) 0x52
    Start-Sleep -Seconds 3
    Check ((-not (Find-Popup)) -and (Edit-Text) -eq $Source -and (Translations $from).Count -eq 0) `
        "a cleared shortcut triggers nothing"
    Check (Prepare-Text) "selection: source text selected"
    $rect = New-Object T+RECT
    [T]::GetWindowRect($script:hostHwnd, [ref]$rect) | Out-Null
    [T]::SetCursorPos($rect.Left + 120, $rect.Top + 12) | Out-Null  # the caption, not the text box
    $from = Record-Count
    [T]::mouse_event($MOUSEEVENTF_LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [T]::mouse_event($MOUSEEVENTF_LEFTUP, 0, 0, 0, [UIntPtr]::Zero)  # "selection finished"
    $popup = Popup-Text
    if (-not $popup) {
        Write-Output "    diagnostics: foreground $([T]::GetForegroundWindow()) host $($script:hostHwnd); edit text '$(Visible-Text (Edit-Text))'"
    }
    Check ($popup -eq $Default -and (Models $from) -eq 'mock-selection') `
        "the selection trigger runs the profile bound to Selection" "got '$(Visible-Text $popup)', models '$(Models $from)'"
    Close-Popup | Out-Null
}

function Test-TrayBinding {
    Write-Output "[tray binding menus]"
    Write-Settings 'p-popup' 'p-exact' 'smartPopup' 'smartReplace'
    $appWindow = Start-App
    Check ($null -ne $appWindow) "TypeTide restarts for the tray test"
    if (-not $appWindow) { return }
    [T]::PostMessageW($appWindow, $WM_APP_TRAY, [IntPtr]::Zero, [IntPtr]$WM_RBUTTONUP) | Out-Null
    $window = Wait-For { @(Menu-Windows)[0] } 5000
    Check ($null -ne $window) "the tray menu opens"
    if (-not $window) { return }
    $menu = [T]::SendMessageInt($window, $MN_GETHMENU, [IntPtr]::Zero, [IntPtr]::Zero)
    $expected = @{ 'Shortcut 1 profile' = 'E2E popup'; 'Shortcut 2 profile' = 'E2E exact'; 'Selection profile' = 'E2E selection' }
    foreach ($label in @('Shortcut 1 profile', 'Shortcut 2 profile', 'Selection profile')) {
        $index = Menu-Index $menu $label
        $sub = if ($index -ge 0) { [T]::GetSubMenu($menu, $index) } else { [IntPtr]::Zero }
        $names = @(); $checked = @()
        if ($sub -ne [IntPtr]::Zero) {
            for ($i = 0; $i -lt [T]::GetMenuItemCount($sub); $i++) {
                $names += [T]::MenuText($sub, $i)
                if (([T]::GetMenuState($sub, [uint32]$i, $MF_BYPOSITION) -band $MF_CHECKED) -ne 0) { $checked += [T]::MenuText($sub, $i) }
            }
        }
        Check (($names -join '|') -eq 'E2E popup|E2E exact|E2E selection' -and ($checked -join '|') -eq $expected[$label]) `
            "'$label' lists every profile and checks the bound one" "items '$($names -join '|')', checked '$($checked -join '|')'"
    }
    $index = Menu-Index $menu 'Shortcut 1 profile'
    Check (Highlight $window $menu $index) "the keyboard reaches 'Shortcut 1 profile'" "highlighted $(Hilited $menu)"
    [T]::PostMessageW($window, $WM_KEYDOWN, [IntPtr]$VK_RIGHT, [IntPtr]::Zero) | Out-Null
    $subWindow = Wait-For { @(Menu-Windows | Where-Object { $_ -ne $window })[0] } 5000
    Check ($null -ne $subWindow) "the binding submenu opens"
    if (-not $subWindow) { Send-Combo @() $VK_ESCAPE; Send-Combo @() $VK_ESCAPE; return }
    $sub = [T]::SendMessageInt($subWindow, $MN_GETHMENU, [IntPtr]::Zero, [IntPtr]::Zero)
    Check (Highlight $subWindow $sub 1) "the keyboard reaches 'E2E exact'" "highlighted $(Hilited $sub)"
    [T]::PostMessageW($subWindow, $WM_KEYDOWN, [IntPtr]$VK_RETURN, [IntPtr]::Zero) | Out-Null
    $closed = Wait-For { @(Menu-Windows).Count -eq 0 } 3000
    if (-not $closed) {
        Write-Output "    diagnostics: Enter on the submenu left the menu open; trying the menu loop's own window"
        [T]::PostMessageW($window, $WM_KEYDOWN, [IntPtr]$VK_RETURN, [IntPtr]::Zero) | Out-Null
        $closed = Wait-For { @(Menu-Windows).Count -eq 0 } 3000
    }
    Write-Output "    diagnostics: menu closed: $([bool]$closed); app windows: $([T]::Describe([uint32]$script:app.Id))"
    $dialog = Wait-For { @([T]::Windows([uint32]$script:app.Id, '#32770'))[0] } 1500
    if ($dialog) {
        $static = [T]::FindWindowExW($dialog, [IntPtr]::Zero, 'Static', $NUL)
        Write-Output "    diagnostics: dialog '$([T]::Text($dialog))': '$(if ($static -ne [IntPtr]::Zero) { [T]::Text($static) })'"
        [T]::PostMessageW($dialog, 0x0111, [IntPtr]$IDOK, [IntPtr]::Zero) | Out-Null  # WM_COMMAND IDOK
    }
    $bound = Wait-For { (Read-Settings).shortcut1ProfileId -eq 'p-exact' } 5000
    Check ([bool]$bound) "choosing a profile in the tray binds shortcut 1" "shortcut1ProfileId '$((Read-Settings).shortcut1ProfileId)'"
    Check (Prepare-Text) "tray: source text selected"
    $from = Record-Count
    Send-Combo @($VK_MENU) 0x44
    $popup = Popup-Text
    Check ($popup -eq $Exact -and (Models $from) -eq 'mock-exact') `
        "the next shortcut 1 trigger uses the new binding without a restart" "got '$(Visible-Text $popup)', models '$(Models $from)'"
    Close-Popup | Out-Null
}

# The older smoke tests start and stop TypeTide themselves; both shortcuts run 'E2E popup'.
function Test-LegacySmoke([string]$script) {
    Write-Output "[$script]"
    $ErrorActionPreference = 'Continue'  # the child's stderr must not abort this script
    Stop-App
    Write-Settings 'p-popup' 'p-popup' 'smartPopup' 'smartReplace'
    $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $script) -Exe $Exe 2>&1
    $code = $LASTEXITCODE
    $output | ForEach-Object { Write-Output "    $_" }
    Check ($code -eq 0) "$script passes" "exit $code"
    Get-Process -Name TypeTide -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

if (-not (Test-Path $Exe)) { Write-Output "missing executable: $Exe"; exit 2 }
if ((Test-Path $DataDir) -and @(Get-ChildItem -Force $DataDir).Count -gt 0) {
    Write-Output "DataDir must be new or empty (refusing to touch existing data): $DataDir"
    exit 2
}
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
$env:TYPETIDE_DATA_DIR = $DataDir

try {
    Test-LegacySmoke 'test-e2e.ps1'
    Test-LegacySmoke 'test-e2e-wpf.ps1'
    if (-not (Start-EditHost)) { Write-Output "the edit host did not start"; exit 2 }
    Test-PopupAndReplace
    Test-SwappedActions
    Test-SameAction
    Test-ClearedShortcutAndSelection
    Test-TrayBinding
} finally {
    Stop-App
    if ($script:editHost) { Stop-Process -Id $script:editHost.Id -Force -ErrorAction SilentlyContinue }
}

if ($script:failures -gt 0) { Write-Output "`n$($script:failures) FAILURE(S)"; exit 1 }
Write-Output "`nall trigger-path checks passed"
exit 0
