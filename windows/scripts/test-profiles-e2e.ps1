# Profile editor UIA end-to-end test: launches the real TypeTide.exe with --open-profiles in an
# isolated TYPETIDE_DATA_DIR and drives the native editor through UI Automation. The editor is
# only given synthetic data: no clipboard, credentials or user selection. (The app's own startup
# update check may still reach the network; nothing in this test sends profile data anywhere.)
# Run with Windows PowerShell 5.1.
# Exit codes: 0 = all checks passed, 1 = failures, 2 = test environment problem.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$DataDir
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class EditText {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr wParam, string lParam);
}
"@

# Control IDs of TranslationProfilesWindow (exposed by UIA as AutomationId).
$IdList = 3001; $IdAdd = 3002; $IdDelete = 3004
$IdName = 3010; $IdCustomMode = 3021; $IdPrompt = 3023; $IdPreview = 3025
$IdSave = 3041; $IdClose = 3042; $IdStatus = 3043
$IdNo = 7  # IDNO in the Save/Discard/Cancel message box

$AE = [System.Windows.Automation.AutomationElement]
$Scope = [System.Windows.Automation.TreeScope]
$script:failures = 0
$script:app = $null
$script:newId = $null

function Check([bool]$cond, [string]$name, [string]$detail = "") {
    if ($cond) { Write-Output "  ok   $name" }
    else { Write-Output "  FAIL $name $detail"; $script:failures++ }
}

function Cond($property, $value) { New-Object System.Windows.Automation.PropertyCondition($property, $value) }

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

# A top-level window of the app under test, optionally with an exact title.
function Find-AppWindow([string]$className, [string]$title = $null) {
    $conditions = @((Cond $AE::ClassNameProperty $className), (Cond $AE::ProcessIdProperty $script:app.Id))
    if ($title) { $conditions += (Cond $AE::NameProperty $title) }
    $typed = [System.Windows.Automation.Condition[]]$conditions
    $all = New-Object System.Windows.Automation.AndCondition -ArgumentList (,$typed)  # one params array
    return $AE::RootElement.FindFirst($Scope::Children, $all)
}

$ClassOf = @{
    3001 = 'ListBox'; 3002 = 'Button'; 3004 = 'Button'; 3010 = 'Edit'; 3021 = 'Button'; 3023 = 'Edit'
    3025 = 'Edit'; 3041 = 'Button'; 3042 = 'Button'; 3043 = 'Static'; 7 = 'Button'
}

function Describe($el) {
    if (-not $el) { return '<none>' }
    $c = $el.Current
    $name = [string]$c.Name
    if ($name.Length -gt 40) { $name = $name.Substring(0, 40) }
    return "{0} id='{1}' class='{2}' name='{3}'" -f $c.ControlType.ProgrammaticName, $c.AutomationId, $c.ClassName, $name
}

# Diagnostic listing of a window's UIA tree (control types, IDs, classes; synthetic names only).
function Dump-Tree($root, [int]$max = 60) {
    $all = $root.FindAll($Scope::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    Write-Output "  UIA tree ($($all.Count) elements):"
    $n = 0
    foreach ($e in $all) {
        if ($n++ -ge $max) { break }
        Write-Output "    $(Describe $e)"
    }
}

function Find-Id($parent, [int]$id) {
    $conditions = @((Cond $AE::AutomationIdProperty ([string]$id)))
    if ($ClassOf.ContainsKey($id)) { $conditions += (Cond $AE::ClassNameProperty $ClassOf[$id]) }
    if ($conditions.Count -eq 1) { return $parent.FindFirst($Scope::Descendants, $conditions[0]) }
    $typed = [System.Windows.Automation.Condition[]]$conditions
    $all = New-Object System.Windows.Automation.AndCondition -ArgumentList (,$typed)
    return $parent.FindFirst($Scope::Descendants, $all)
}

function Invoke-El($el) {
    try { $el.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
    catch { Write-Output "  (Invoke failed on $(Describe $el))"; throw }
}
function Set-Value($el, [string]$text) { $el.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue($text) }
# Managed UIA exposes ValuePattern only on single-line edits; multiline edits are Documents with
# TextPattern. Write them with WM_SETTEXT (which sends no EN_CHANGE; the editor re-reads its
# controls at Save/Close) and read them through TextPattern.
function Set-EditText($el, [string]$text) {
    [EditText]::SendMessageW([IntPtr]$el.Current.NativeWindowHandle, 0x000C, [IntPtr]::Zero, $text) | Out-Null
}
function Get-DocumentText($el) {
    return $el.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern).DocumentRange.GetText(-1)
}
function Item-Count($list) {
    $items = Cond $AE::ControlTypeProperty ([System.Windows.Automation.ControlType]::ListItem)
    return $list.FindAll($Scope::Children, $items).Count
}
function Status-Text($window) { return (Find-Id $window $IdStatus).Current.Name }

# UIA radio buttons expose SelectionItem (not Toggle). The app re-reads the radio state at every
# decision point, so a Select that is processed late still applies.
function Choose-Custom($window) {
    $radio = Find-Id $window $IdCustomMode
    $radio.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $prompt = Find-Id $window $IdPrompt
    return [bool](Wait-For { $prompt.Current.IsEnabled })
}

function Read-Settings {
    $path = Join-Path $DataDir 'settings.json'
    if (-not (Test-Path $path)) { return $null }
    return Get-Content -Raw -Encoding UTF8 $path | ConvertFrom-Json
}

function Start-Editor {
    if ($script:app) {
        Stop-Process -Id $script:app.Id -Force -ErrorAction SilentlyContinue
        $script:app.WaitForExit(5000) | Out-Null  # frees the single-instance mutex
    }
    $script:app = Start-Process -FilePath $Exe -ArgumentList '--open-profiles' -PassThru
    return Wait-For { Find-AppWindow 'TypeTideProfiles' } 15000
}

function Close-Discarding($window) {
    Invoke-El (Find-Id $window $IdClose)
    $dialog = Wait-For { Find-AppWindow '#32770' 'TypeTide' }
    if (-not $dialog) { return $false }
    Invoke-El (Find-Id $dialog $IdNo)
    return [bool](Wait-For { -not (Find-AppWindow 'TypeTideProfiles') })
}

function Test-CreateAndSave($window) {
    Write-Output "[create, edit and save]"
    Dump-Tree $window
    $list = Wait-For { Find-Id $window $IdList }
    Check ($null -ne $list) "the profile list is found" (Describe $list)
    if (-not $list) { return }
    $listed = Wait-For { (Item-Count $list) -eq 2 }
    Check ([bool]$listed) "the default configuration lists two profiles" "found $(Item-Count $list)"
    Invoke-El (Find-Id $window $IdAdd)
    Check ([bool](Wait-For { (Item-Count $list) -eq 3 })) "Add appends a profile"
    Set-Value (Find-Id $window $IdName) 'E2E profile & more'
    Check (Choose-Custom $window) "choosing a custom prompt enables the prompt editor"
    Set-EditText (Find-Id $window $IdPrompt) "Line 1`r`n`tLine 2  "
    $preview = Find-Id $window $IdPreview
    $explained = Wait-For { (Get-DocumentText $preview) -like '*Built-in*' }
    Check ([bool]$explained) "the preview explains that a new Built-in profile cannot run a custom prompt"
    Invoke-El (Find-Id $window $IdSave)
    $saved = Wait-For { (Status-Text $window) -like 'Saved*' }
    Check ([bool]$saved) "Save reports success" "status '$(Status-Text $window)'"
    $settings = Read-Settings
    $created = @($settings.profiles | Where-Object { $_.name -eq 'E2E profile & more' })
    Check ($created.Count -eq 1) "the new profile is written to settings.json"
    if ($created.Count -ne 1) { return }
    Check ($created[0].promptMode -eq 'custom' -and $created[0].systemPrompt -eq "Line 1`r`n`tLine 2  ") `
        "the custom prompt is saved exactly"
    Check (@($settings.profiles).Count -eq 3) "settings.json holds three profiles"
    $script:newId = $created[0].id
}

function Test-DraftCancel($window) {
    Write-Output "[cancel discards drafts]"
    Set-Value (Find-Id $window $IdName) 'Draft only'
    Check (Close-Discarding $window) "closing with unsaved edits offers Discard and closes"
    $names = @((Read-Settings).profiles | ForEach-Object { $_.name })
    Check (-not ($names -contains 'Draft only')) "discarded edits never reach settings.json"
}

function Test-OversizePrompt($window) {
    Write-Output "[oversize prompt]"
    Check (Choose-Custom $window) "the first profile switches to a custom prompt"
    Set-EditText (Find-Id $window $IdPrompt) ('x' * 33000)
    Invoke-El (Find-Id $window $IdSave)
    $refused = Wait-For { (Status-Text $window) -like 'Not saved*' }
    Check ([bool]$refused) "an over-long prompt is refused" "status '$(Status-Text $window)'"
    $first = @((Read-Settings).profiles)[0]
    Check ($first.promptMode -eq 'preset' -and $first.systemPrompt -eq '') "nothing truncated was saved"
    Check (Close-Discarding $window) "the refused draft is discarded on close"
}

function Find-ReplacementItem([string]$namePattern) {
    $menu = Find-AppWindow '#32768'  # the popup menu window
    if (-not $menu) { return $null }
    $items = $menu.FindAll($Scope::Descendants,
        (Cond $AE::ControlTypeProperty ([System.Windows.Automation.ControlType]::MenuItem)))
    return $items | Where-Object { $_.Current.Name -like $namePattern } | Select-Object -First 1
}

function Test-DeleteWithReplacement($window, [string]$replacementId) {
    Write-Output "[delete a bound profile with a replacement]"
    Invoke-El (Find-Id $window $IdDelete)
    $menuItem = Wait-For { Find-ReplacementItem 'E2E profile*' }
    Check ($null -ne $menuItem) "deleting a bound profile asks for a replacement"
    if (-not $menuItem) { return }
    Invoke-El $menuItem
    Check ([bool](Wait-For { (Item-Count (Find-Id $window $IdList)) -eq 2 })) "the bound profile is removed"
    Invoke-El (Find-Id $window $IdSave)
    Check ([bool](Wait-For { (Status-Text $window) -like 'Saved*' })) "the deletion is saved" "status '$(Status-Text $window)'"
    $settings = Read-Settings
    Check (@($settings.profiles).Count -eq 2) "settings.json holds two profiles after the delete"
    Check ($settings.shortcut1ProfileId -eq $replacementId -and $settings.selectionProfileId -eq $replacementId) `
        "the entries bound to the deleted profile now use the replacement"
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
        Check ($null -ne $window -and (Item-Count (Find-Id $window $IdList)) -eq 3) "profiles survive a restart"
        if ($window) { Test-OversizePrompt $window }
        $window = Start-Editor
        Check ($null -ne $script:newId) "the created profile id is known"
        if ($window -and $script:newId) { Test-DeleteWithReplacement $window $script:newId }
    }
} finally {
    if ($script:app) { Stop-Process -Id $script:app.Id -Force -ErrorAction SilentlyContinue }
}

if ($script:failures -gt 0) { Write-Output "`n$($script:failures) FAILURE(S)"; exit 1 }
Write-Output "`nall profile editor checks passed"
exit 0
