# SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

param (
    [switch]$c, # Check Compliance.
    [switch]$s, # Save Initial Settings And Tune.
    [switch]$r, # Restore Settings.
    [switch]$h, # Help.
    [string]$f  # Json File Path.
)

$scriptVersion = "1.0.1"
$maxPagingFileSize = 15360
$separator = $("-" * 50)
$highPerformanceGUID = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c" # "High Performance" GUID.
$visualFXRegistryPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\VisualEffects"
$visualFXRegistryName = "VisualFXSetting"
$visualFXPropertyType = "DWORD"
$bestPerformanceVisualFX = 2
$timeout = '00:00:30'
$scriptName = $myInvocation.MyCommand.Name # Extract just the script name.
$importFilePath = Join-Path $env:TEMP "RivermaxTuning-Import.inf"
$exportFilePath = Join-Path $env:TEMP "RivermaxTuning-ExportedSecuritySettings.inf" 
$seceditSdbFilePath = Join-Path $env:TEMP "secedit.sdb"
$powercfgExe = "powercfg.exe"
$secEditExe = "secedit.exe"
$gpupdateExe = "gpupdate.exe"

$PowerPlanSectionTitle = "POWER PLAN"
$PageSizeSectionTitle = "PAGEFILE SIZE"
$SystemVisualEffectsSectionTitle = "SYSTEM VISUAL EFFECTS"
$SearchIndexingSectionTitle = "SEARCH INDEXING"
$W32TimeSectionTitle = "W32time"
$LargePagesSupportSectionTitle = "LARGE PAGES SUPPORT"

enum SeLockPrivilegeActionType {
    Add
    Reset
}

$generalInfo = @"
NAME
    $scriptName

SYNOPSIS
    Rivermax Windows Tuning Automation script.

SYNTAX
    $scriptName -h
    $scriptName -c
    $scriptName {-s|-r}  -f <json file path>

OPTIONS
    -h
        Display help message.

    -c
        Check system compliance.

    -s  -f <json file path>
        Save initial state to JSON file and tune requirements.

    -r  -f <json file path>
        Restore system settings to the initial state.

Description:
    This script allows you to tune system general settings. 
    You can check system compliance, save initial settings to a JSON file, tune requirements, or restore initial settings.
    You need to specify one of the defined actions above, and provide a file path for saving/restoring original settings of the system 
    as a JSON file.
"@

$usage = @"

Usage:

$generalInfo

Getting Help:
    To obtain help and learn more about the script, run the following command:
    $scriptName -h

"@

$helpText = @"

$generalInfo

RELATED LINKS
    Link to tuning documentation: https://enterprise-support.nvidia.com/s/article/Rivermax-Windows-Performance-Tuning-Guide

EXAMPLE
    To check system compliance:
        $scriptName -c
    
    To Save initial state to JSON file and tune requirements:
        $scriptName -s -f "C:\Users\systemOriginalSettings.json"

    To Restore settings from JSON file:
        $scriptName -s  -f "C:\Users\systemOriginalSettings.json"

    To display this help message:
        $scriptName -h
        
"@

function Write-ColorOutput($ForegroundColor) {
    $fc = $host.UI.RawUI.ForegroundColor
    $host.UI.RawUI.ForegroundColor = $ForegroundColor

    if ($args) {
        Write-Output $args
    } else {
        $input | Write-Output
    }
    
    $host.UI.RawUI.ForegroundColor = $fc
}

function PrintSectionTitle {
    param(
        [string]$Text
    )
    $textSeparatorLength = 30
    $textSeparator = $("-" * $textSeparatorLength)
    Write-ColorOutput Green "`n$textSeparator"
    Write-ColorOutput Green "$Text"
    Write-ColorOutput Green "$textSeparator`n"
}

function PrintWithHighlight {
    param(
        [string]$Text
    )
    
    Write-ColorOutput Yellow "`n$Text `n"
}

function PrintWarnings {
    param(
        [string]$Text
    )
    
    Write-ColorOutput Red "$Text"
}

function Terminate {
    param (
        [string]$ErrorMessage,
        [bool]$ShowUsage
    )
    if ($ShowUsage) {
        $usage
    }
    if ($ErrorMessage) {
        Write-Error $ErrorMessage
    }
    exit 1
}

function SwitchAutoManagedPagefile {
    param (
        [bool]$doEnable
    )
    Get-CimInstance -ClassName Win32_ComputerSystem | Set-CimInstance -Property @{AutomaticManagedPagefile=$doEnable}
}

function CheckAutoManagedPagefile {
    $autoManagedPagefile = Get-CimInstance -ClassName Win32_ComputerSystem | Select-Object AutomaticManagedPagefile | Select-Object -ExpandProperty AutomaticManagedPagefile

    return $autoManagedPagefile
}

function SetPagefileSize {
    param(
        [int]$SizeMB
    )

    $regPath = "HKLM:\System\CurrentControlSet\Control\Session Manager\Memory Management"
    $regName = "PagingFiles"
    $pagefile = "$env:SystemDrive\pagefile.sys $SizeMB $SizeMB"

    Set-ItemProperty -Path $regPath -Name $regName -Value $pagefile
}

function DisableSearchIndexing {
    $disable = Get-WmiObject Win32_Service | Where-Object { $_.Name -eq "wsearch" } | ForEach-Object { $_.StopService(); $_.ChangeStartMode("Disabled") }
}

function EnableSearchIndexing {
    Set-Service -Name WSearch -StartupType Automatic; Start-Service -Name WSearch
}

function GetSearchIndexingStatus {
    $serviceStatus = Get-Service -Name WSearch | Select-Object -ExpandProperty Status

    if ($serviceStatus -in 'Running', 'StartPending') {
        return $true
    }
    return $false
}

function IsW32TimeRunning {
    if (Get-Service -Name 'w32time' -ErrorAction SilentlyContinue) {
        try {
            $status = Get-Service -Name 'w32time' | Select-Object -ExpandProperty Status 
            return $status -eq 'Running'
        } catch {
            Terminate -ErrorMessage "Error: Unable to retrieve w32time status."
        }
    } else {
        return "W32time service is not installed on this system."
    }
}

function IsW32TimeRegistered {
    try {
        $checkCommand = "Get-Service -Name 'w32time' -ErrorAction Stop | Select-Object -Property DisplayName, StartType, Status"
        $output = Invoke-Expression $checkCommand

        return $true
    } catch {
        if ($_.Exception.Message -like '*Cannot find any service*') {
            return $false
        }
        return $true
    }
}

function DisableW32Time {
    if (IsW32TimeRegistered) {
        Invoke-Expression 'Stop-Service w32time -Force'
        Invoke-Expression 'w32tm /unregister'
    } else {
        Write-Output "W32time was not registered. no action needed"
    }
}

function RegisterW32Time {
    w32tm /register
}

function FindExecutable {
    param (
        [string]$FileName
    )
    foreach ($path in $env:PATH -split ';') {
        if ($path) {
            $executablePath = Join-Path $path $FileName
            if (Test-Path $executablePath -PathType Leaf) {
                return $true
            }
        }
    }
    return $false
}

function UpdateSeLockMemoryPrivilege {
    param (
        [SeLockPrivilegeActionType]$Action,   # Specify 'Add' or 'Reset' as the action.
        [string]$CurrentMembers               # Current username to be processed.
    )
    $currentUsername = whoami

    # Ensure the $CurrentMembers variable is defined.
    if (-not $CurrentMembers) {
        $includedMembers = @()
    } else {
        $includedMembers = $CurrentMembers.split()
    }
    if ($Action -eq ([SeLockPrivilegeActionType]::Add)) {
       $includedMembers += "$currentUsername"
    }
    $includedMembers = $includedMembers -join ","
    $newContent = "SeLockMemoryPrivilege = $includedMembers "

    # Remove existing lines with SeLockMemoryPrivilege from $importFilePath.
    (Get-Content $importFilePath) | ForEach-Object {
        if ($_ -notlike "SeLockMemoryPrivilege*") {
            $_
        }
    } | Set-Content $importFilePath

    Add-Content $importFilePath $newContent

    if (-not ((FindExecutable -FileName $secEditExe) -and (FindExecutable -FileName $gpupdateExe))) {
        Terminate -ErrorMessage "Error: Can't Find Executable Files"
    }
    
    # Import the updated configuration using SecEdit.exe.
    & $secEditExe /import /db $secEditExe /cfg $importFilePath
    & $secEditExe /configure /db $secEditExe

    & $gpupdateExe /force
}

function GetSeLockMemoryPrivilegeMembers {
    if (-not (FindExecutable -FileName $secEditExe)) {
        Terminate -ErrorMessage "Error: Can't Find Executable File"
    }
    
    $securitySettings = & $secEditExe /export /cfg $exportFilePath /areas USER_RIGHTS

    return Get-Content $exportFilePath | Select-String -Pattern "SeLockMemoryPrivilege.*=.*" | ForEach-Object { $_ -replace "SeLockMemoryPrivilege = " }
}

function TestLargePagesSupport {
    $currentMembers = GetSeLockMemoryPrivilegeMembers

    $currentUsername = ((whoami).Split("\")[-1]).ToLower()  # Extract only the username.
    $currentDomain = (whoami).Split("\")[0]

    $userInfo = Get-WmiObject -Query "SELECT * FROM Win32_UserAccount WHERE Name = '$currentUsername' AND Domain = '$currentDomain'"

    $SID = $userInfo.SID
    $SID = "*$SID" # Adding '*' in order to match exported security settings file.
    $SID = $SID.ToLower()
    
    $usernameExists = $currentMembers -contains $SID -or $currentMembers -split ',' -contains $SID -or $currentMembers -contains $currentUsername -or $currentMembers -split ',' -contains $currentUsername

    return $usernameExists
}

function CheckPowerShellVersion {
    $minPowerShellVersion = 5
    $psVersion = $PSVersionTable.PSVersion
    if ($psVersion.Major -lt $minPowerShellVersion) {
        Terminate -ErrorMessage "This script requires PowerShell version $($minPowerShellVersion) or later."
    }
}

function CheckWindowsVersion {
    $minWindowsVersion = 10
    $windowsVersion = [System.Environment]::OSVersion.Version
    if ($windowsVersion.Major -lt $minWindowsVersion) {
        Terminate -ErrorMessage "Windows version $($windowsVersion) is not supported.`n" 
    }
}

function CheckSetupIsCompliant {
    CheckPowerShellVersion
    CheckWindowsVersion
}

function GetPowerSchemeGuid {
    if (-not (FindExecutable -FileName $powercfgExe)) {
        Terminate -ErrorMessage "Error: Can't Find Executable File"
    }
    
    $queryResult = & $powercfgExe /query SCHEME_CURRENT SUB_SLEEP STANDBYIDLE
    $match = $queryResult | Select-String 'Power Scheme GUID:\s+([\S]+)'

    if ($match.Matches.Count -eq 0) {
        Write-Error "Power Scheme GUID not found."
        return
    }
    return $match
}

function GetPowerPlan {
    $currentPowerPlanGUID = $(GetPowerSchemeGuid).Matches[0].Groups[1].Value
    return $currentPowerPlanGUID
}

function CheckPowerPlan {
    PrintSectionTitle $PowerPlanSectionTitle

    if ((GetPowerPlan) -eq $highPerformanceGUID) {
        Write-Output "`nPower plan is set to High Performance."
    } else {
        PrintWarnings -Text "`nPower plan is not set to High Performance."
    }
    
    if (-not (FindExecutable -FileName $powercfgExe)) {
        Terminate -ErrorMessage "Error: Can't Find Executable File"
    }

    & $powercfgExe /l    
}

function GetPagefileSize {
    return (Get-WmiObject -Query "SELECT * FROM Win32_PageFileSetting" | Select-Object -ExpandProperty InitialSize)
}

function CheckPagefileSize {
    PrintSectionTitle $PageSizeSectionTitle
    
    $pagingFileSize = GetPagefileSize
    if ($pagingFileSize -eq $maxPagingFileSize) {
        Write-Output "`nPaging file size requirement is met. Current size: $pagingFileSize MB."
    } elseif ($pagingFileSize -eq $null) {
        PrintWarnings -Text "`nPaging file size requirement is not met. Current size: 0 MB." 
    } else {
        PrintWarnings -Text "`nPaging file size requirement is not met. Current size: $pagingFileSize MB."
    }    
}

function GetVisualEffectsVal {
    if (-not (Get-ItemProperty $visualFXRegistryPath).PSObject.Properties.Name -contains $visualFXRegistryName) {
        New-ItemProperty -Path $visualFXRegistryPath -Name $visualFXRegistryName -PropertyType $visualFXPropertyType
    }
    return (Get-ItemProperty -Path $visualFXRegistryPath -Name $visualFXRegistryName | Select-Object -ExpandProperty $visualFXRegistryName)
}

function CheckSystemVisualEffects {
    PrintSectionTitle $SystemVisualEffectsSectionTitle
    
    Write-Output "`nVisualFXSetting Key Values:`n
0 = 'Let Windows choose what is best for my computer'
1 = 'Adjust for best appearance'
2 = 'Adjust for best performance'
3 = 'Custom'`n"

    if ((GetVisualEffectsVal) -eq $bestPerformanceVisualFX) {
        Write-Output "`nVisual effect requirement is met."
    } else {
        PrintWarnings -Text "`nVisual effect requirement is not met." 
    }
    Get-ItemProperty -Path $visualFXRegistryPath -Name $visualFXRegistryName
}

function CheckSearchIndexing {
    PrintSectionTitle $SearchIndexingSectionTitle
    
    if (GetSearchIndexingStatus) {
        PrintWarnings -Text "`nSearch indexing requirement is not met."
    } else {
        Write-Output "`nSearch indexing is not running."
    }
    Get-Service -Name WSearch | Select-Object Status | Format-List
}

function CheckW32time {
    PrintSectionTitle $W32TimeSectionTitle
    
    if (-not (IsW32TimeRegistered)) {
        Write-Output "`nw32time is not registered." 
        return
    }

    PrintWarnings -Text "`nw32time is registered. Requirement is not met." 
    
    if (IsW32TimeRunning) {
        Write-Output "w32time is running."
        return
    }
    Write-Output "w32time is not running." 
}

function CheckLargePagesSupport {
    PrintSectionTitle $LargePagesSupportSectionTitle
    
    $usernameExists = TestLargePagesSupport 
    if ($usernameExists) {
        Write-Output "`nLarge pages is Supported for this user."
        return
    }
    PrintWarnings -Text "`nLarge pages is not Supported for this user."
}

function PrintSummary {
    Write-Output "`n`n$separator`n"
    
    $summary = @"
Summary:
- Power Plan: $(if ($(GetPowerPlan) -eq $highPerformanceGUID) {'Tuned'} else {'Not Tuned'})
- Pagefile Size: $(if ($(GetPagefileSize) -eq $maxPagingFileSize) {'Tuned'} else {'Not Tuned'})
- System Visual Effects: $(if ($(GetVisualEffectsVal) -eq $bestPerformanceVisualFX) {'Tuned'} else {'Not Tuned'})
- Search Indexing: $(if ($(GetSearchIndexingStatus) -eq $false) {'Tuned'} else {'Not Tuned'})
- W32time: $(if (!(IsW32TimeRegistered)) {'Tuned'} else {'Not Tuned'})
- Large Pages Support: $(if (TestLargePagesSupport) {'Tuned'} else {'Not Tuned'})
"@
    
    $summary
    Write-Output "`n$separator`n`n"
}

function SetPowerPlan {
    param (
        [string[]]$PowerPlanGUID
    )
    
    PrintSectionTitle $PowerPlanSectionTitle

    if (-not (FindExecutable -FileName $powercfgExe)) {
        Terminate -ErrorMessage "Error: Can't Find Executable File"
    }
        
    PrintWithHighlight -Text "POWER PLAN BEFORE:"
    & $powercfgExe /l
    & $powercfgExe /setactive $PowerPlanGUID
    PrintWithHighlight -Text "POWER PLAN AFTER:"
    & $powercfgExe /l
}

function SetPagefile {
    param (
        [switch]$Restore,
        [int]$InitialPagingFileSize,
        [bool]$AutoManagedPagefile
    )
    PrintSectionTitle $PageSizeSectionTitle
    
    PrintWithHighlight -Text "PAGEFILE SIZE BEFORE:"
    $pagingFileSize = GetPagefileSize
    if ($pagingFileSize -eq $null) {
        Write-Output "Win32_PageFileSetting MaximumSize: 0"
    } else {
        Get-CimInstance -ClassName Win32_PageFileSetting | Format-List
    }

    SwitchAutoManagedPagefile -doEnable $false
    
    SetPagefileSize $maxPagingFileSize
    PrintWithHighlight -Text "PAGEFILE SIZE AFTER:"
    if ($Restore) {
        if ($InitialPagingFileSize) {
            SetPagefileSize $InitialPagingFileSize
        }
        
        if ($AutoManagedPagefile) {
            SwitchAutoManagedPagefile -doEnable $true
        }
        
        $pagingFileSize = GetPagefileSize
        if ($pagingFileSize) {
            Get-CimInstance -ClassName Win32_PageFileSetting | Format-List
        } else {
            Write-Output "Win32_PageFileSetting MaximumSize: 0"
        }
    } else {
        Get-CimInstance -ClassName Win32_PageFileSetting | Format-List
    }
}

function SetSystemVisualEffects {
    param (
        [string]$VisualFXVal
    )
    PrintSectionTitle $SystemVisualEffectsSectionTitle
    
    Write-Output "`nVisualFXSetting Key Values:
0 = 'Let Windows choose what is best for my computer'
1 = 'Adjust for best appearance'
2 = 'Adjust for best performance'
3 = 'Custom'`n"
    PrintWithHighlight -Text "SYSTEM VISUAL EFFECTS BEFORE:"
    if (-not (Get-ItemProperty $visualFXRegistryPath).PSObject.Properties.Name -contains $visualFXRegistryName) {
        New-ItemProperty -Path $visualFXRegistryPath -Name $visualFXRegistryName -PropertyType $visualFXPropertyType
    }
    Get-ItemProperty -Path $visualFXRegistryPath -Name $visualFXRegistryName
    
    Set-ItemProperty -Path $visualFXRegistryPath -Name $visualFXRegistryName -Value $VisualFXVal
    PrintWithHighlight -Text "SYSTEM VISUAL EFFECTS AFTER:"

    Get-ItemProperty -Path $visualFXRegistryPath -Name $visualFXRegistryName
}

function SetSearchIndexing {
    param (
        [bool]$Restore,
        [bool]$SearchIndexingStatus
    )
    PrintSectionTitle $SearchIndexingSectionTitle
    $svc = Get-Service -Name WSearch
    PrintWithHighlight -Text "SEARCH INDEXING BEFORE:"
    Get-Service -Name WSearch
    PrintWithHighlight -Text "SEARCH INDEXING AFTER:"
    if ($Restore) {
        if ($SearchIndexingStatus) {
            EnableSearchIndexing
            $svc.WaitForStatus('Running',$timeout)
        } elseif ($SearchIndexingStatus -eq $false) {
            DisableSearchIndexing
            $svc.WaitForStatus('Stopped',$timeout)
        } else {
            Write-Output "`n* Unable to determine search indexing status."
        }
        Get-Service -Name WSearch
    } else {
        DisableSearchIndexing
        $svc.WaitForStatus('Stopped',$timeout)
        Get-Service -Name WSearch
    }
}

function SetW32time {
    param (
        [bool]$Restore,
        [bool]$Isw32timeRegistered,
        [string]$Isw32timeRunning
    )
    PrintSectionTitle $W32TimeSectionTitle
    
    PrintWithHighlight -Text "W32time BEFORE:" 
    $w32TimeRegistered = IsW32TimeRegistered
    if ($w32TimeRegistered) {
        Get-Service -Name 'w32time'
    } else {
        Write-Output "w32time is not registered."
    }
    PrintWithHighlight -Text "W32time AFTER:" 
    if ($Restore) {
        if ($Isw32timeRegistered) {
            RegisterW32Time
            if ($Isw32timeRunning) {
                Start-Service w32time
            } else {
                Write-Output "W32time was not running"
            }
        } else {
            Write-Output "W32time was not registered. no action needed"
        }
    } else {
        DisableW32Time
    }
}

function CreateImportInfFile {
    try {
        if (-not (Test-Path $importFilePath)) {
            @'
[Unicode]
Unicode=yes
[System Access]
[Event Audit]
[Registry Values]
[Version]
signature="$CHICAGO$"
Revision=1
[Profile Description]
Description=Adding "(SeLockMemoryPrivilege)" right for user account
[Privilege Rights]
SeLockMemoryPrivilege = 
'@ | Out-File -FilePath $importFilePath -Encoding Unicode
            Write-Output "File $importFilePath created successfully."
        }
    } catch {
        Terminate -Text "An error occurred while creating the file $importFilePath : $_"
    }
}

function SetLargePagesSupport {
    param (
        [switch]$Restore,
        [string]$CurrentMembers
    )
    PrintSectionTitle $LargePagesSupportSectionTitle
    
    CreateImportInfFile    
    PrintWithHighlight -Text "LARGE PAGES SUPPORT BEFORE:"
    TestLargePagesSupport

    PrintWithHighlight -Text "LARGE PAGES SUPPORT AFTER:"
    if ($Restore) {
        UpdateSeLockMemoryPrivilege -Action Reset -CurrentMembers $CurrentMembers
    } else {
        $CurrentMembers = GetSeLockMemoryPrivilegeMembers
        UpdateSeLockMemoryPrivilege -Action Add -CurrentMembers $CurrentMembers
    }
    TestLargePagesSupport
}

function VerifyJSONPath {
    param (
        [bool]$CheckForExistingFile
    )

    if (-not $jsonFilePath) {
        Terminate -ErrorMessage "Error: You must provide a json file path to save/restore settings to/from. ."
    }

    if (-not (Test-Path -Path (Split-Path -Path $jsonFilePath -Parent) -PathType Container)) {
        Terminate -ErrorMessage "Error: The provided path is not a valid path."
    }

    if ($CheckForExistingFile) {
        if (Test-Path -Path $jsonFilePath) {
            Terminate -ErrorMessage "A file named $(Split-Path -Path $jsonFilePath -Leaf) already exists in this directory."
        }
    } elseif (-not (Test-Path -Path $jsonFilePath)) {
        Terminate -ErrorMessage "The file $(Split-Path -Path $jsonFilePath -Leaf) does not exist in the directory '$(Split-Path -Path $jsonFilePath -Parent)'."
    }
}

function StoreOriginalSettings {
    param (
        [object]$SystemOriginalSettings
    )
    try {
        $SystemOriginalSettings | ConvertTo-Json | Set-Content -Path "$jsonFilePath"
        Write-Output "Initial state saved to $jsonFilePath"
    } catch {
        Terminate -ErrorMessage "Error saving initial state to $jsonFilePath : $_"
    }
}

function SaveJSON {
    $currentPowerPlanGUID = GetPowerPlan   
    
    $autoManagedPagefile = CheckAutoManagedPagefile
    $initialPagingFileSize = (GetPagefileSize) -as [int]
    $visualEffectsRegistry = GetVisualEffectsVal
    $searchIndexingStatus = GetSearchIndexingStatus
    $isw32timeRegistered = IsW32TimeRegistered
    $isW32timeRunning = IsW32TimeRunning
    $currentMembers = GetSeLockMemoryPrivilegeMembers
    
    $systemOriginalSettings = @{
        ScriptVersion = $scriptVersion
        PowerPlanGUID = $currentPowerPlanGUID
        InitialPagingFileSize = $initialPagingFileSize
        AutoManagedPagefile = $autoManagedPagefile
        VisualFXSetting = $visualEffectsRegistry
        SearchIndexingStatus = $searchIndexingStatus
        W32timeRegistered = $isw32timeRegistered
        W32timeRunning = $isW32timeRunning
        SeLockMemoryPrivilege = $currentMembers
    }
    StoreOriginalSettings -SystemOriginalSettings $systemOriginalSettings
}

function GetJSONOriginalSettings {
    if (-not (Test-Path "$jsonFilePath")) {
        Terminate -ErrorMessage "Error: $jsonFilePath file not found. Unable to restore initial state."
    }
    
    $systemOriginalSettings = Get-Content -Raw -Path "$jsonFilePath" | ConvertFrom-Json
    
    if ($systemOriginalSettings.ScriptVersion -ne $scriptVersion) {
        Terminate -ErrorMessage "Warning: The version of the script that saved the initial state ($($systemOriginalSettings.ScriptVersion)) does not match the current script version ($scriptVersion). Restoration may not work as expected."
    }  
    return $systemOriginalSettings
}

function CheckCompliance {
    $separator  
    PrintWarnings -Text "Checking requirements...`n"
    
    CheckPowerPlan
    CheckPagefileSize
    CheckSystemVisualEffects
    CheckSearchIndexing
    CheckW32time
    CreateImportInfFile
    CheckLargePagesSupport

    PrintSummary
}

function SaveInitialStateAndTune {
    $separator
    PrintWarnings -Text "Saving initial state and tuning requirements...`n"

    SaveJSON
    SetPowerPlan -PowerPlanGUID $highPerformanceGUID
    SetPagefile
    SetSystemVisualEffects -VisualFXVal $bestPerformanceVisualFX
    SetSearchIndexing
    SetW32time
    SetLargePagesSupport

    PrintSummary
    Write-Output "For achieving better performance, consider disabling Windows Defender, disabling unnecessary tasks and applications, and tuning the BIOS as described within the Rivermax Performance Tuning`n"
    PrintWarnings -Text "ATTENTION: After disabling the various previously mentioned Services, reboot the machine, as some configuration-changes take affect only after a reboot."
}

function RestoreSettings {
    $separator    
    PrintWarnings -Text "Restoring initial state from $jsonFilePath...`n"

    $systemOriginalSettings = GetJSONOriginalSettings

    SetPowerPlan -PowerPlanGUID $systemOriginalSettings.PowerPlanGUID
    SetPagefile -Restore $true -InitialPagingFileSize $systemOriginalSettings.InitialPagingFileSize -AutoManagedPagefile $systemOriginalSettings.AutoManagedPagefile
    SetSystemVisualEffects -VisualFXVal $systemOriginalSettings.VisualFXSetting
    SetSearchIndexing -Restore $true -SearchIndexingStatus $systemOriginalSettings.SearchIndexingStatus
    SetW32time -Restore $true -Isw32timeRegistered $systemOriginalSettings.W32timeRegistered -Isw32timeRunning $systemOriginalSettings.W32timeRunning
    SetLargePagesSupport -Restore $true -CurrentMembers $systemOriginalSettings.SeLockMemoryPrivilege

    PrintSummary
    PrintWarnings -Text "ATTENTION: After disabling the various previously mentioned Services, reboot the machine, as some configuration-changes take affect only after a reboot." 
}

function VerifyInputs {
    try {
        CheckSetupIsCompliant

        $numOfProvidedSwitches = [int]($checkCompliance.IsPresent + $saveInitialSettingsAndTune.IsPresent + $restoreSettings.IsPresent + $help.IsPresent)

        if ($ScriptArgs.Count -ne 0) {
            Terminate -ErrorMessage "$scriptName : A parameter cannot be found that matches parameter name $ScriptArgs" -ShowUsage $true
        } elseif ($numOfProvidedSwitches -gt 1) {
            Terminate -ErrorMessage "$scriptName : Please provide one of the following switches: -c, -s, -r, -h." -ShowUsage $true
        } elseif ($numOfProvidedSwitches -eq 0) {
            Terminate -ShowUsage $true
        }
        
    } catch {
        Terminate -ErrorMessage "$_"
    }
}

function Main {
    param (
        [string[]]$ScriptArgs
    )

    $checkCompliance = $c
    $saveInitialSettingsAndTune = $s
    $restoreSettings = $r
    $help = $h
    $jsonFilePath = $f

    VerifyInputs

    try {
        if ($checkCompliance) {
            CheckCompliance
        } elseif ($saveInitialSettingsAndTune) {
            VerifyJSONPath -CheckForExistingFile:$true 
            SaveInitialStateAndTune
        } elseif ($restoreSettings) {
            VerifyJSONPath
            RestoreSettings
        } elseif ($help) {
            $helpText
        }
    } catch {
        Terminate -ErrorMessage "$_"
    }
}

Main -ScriptArgs $args
