# Profile editor UIA end-to-end test: launches the real TypeTide.exe with --open-profiles in an
# isolated TYPETIDE_DATA_DIR and drives the native editor through UI Automation. Synthetic data
# only: no clipboard, credentials, network or user selection. Run with Windows PowerShell 5.1.
# Exit codes: 0 = all checks passed, 1 = failures, 2 = test environment problem.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$DataDir
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

# Control IDs of TranslationProfilesWindow (exposed by UIA as AutomationId).
$IdList = 3001; $IdAdd = 3002; $IdDelete = 3004
$IdName = 3010; $IdCustomMode = 3021; $IdPrompt = 3023; $IdPreview = 3025
$IdSave = 3041; $IdClose = 3042; $IdStatus = 3043
$IdNo = 7  # IDNO in the Save/Discard/Cancel message box

$AE = [System.Windows.Automation.AutomationElement]
$Scope = [System.Windows.Automation.TreeScope]
$script:failures = 0
$script:app = $null

function Check([bool]$cond, [string]$name, [string]$detail = "") {
    if ($cond) { Write-Output "  ok   $name" }
    else { Write-Output "  FAIL $name $detail"; $script:failures++ }
}

function Cond($property, $value) { New-Object System.Windows.Automation.PropertyCondition($property, $value) }

function Wait-For([scriptblock]$probe, [int]$timeoutMs = 10000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        $result = & $probe
        if ($result) { return $result }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Find-AppWindow([string]$className) {
    $byClass = Cond $AE::ClassNameProperty $className
    $byProcess = Cond $AE::ProcessIdProperty $script:app.Id
    $both = New-Object System.Windows.Automation.AndCondition($byClass, $byProcess)
    return $AE::RootElement.FindFirst($Scope::Children, $both)
}

function Find-Id($parent, [int]$id) { return $parent.FindFirst($Scope::Descendants, (Cond $AE::AutomationIdProperty ([string]$id))) }
function Invoke-El($el) { $el.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
function Select-El($el) { $el.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select() }
function Set-Value($el, [string]$text) { $el.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue($text) }
function Get-Value($el) { return $el.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value }
function Item-Count($list) {
    $items = Cond $AE::ControlTypeProperty ([System.Windows.Automation.ControlType]::ListItem)
    return $list.FindAll($Scope::Children, $items).Count
}
function Status-Text($window) { return (Find-Id $window $IdStatus).Current.Name }

function Read-Settings {
    $path = Join-Path $DataDir 'settings.json'
    if (-not (Test-Path $path)) { return $null }
    return Get-Content -Raw -Encoding UTF8 $path | ConvertFrom-Json
}

function Start-Editor {
    if ($script:app) { Stop-Process -Id $script:app.Id -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500 }
    $script:app = Start-Process -FilePath $Exe -ArgumentList '--open-profiles' -PassThru
    return Wait-For { Find-AppWindow 'TypeTideProfiles' } 15000
}

function Close-Discarding($window) {
    Invoke-El (Find-Id $window $IdClose)
    $dialog = Wait-For { Find-AppWindow '#32770' }
    if (-not $dialog) { return $false }
    Invoke-El (Find-Id $dialog $IdNo)
    return [bool](Wait-For { -not (Find-AppWindow 'TypeTideProfiles') })
}

function Test-CreateAndSave($window) {
    Write-Output "[create, edit and save]"
    $list = Find-Id $window $IdList
    Check ((Item-Count $list) -eq 2) "the default configuration lists two profiles"
    Invoke-El (Find-Id $window $IdAdd)
    Check ((Item-Count $list) -eq 3) "Add appends a profile"
    Set-Value (Find-Id $window $IdName) 'E2E profile & more'
    Select-El (Find-Id $window $IdCustomMode)
    Set-Value (Find-Id $window $IdPrompt) "Line 1`r`n`tLine 2  "
    $preview = Get-Value (Find-Id $window $IdPreview)
    Check ($preview -like '*Built-in*') "the preview explains that a new Built-in profile cannot run a custom prompt"
    Invoke-El (Find-Id $window $IdSave)
    $saved = Wait-For { (Status-Text $window) -like 'Saved*' } 5000
    Check ([bool]$saved) "Save reports success" "status '$(Status-Text $window)'"
    $settings = Read-Settings
    $profile = $settings.profiles | Where-Object { $_.name -eq 'E2E profile & more' }
    Check ($null -ne $profile) "the new profile is written to settings.json"
    Check ($profile.promptMode -eq 'custom' -and $profile.systemPrompt -eq "Line 1`r`n`tLine 2  ") `
        "the custom prompt is saved exactly"
    Check (@($settings.profiles).Count -eq 3) "settings.json holds three profiles"
    return $profile.id
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
    Select-El (Find-Id $window $IdCustomMode)
    Set-Value (Find-Id $window $IdPrompt) ('x' * 33000)
    Invoke-El (Find-Id $window $IdSave)
    $refused = Wait-For { (Status-Text $window) -like 'Not saved*' } 5000
    Check ([bool]$refused) "an over-long prompt is refused" "status '$(Status-Text $window)'"
    $first = @((Read-Settings).profiles)[0]
    Check ($first.promptMode -eq 'preset' -and $first.systemPrompt -eq '') "nothing truncated was saved"
    Check (Close-Discarding $window) "the refused draft is discarded on close"
}

function Test-DeleteWithReplacement($window, [string]$replacementId) {
    Write-Output "[delete a bound profile with a replacement]"
    Invoke-El (Find-Id $window $IdDelete)
    $menuItem = Wait-For {
        $items = $AE::RootElement.FindAll($Scope::Descendants,
            (Cond $AE::ControlTypeProperty ([System.Windows.Automation.ControlType]::MenuItem)))
        $items | Where-Object { $_.Current.Name -like 'E2E profile*' } | Select-Object -First 1
    }
    Check ($null -ne $menuItem) "deleting a bound profile asks for a replacement"
    if (-not $menuItem) { return }
    Invoke-El $menuItem
    Check ([bool](Wait-For { (Item-Count (Find-Id $window $IdList)) -eq 2 })) "the bound profile is removed"
    Invoke-El (Find-Id $window $IdSave)
    Wait-For { (Status-Text $window) -like 'Saved*' } 5000 | Out-Null
    $settings = Read-Settings
    Check (@($settings.profiles).Count -eq 2) "settings.json holds two profiles after the delete"
    Check ($settings.shortcut1ProfileId -eq $replacementId -and $settings.selectionProfileId -eq $replacementId) `
        "the entries bound to the deleted profile now use the replacement"
}

if (-not (Test-Path $Exe)) { Write-Output "missing executable: $Exe"; exit 2 }
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
$env:TYPETIDE_DATA_DIR = $DataDir

try {
    $window = Start-Editor
    Check ($null -ne $window) "the profile editor opens"
    if ($window) {
        $newId = Test-CreateAndSave $window
        Test-DraftCancel $window
        $window = Start-Editor
        Check ($null -ne $window -and (Item-Count (Find-Id $window $IdList)) -eq 3) "profiles survive a restart"
        if ($window) { Test-OversizePrompt $window }
        $window = Start-Editor
        if ($window -and $newId) { Test-DeleteWithReplacement $window $newId }
    }
} finally {
    if ($script:app) { Stop-Process -Id $script:app.Id -Force -ErrorAction SilentlyContinue }
}

if ($script:failures -gt 0) { Write-Output "`n$($script:failures) FAILURE(S)"; exit 1 }
Write-Output "`nall profile editor checks passed"
exit 0
