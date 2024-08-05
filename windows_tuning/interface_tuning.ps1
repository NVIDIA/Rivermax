# SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
    [string]$i, # Adapter Name.
    [ValidateSet("Both", "Send", "Receive")]
    [string]$p, # Profile.
    [string]$f  # Json Dir Path.
)

$scriptVersion = "1.0.1"
$separator = $("-" * 50)
$scriptName = $myInvocation.MyCommand.Name # Extract just the script name.
$invalidPreferenceIndex = 32767 # Invalid preference index. 

$FlowControl = "FLOW CONTROL"
$NumaSelection = "NUMA SELECTION"
$LocalPortLoopback = "LOCAL PORT LOOPBACK"
$LoopbackPropertyName = "Disable Local Loopback Flags"

enum PlatformCategory {
    undefined
    VM
    BareMetal
}
$global:platformCategory = [PlatformCategory]::undefined

$generalInfo = @"
NAME
    $scriptName

SYNOPSIS
    Rivermax Windows Tuning Automation For Network Adapter Requirements.

SYNTAX
    $scriptName -h
    $scriptName -c  -i <device interface>  [ -p {Both | Send | Receive; default=Both} ] 
    $scriptName {-s|-r}  -i <device interface>  [-p {Both | Send | Receive; default=Both}]  -f <json file path>

OPTIONS
    -h
        Display help message.

    -c  -i <device interface>  [ -p {Both | Send | Receive; default=Both} ] 
        Check adapter compliance.

    -s  -i <device interface>  [ -p {Both | Send | Receive; default=Both} ]  -f <json file path>
        Save initial state to JSON file and tune requirements.

    -r  -i <device interface>  [ -p {Both | Send | Receive; default=Both} ]  -f <json file path>
        Restore initial settings.
"@

$usage = @"

Usage:

$generalInfo

Description:
    This script allows you to tune network adapter settings. 
    You can check adapter compliance, save initial settings to a JSON file, tune requirements, or restore initial settings.
    You need to specify one of the defined actions above, provide an adapter name to perform the action on. You can also
    specify the Profile {Both | Send | Receive; default=Both}, and a file path for saving/restoring original settings of the interface 
    as a JSON file.

Getting Help:
    To obtain more information about the script and its usage, run the following command:
    $scriptName -h

"@

$helpText = @"

$generalInfo

DESCRIPTION
    - This script works only when given a valid NIC.
    - If the [-p] is "Both" or "Send", the script will check, set and reset the selection of the closest NUMA to the 
      NIC, disabling flow control and local Loopback if it's present!
    - If the [-p] is "Receive", the script will only check, set and reset the flow control of the valid NIC given.
    - If [-s] or [-r] is selected you will need to specify the path to the directory to 
      save/restore the original settings.

RELATED LINKS
    Link to tuning documentation: https://enterprise-support.nvidia.com/s/article/Rivermax-Windows-Performance-Tuning-Guide

EXAMPLES
    To check adapter compliance for default profile "both":
        $scriptName -c  -i "Ethernet 2"

    To save initial state and tune requirements for both sending and receiving:
        $scriptName -s  -i "Ethernet 2"  -p "Both"  -f "C:\Users\InterfaceOriginalSettings.json"

    To restore adapter settings for both sending and receiving from json file in the current default directory:
        $scriptName -r  -i "Ethernet" -f "C:\Users\InterfaceOriginalSettings.json"

    To display this help message:
        $scriptName -h

"@

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

function CheckWhetherIsVM {
    if ($global:platformCategory -eq [PlatformCategory]::undefined) {
        $computerSystem = Get-WmiObject -Class Win32_ComputerSystem
        $manufacturer = $computerSystem.Manufacturer
        $model = $computerSystem.Model

        if ($manufacturer -match "Microsoft Corporation" -and $model -match "Virtual" -or
            $manufacturer -match "VMware, Inc." -or $model -match "VMware Virtual Platform" -or
            $manufacturer -match "Xen" -or $model -match "HVM domU" -or
            $manufacturer -match "Amazon EC2" -or $model -match "EC2" -or
            $manufacturer -match "QEMU" -or $model -match "Standard PC" -or
            $manufacturer -match "Red Hat" -or $model -match "KVM") {
            $global:platformCategory = [PlatformCategory]::VM
        } else {
            $global:platformCategory = [PlatformCategory]::BareMetal
        }
    }
    return ($global:platformCategory -eq [PlatformCategory]::VM)
}

function CheckSetupIsCompliant {
    CheckPowerShellVersion
    CheckWindowsVersion
}

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

function PrintDevicePropertyNameFormatted {
    param (
        [string]$PropertyName
    )
    Get-NetAdapterAdvancedProperty -Name $adapterName -DisplayName $PropertyName | Format-Table
}

function GetFlowControl {
    $currentFlowControl = Get-NetAdapterAdvancedProperty -Name $adapterName -DisplayName $FlowControl | Select-Object -ExpandProperty DisplayValue
    return $currentFlowControl
}

function CheckFlowControl {
    if (CheckWhetherIsVM) {
        Write-Output "Skipping flow control check because this is a virtual machine."
    } else {
        $currentFlowControl = GetFlowControl

        if ($currentFlowControl -ne "Disabled") {
            PrintWarnings "`nFlow control should be disabled."
        }
        Write-Output "Flow Control current value: $currentFlowControl"
    }
}

function SetFlowControl {
    param (
        [string]$DisplayValue   
    )
    PrintSectionTitle $FlowControl
    if (CheckWhetherIsVM) {
        Write-Output "Skipping flow hhhhhcontrol check because this is a virtual machine."
        return
    }

    PrintWithHighlight -Text "FLOW CONTROL BEFORE:"
    PrintDevicePropertyNameFormatted -PropertyName $FlowControl
    PrintWithHighlight -Text "FLOW CONTROL AFTER:"

    Set-NetAdapterAdvancedProperty -Name $adapterName -DisplayName $FlowControl -DisplayValue $DisplayValue 
    PrintDevicePropertyNameFormatted -PropertyName $FlowControl
}

function CheckNUMASelection {
    $rssForNumaSelection = Get-NetAdapterRss -Name $adapterName

    $uniquePreferenceIndexes = @($rssForNumaSelection.RssProcessorArray | Select-Object -Unique PreferenceIndex)

    $areOnSameNuma = $uniquePreferenceIndexes.Count -eq 1
    return $areOnSameNuma
}

function SetNumaSelection {
    param (
        [string]$BaseProcessorGroup,
        [string]$MaxProcessorGroup,
        [string]$BaseProcessorNumber,
        [string]$MaxProcessorNumber,
        [string]$NumaNodesCount
    )
    PrintSectionTitle $NumaSelection

    PrintWithHighlight -Text "NetAdapterRss BEFORE:"  
    Get-NetAdapterRss -Name $adapterName

    PrintWithHighlight -Text "NetAdapterRss AFTER:" 
    Set-NetAdapterRss -Name $adapterName -BaseProcessorGroup $baseProcessorGroup -MaxProcessorGroup $maxProcessorGroup -BaseProcessorNumber $baseProcessorNumber -MaxProcessorNumber $maxProcessorNumber -MaxProcessors $numaNodesCount -Profile Closest
    Get-NetAdapterRss -Name $adapterName
}

function SnapshotNuma {
    $rssForNumaSelection = Get-NetAdapterRss -Name $adapterName

    $rssProcessorArray = $rssForNumaSelection.RssProcessorArray | Where-Object { $_.PreferenceIndex -ne $invalidPreferenceIndex }
    
    $closestNumaDistance = ($rssProcessorArray | Sort-Object $_.PreferenceIndex | Select-Object -First 1).PreferenceIndex

    $closestNumaNodes = $rssProcessorArray | Sort-Object $_.PreferenceIndex | Where-Object { $_.PreferenceIndex -eq $closestNumaDistance } | Sort-Object $_.ProcessorNumber

    $baseProcessor = $closestNumaNodes | Select-Object -First 1

    $maxProcessor = $closestNumaNodes | Select-Object -Last 1

    SetNumaSelection -BaseProcessorGroup $baseProcessor.ProcessorGroup -MaxProcessorGroup $maxProcessor.ProcessorGroup -BaseProcessorNumber $baseProcessor.ProcessorNumber -MaxProcessorNumber $maxProcessor.ProcessorNumber -NumaNodesCount $closestNumaNodes.Count
}

function RestoreNuma {
    param (
        [Object]$JsonRssForNuma
    )

    $baseProcessorGroup = $JsonRssForNuma.BaseProcessorGroup
    $maxProcessorGroup = $JsonRssForNuma.MaxProcessorGroup
    $baseProcessorNumber = $JsonRssForNuma.BaseProcessorNumber
    $maxProcessorNumber = $JsonRssForNuma.MaxProcessorNumber
    $maxProcessors = $JsonRssForNuma.MaxProcessors
    $numaProfile = $JsonRssForNuma.Profile

    SetNumaSelection -BaseProcessorGroup $baseProcessorGroup -MaxProcessorGroup $maxProcessorGroup -BaseProcessorNumber $baseProcessorNumber -MaxProcessorNumber $maxProcessorNumber -NumaNodesCount $maxProcessors
}

function GetLocalPortLoopback {
    $currentProperty = Get-NetAdapterAdvancedProperty -Name $adapterName -DisplayName $LoopbackPropertyName
    if ($currentProperty.DisplayValue) {
        return $currentProperty.DisplayValue
    }
    return "Not present"
}

function CheckLocalPortLoopback {
    $localPortLoopbackStatus = GetLocalPortLoopback
    
    if ($localPortLoopbackStatus -eq "Not present") {
        Write-Output "`nLocal Port Loopback current value: $localPortLoopbackStatus"
        return
    }
    
    if ($localPortLoopbackStatus -ne "Disable Unicast and Multicast") {
        PrintWarnings -Text "`nLocal Port Loopback display value should be `"Disable Unicast and Multicast`"."
    }

    Write-Output "Local Port Loopback current value: $localPortLoopbackStatus"
}

function SetLocalPortLoopback {
    param (
        [string]$DisplayValue   
    )

    PrintSectionTitle $LocalPortLoopback

    PrintWithHighlight -Text "LOCAL PORT LOOPBACK BEFORE:"
    PrintDevicePropertyNameFormatted -PropertyName $LoopbackPropertyName

    PrintWithHighlight -Text "LOCAL PORT LOOPBACK AFTER:"
    $localPortLoopbackAfter = Get-NetAdapterAdvancedProperty -Name $adapterName -DisplayName $LoopbackPropertyName

    if (-not [string]::IsNullOrWhiteSpace($localPortLoopbackAfter.DisplayValue)) {    
        Set-NetAdapterAdvancedProperty -Name $adapterName -DisplayName $LoopbackPropertyName -DisplayValue $DisplayValue
        PrintDevicePropertyNameFormatted -PropertyName $LoopbackPropertyName
    } else {
        Write-Output "No action taken. The original state of local loopback was 'Not Present'."
    }   
}

function VerifyJSONPath {
    param (
        [bool]$CheckForExistingFile
    )

    if (-not $jsonFilePath) {
        Terminate -ErrorMessage "Error: You must provide a json file path to save/restore settings to/from."
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

function PrintSummary {
    param (
        [bool]$Receive
    )
    Write-Output "`n`n$separator`n"
    
    if ($Receive) {
        if (CheckWhetherIsVM) {
            $summary = @"
Summary:
- This is a virtual machine, flow control check is not applicable.
"@
        } else {
            $summary = @"
Summary:
- Flow Control: $(if ($(GetFlowControl) -eq 'Disabled') {'Tuned'} else {'Not Tuned'})
"@
        }
    } else {
        if (CheckWhetherIsVM) {
            $summary = @"
Summary:
- Numa Selection: $(if ($(CheckNUMASelection)) {'Tuned'} else {'Not Tuned'})
- Local port loopback: $(if ($(GetLocalPortLoopback) -eq 'Disable Unicast and Multicast' -or $(GetLocalPortLoopback) -eq 'Not present') {'Tuned'} else {'Not Tuned'})
- This is a virtual machine, flow control check is not applicable.
"@
        } else {
            $summary = @"
Summary:
- Numa Selection: $(if ($(CheckNUMASelection)) {'Tuned'} else {'Not Tuned'})
- Flow Control: $(if ($(GetFlowControl) -eq 'Disabled') {'Tuned'} else {'Not Tuned'})
- Local port loopback: $(if ($(GetLocalPortLoopback) -eq 'Disable Unicast and Multicast' -or $(GetLocalPortLoopback) -eq 'Not present') {'Tuned'} else {'Not Tuned'})
"@
        }
    }
    $summary
    Write-Output "`n$separator`n`n"
}

function StoreOriginalSettings {
    param (
        [object]$InterfaceOriginalSettings
    )
    try {
        $InterfaceOriginalSettings | ConvertTo-Json | Set-Content -Path "$jsonFilePath"
        Write-Output "Initial state saved to $jsonFilePath"
    } catch {
        Terminate -ErrorMessage "Error saving initial state to $jsonFilePath : $_"
    }
}

function SaveJSON {
    $rssForNumaSelection = Get-NetAdapterRss -Name $adapterName
    $currentFlowControl = if (CheckWhetherIsVM) {
        "Not Applicable"
    } else {
        Get-NetAdapterAdvancedProperty -Name $adapterName -DisplayName $FlowControl | Select-Object -ExpandProperty DisplayValue
    }
    $currentLocalPortLoopback = Get-NetAdapterAdvancedProperty -Name $adapterName -DisplayName $LoopbackPropertyName

    $interfaceOriginalSettings = @{
        ScriptVersion         = $scriptVersion
        AdapterName           = $adapterName
        NumaSelectionRSS      = $rssForNumaSelection
        FlowControl           = $currentFlowControl
        LoopbackDisplayValue  = $currentLocalPortLoopback.DisplayValue
    }
        
    StoreOriginalSettings -InterfaceOriginalSettings $interfaceOriginalSettings
}

function GetJSONOriginalSettings {
    if (-not (Test-Path "$jsonFilePath")) {
        Terminate -ErrorMessage "Error: $jsonFilePath file not found. Unable to restore initial state."
    }
    
    $interfaceOriginalSettings = Get-Content -Raw -Path "$jsonFilePath" | ConvertFrom-Json
    
    if ($interfaceOriginalSettings.ScriptVersion -ne $scriptVersion) {
        Terminate -ErrorMessage "Error: The version of the script that saved the initial state ($($interfaceOriginalSettings.ScriptVersion)) does not match the current script version ($scriptVersion). Restoration may not work as expected."
    }

    if ($interfaceOriginalSettings.AdapterName -ne $adapterName) {
        Terminate -ErrorMessage "Error: The Adapter Name of the script that saved the initial state ($($interfaceOriginalSettings.AdapterName)) does not match the current script adapter ($adapterName). Restoration may not work as expected."
    }
    return $interfaceOriginalSettings
}

function CheckCompliance {
    $separator
    PrintWarnings -Text "Checking requirements...`n"

    if ($profile -eq "Receive") {
        PrintSectionTitle $FlowControl
        CheckFlowControl
        PrintSummary -Receive $true
    } else {
        PrintSectionTitle $NumaSelection
        $numaRes = CheckNUMASelection
        Get-NetAdapterRss -Name $adapterName
        
        PrintSectionTitle $FlowControl
        CheckFlowControl
        
        PrintSectionTitle $LocalPortLoopback
        CheckLocalPortLoopback
        PrintSummary
    }
}

function SaveInitialStateAndTune {
    $separator
    PrintWarnings -Text "Saving initial state and tuning requirements...`n"

    SaveJSON 

    if ($profile -eq "Receive") {
        SetFlowControl -DisplayValue "Disabled"
        PrintSummary -Receive $true

    } else {
        SnapshotNuma
        SetFlowControl -DisplayValue "Disabled"
        SetLocalPortLoopback -DisplayValue "Disable Unicast and Multicast"
        PrintSummary
    }
}

function RestoreSettings {
    $separator
    PrintWarnings -Text "Restoring initial settings from $jsonFilePath...`n"

    $interfaceOriginalSettings = GetJSONOriginalSettings
    
    if ($profile -eq "Receive") {
        SetFlowControl -DisplayValue $interfaceOriginalSettings.FlowControl
        PrintSummary -Receive $true

    } else {
        RestoreNuma -JsonRssForNuma $interfaceOriginalSettings.NumaSelectionRSS
        SetFlowControl -DisplayValue $interfaceOriginalSettings.FlowControl
        SetLocalPortLoopback -DisplayValue $interfaceOriginalSettings.LoopbackDisplayValue
        PrintSummary
    }
}

function VerifyAdapterName {
    if (-not $adapterName) {
        Terminate -ErrorMessage "Error: You must provide the adapter name you want to tune." -ShowUsage $true
    }
    
    $adapterExists = Get-NetAdapter -Name $adapterName -ErrorAction SilentlyContinue

    if (-not $adapterExists) {
        Terminate -ErrorMessage "Error: Adapter '$adapterName' was not found."
    }
}

function VerifyProfile {
    $validProfiles = @("Receive", "Send", "Both")
    
    if (-not $profile) { 
        $profile = "Both" 
    }
    
    if ($profile.Trim() -notin $validProfiles) {
        Terminate -ErrorMessage "Error: Invalid value for Profile parameter. Valid options are 'Receive', 'Send', default is 'Both'."
    }
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
        } elseif ($help) {
            $helpText
            return
        }
        
        VerifyAdapterName
        VerifyProfile
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
    $adapterName = $i
    $profile = $p
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
        }
    } catch {
        Terminate -ErrorMessage "$_"
    }
}

Main -ScriptArgs $args
