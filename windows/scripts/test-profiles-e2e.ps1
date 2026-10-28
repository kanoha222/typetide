# Profile editor end-to-end test: launches the real TypeTide.exe with --open-profiles (and
# finally --open-settings) in an isolated TYPETIDE_DATA_DIR and drives the native editor and the
# Settings window's profile summary with window messages (WM_COMMAND,
# WM_SETTEXT/WM_GETTEXT, LB_GETCOUNT, posted menu keys), like the other windows/scripts E2E
# tests. Managed UI Automation sees these Win32 controls only as generic panes on the CI runner.
# The editor is only given synthetic data: no clipboard, credentials or user selection. (The
# app's own startup update check may still reach the network; nothing here sends profile data.)
# Run with Windows PowerShell 5.1.
# Exit codes: 0 = all checks passed, 1 = failures, 2 = test environment problem.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$DataDir
)
$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class W {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr hwnd, StringBuilder name, int max);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr dialog, int id);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr wParam, string lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr wParam, StringBuilder lParam);
    [DllImport("user32.dll", EntryPoint = "SendMessageW")] public static extern IntPtr SendMessageInt(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr lParam);

    static string ClassOf(IntPtr hwnd) {
        StringBuilder name = new StringBuilder(256);
        GetClassNameW(hwnd, name, name.Capacity);
        return name.ToString();
    }

    /// A visible top-level window of process `pid` with class `cls` (and caption `title` if given).
    public static IntPtr FindTop(uint pid, string cls, string title) {
        IntPtr found = IntPtr.Zero;
        EnumProc callback = delegate (IntPtr hwnd, IntPtr unused) {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner != pid || !IsWindowVisible(hwnd) || ClassOf(hwnd) != cls) return true;
            if (!string.IsNullOrEmpty(title) && Text(hwnd) != title) return true;  // PowerShell passes $null as ""
            found = hwnd;
            return false;
        };
        EnumWindows(callback, IntPtr.Zero);
        GC.KeepAlive(callback);
        return found;
    }

    /// Diagnostic list of the top-level windows of process `pid`: class, caption, visibility.
    public static string ListTop(uint pid) {
        StringBuilder all = new StringBuilder();
        EnumProc callback = delegate (IntPtr hwnd, IntPtr unused) {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner == pid)
                all.AppendFormat("    class='{0}' title='{1}' visible={2}\n", ClassOf(hwnd), Text(hwnd), IsWindowVisible(hwnd));
            return true;
        };
        EnumWindows(callback, IntPtr.Zero);
        GC.KeepAlive(callback);
        return all.ToString();
    }

    /// Texts of every child control of `parent` (hidden pages included).
    public static string[] ChildTexts(IntPtr parent) {
        System.Collections.Generic.List<string> all = new System.Collections.Generic.List<string>();
        EnumProc callback = delegate (IntPtr hwnd, IntPtr unused) { all.Add(Text(hwnd)); return true; };
        EnumChildWindows(parent, callback, IntPtr.Zero);
        GC.KeepAlive(callback);
        return all.ToArray();
    }

    /// Text of a window in another process (WM_GETTEXT is marshaled; GetWindowText is not).
    public static string Text(IntPtr hwnd) {
        int length = SendMessageInt(hwnd, 0x000E, IntPtr.Zero, IntPtr.Zero).ToInt32();  // WM_GETTEXTLENGTH
        StringBuilder text = new StringBuilder(length + 1);
        SendMessageW(hwnd, 0x000D, (IntPtr)(length + 1), text);                           // WM_GETTEXT
        return text.ToString();
    }
}
"@

# Control IDs of TranslationProfilesWindow.
$IdList = 3001; $IdAdd = 3002; $IdDelete = 3004
$IdName = 3010; $IdCustomMode = 3021; $IdPrompt = 3023; $IdPreview = 3025
$IdSave = 3041; $IdClose = 3042; $IdStatus = 3043
$IdNo = 7  # IDNO in the Save/Discard/Cancel message box
$WM_SETTEXT = 0x000C; $WM_COMMAND = 0x0111; $BM_SETCHECK = 0x00F1; $LB_GETCOUNT = 0x018B
$IdPresetMode = 3020
$WM_KEYDOWN = 0x0100; $VK_DOWN = 0x28; $VK_RETURN = 0x0D
# Control IDs of SettingsWindow's profile summaries.
$SIdSummary = @(1901, 1902, 1903); $SIdManage = 1904; $SIdTest = 1905; $SIdTestStatus = 1906
$SIdLanguage = @(1911, 1912, 1913)

$script:failures = 0
$script:app = $null

function Check([bool]$cond, [string]$name, [string]$detail = "") {
    if ($cond) { Write-Output "  ok   $name" }
    else { Write-Output "  FAIL $name $detail"; $script:failures++ }
}

# Polls $probe until it returns something truthy or the timeout passes (then $null).
function Wait-For([scriptblock]$probe, [int]$timeoutMs = 5000, [int]$intervalMs = 200) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    while ($true) {
        $result = & $probe
        if ($result) { return $result }
        if ([DateTime]::UtcNow -ge $deadline) { return $null }
        Start-Sleep -Milliseconds $intervalMs
    }
}

function Find-AppWindow([string]$className, [string]$title = $null) {
    $hwnd = [W]::FindTop([uint32]$script:app.Id, $className, $title)
    if ($hwnd -eq [IntPtr]::Zero) { return $null }
    return $hwnd
}

function Control($window, [int]$id) { return [W]::GetDlgItem($window, $id) }
# The WM_COMMAND/BN_CLICKED a click produces, sent to the parent. (BM_CLICK simulates the mouse
# and can fail when the window is not active, which nothing guarantees on a CI desktop.)
function Click($window, [int]$id) {
    [W]::SendMessageInt($window, $WM_COMMAND, [IntPtr]$id, (Control $window $id)) | Out-Null
}
function Set-Text($window, [int]$id, [string]$text) { [W]::SendMessageW((Control $window $id), $WM_SETTEXT, [IntPtr]::Zero, $text) | Out-Null }
function Get-Text($window, [int]$id) { return [W]::Text((Control $window $id)) }
function Item-Count($window) { return [W]::SendMessageInt((Control $window $IdList), $LB_GETCOUNT, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() }
function Status-Text($window) { return Get-Text $window $IdStatus }

function Choose-Custom($window) {
    [W]::SendMessageInt((Control $window $IdPresetMode), $BM_SETCHECK, [IntPtr]0, [IntPtr]::Zero) | Out-Null
    [W]::SendMessageInt((Control $window $IdCustomMode), $BM_SETCHECK, [IntPtr]1, [IntPtr]::Zero) | Out-Null
    Click $window $IdCustomMode
    return [bool](Wait-For { [W]::IsWindowEnabled((Control $window $IdPrompt)) })
}

function Read-Settings {
    $path = Join-Path $DataDir 'settings.json'
    if (-not (Test-Path $path)) { return $null }
    return Get-Content -Raw -Encoding UTF8 $path | ConvertFrom-Json
}

# (Re)starts the app with `$argument` and returns its top-level window of class `$className`.
function Start-App([string]$argument, [string]$className) {
    if ($script:app) {
        Stop-Process -Id $script:app.Id -Force -ErrorAction SilentlyContinue
        $script:app.WaitForExit(5000) | Out-Null  # frees the single-instance mutex
    }
    $script:app = Start-Process -FilePath $Exe -ArgumentList $argument -PassThru
    $window = Wait-For { Find-AppWindow $className } 15000
    if (-not $window) {
        Write-Host "  top-level windows of the app:"  # host, not output: Start-App returns the window
        Write-Host ([W]::ListTop([uint32]$script:app.Id))
    }
    return $window
}

function Start-Editor {
    $window = Start-App '--open-profiles' 'TypeTideProfiles'
    if (-not $window) { return $null }
    # The window is shown once open() has filled the controls.
    if (-not (Wait-For { (Item-Count $window) -gt 0 })) { return $null }
    return $window
}

function Close-Discarding($window) {
    Click $window $IdClose  # posts the Save/Discard/Cancel prompt, so this returns at once
    $dialog = Wait-For { Find-AppWindow '#32770' 'TypeTide' }
    if (-not $dialog) { return $false }
    Click $dialog $IdNo
    return [bool](Wait-For { -not (Find-AppWindow 'TypeTideProfiles') })
}

function Test-CreateAndSave($window) {
    Write-Output "[create, edit and save]"
    Check ((Item-Count $window) -eq 2) "the default configuration lists two profiles" "found $(Item-Count $window)"
    Click $window $IdAdd
    Check ([bool](Wait-For { (Item-Count $window) -eq 3 })) "Add appends a profile"
    Set-Text $window $IdName 'E2E profile & more'
    Check (Choose-Custom $window) "choosing a custom prompt enables the prompt editor"
    Set-Text $window $IdPrompt "Line 1`r`n`tLine 2  "
    Check ([bool](Wait-For { (Get-Text $window $IdPreview) -like '*Built-in*' })) `
        "the preview explains that a new Built-in profile cannot run a custom prompt"
    Click $window $IdSave
    $saved = Wait-For { (Status-Text $window) -like 'Saved*' }
    Check ([bool]$saved) "Save reports success" "status '$(Status-Text $window)'"
    $settings = Read-Settings
    $created = @($settings.profiles | Where-Object { $_.name -eq 'E2E profile & more' })
    Check ($created.Count -eq 1) "the new profile is written to settings.json"
    if ($created.Count -ne 1) { return }
    Check ($created[0].promptMode -eq 'custom' -and $created[0].systemPrompt -eq "Line 1`r`n`tLine 2  ") `
        "the custom prompt is saved exactly"
    Check (@($settings.profiles).Count -eq 3) "settings.json holds three profiles"
}

function Test-DraftCancel($window) {
    Write-Output "[cancel discards drafts]"
    Set-Text $window $IdName 'Draft only'
    Check (Close-Discarding $window) "closing with unsaved edits offers Discard and closes"
    $names = @((Read-Settings).profiles | ForEach-Object { $_.name })
    Check (-not ($names -contains 'Draft only')) "discarded edits never reach settings.json"
}

function Test-OversizePrompt($window) {
    Write-Output "[oversize prompt]"
    Check (Choose-Custom $window) "the first profile switches to a custom prompt"
    Set-Text $window $IdPrompt ('x' * 33000)
    Click $window $IdSave
    $refused = Wait-For { (Status-Text $window) -like 'Not saved*' }
    Check ([bool]$refused) "an over-long prompt is refused" "status '$(Status-Text $window)'"
    $first = @((Read-Settings).profiles)[0]
    Check ($first.promptMode -eq 'preset' -and $first.systemPrompt -eq '') "nothing truncated was saved"
    Check (Close-Discarding $window) "the refused draft is discarded on close"
}

function Test-DeleteWithReplacement($window) {
    Write-Output "[delete a bound profile with a replacement]"
    $deletedId = @((Read-Settings).profiles)[0].id  # the first profile is selected and bound
    Click $window $IdDelete  # posts the delete, so this returns before the menu opens
    $menu = Wait-For { Find-AppWindow '#32768' }
    Check ($null -ne $menu) "deleting a bound profile asks for a replacement"
    if (-not $menu) { return }
    Check ((Status-Text $window) -like '*in use*') "the editor explains why a replacement is needed"
    [W]::PostMessageW($menu, $WM_KEYDOWN, [IntPtr]$VK_DOWN, [IntPtr]::Zero) | Out-Null
    [W]::PostMessageW($menu, $WM_KEYDOWN, [IntPtr]$VK_RETURN, [IntPtr]::Zero) | Out-Null
    Check ([bool](Wait-For { (Item-Count $window) -eq 2 })) "the bound profile is removed"
    Click $window $IdSave
    Check ([bool](Wait-For { (Status-Text $window) -like 'Saved*' })) "the deletion is saved" "status '$(Status-Text $window)'"
    $settings = Read-Settings
    $ids = @($settings.profiles | ForEach-Object { $_.id })
    Check ($ids.Count -eq 2 -and -not ($ids -contains $deletedId)) "settings.json no longer holds the deleted profile"
    Check ($settings.shortcut1ProfileId -eq $settings.selectionProfileId -and ($ids -contains $settings.shortcut1ProfileId)) `
        "the entries bound to the deleted profile moved to the chosen replacement"
}

# Name of the profile settings.json binds to entry `$index` (0 = shortcut 1, 1 = shortcut 2,
# 2 = selection).
function Bound-Name($settings, [int]$index) {
    $id = @($settings.shortcut1ProfileId, $settings.shortcut2ProfileId, $settings.selectionProfileId)[$index]
    return @($settings.profiles | Where-Object { $_.id -eq $id } | ForEach-Object { $_.name })[0]
}

function Test-SettingsSummary {
    Write-Output "[settings summary and profile manager entry]"
    $settingsWindow = Start-App '--open-settings' 'TypeTideSettings'
    Check ($null -ne $settingsWindow) "the settings window opens"
    if (-not $settingsWindow) { return }
    $json = Read-Settings
    $labels = @('Shortcut 1', 'Shortcut 2', 'Selection')
    for ($i = 0; $i -lt 3; $i++) {
        $line = Get-Text $settingsWindow $SIdSummary[$i]
        $name = Bound-Name $json $i
        Check ($line.StartsWith($labels[$i]) -and $name -and $line.Contains($name) -and $line.Contains('Built-in') -and $line.Contains('local')) `
            "the Translation page shows the profile bound to $($labels[$i]), its backend and route" "'$line'"
        $languages = Get-Text $settingsWindow $SIdLanguage[$i]
        Check ($name -and $languages.Contains($name) -and $languages.Contains('English')) `
            "the Languages page shows the languages of the $($labels[$i]) profile" "'$languages'"
    }
    $texts = [W]::ChildTexts($settingsWindow)
    foreach ($legacy in @('Native language:', 'Foreign language:', 'Read direction:', 'Rewrite direction:',
                          'Read style:', 'Rewrite style:', 'Ollama model', 'API model')) {
        Check (-not ($texts -contains $legacy)) "the old '$legacy' control is gone"
    }
    Check (($texts -contains 'Base URL') -and ($texts -contains 'API key')) "the cloud connection fields stay"

    Click $settingsWindow $SIdTest
    $finished = Wait-For { (Get-Text $settingsWindow $SIdTestStatus) -like 'Tested*' } 15000
    Check ([bool]$finished) "testing the bound models finishes" "status '$(Get-Text $settingsWindow $SIdTestStatus)'"
    Check ((Get-Text $settingsWindow $SIdSummary[0]) -like '*Download the built-in model*') `
        "a bound model's test result appears on its line" "'$(Get-Text $settingsWindow $SIdSummary[0])'"

    Click $settingsWindow $SIdManage
    $editor = Wait-For { Find-AppWindow 'TypeTideProfiles' }
    Check ($null -ne $editor) "Manage profiles opens the profile editor"
    if (-not $editor) { return }
    if (-not (Wait-For { (Item-Count $editor) -gt 0 })) { return }
    $firstId = @($json.profiles)[0].id  # selected when the editor opens
    $entryIds = @($json.shortcut1ProfileId, $json.shortcut2ProfileId, $json.selectionProfileId)
    $boundTo = @(0, 1, 2 | Where-Object { $entryIds[$_] -eq $firstId })
    Check ($boundTo.Count -gt 0) "the first profile is bound to an entry"
    Set-Text $editor $IdName 'Renamed for the summary'
    Click $editor $IdSave
    Check ([bool](Wait-For { (Status-Text $editor) -like 'Saved*' })) "the rename is saved" "status '$(Status-Text $editor)'"
    foreach ($i in $boundTo) {
        Check ([bool](Wait-For { (Get-Text $settingsWindow $SIdSummary[$i]) -like '*Renamed for the summary*' })) `
            "an editor save updates the $($labels[$i]) summary" "'$(Get-Text $settingsWindow $SIdSummary[$i])'"
    }
}

if (-not (Test-Path $Exe)) { Write-Output "missing executable: $Exe"; exit 2 }
if ((Test-Path $DataDir) -and @(Get-ChildItem -Force $DataDir).Count -gt 0) {
    Write-Output "DataDir must be new or empty (refusing to touch existing data): $DataDir"
    exit 2
}
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
$env:TYPETIDE_DATA_DIR = $DataDir

try {
    $window = Start-Editor
    Check ($null -ne $window) "the profile editor opens"
    if ($window) {
        Test-CreateAndSave $window
        Test-DraftCancel $window
        $window = Start-Editor
        Check ($null -ne $window -and (Item-Count $window) -eq 3) "profiles survive a restart"
        if ($window) { Test-OversizePrompt $window }
        $window = Start-Editor
        if ($window) { Test-DeleteWithReplacement $window }
        Test-SettingsSummary
    }
} finally {
    if ($script:app) { Stop-Process -Id $script:app.Id -Force -ErrorAction SilentlyContinue }
}

if ($script:failures -gt 0) { Write-Output "`n$($script:failures) FAILURE(S)"; exit 1 }
Write-Output "`nall profile editor and settings summary checks passed"
exit 0
