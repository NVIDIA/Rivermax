# Rivermax Tuning Scripts

## Description

This directory contains two scripts designed to automate and optimize the tuning of system and network interface settings for systems using the Rivermax library. 
These scripts help enhance performance by adjusting various system and interface parameters.

## system_tuning Script

The System Tuning script automates the optimization of general system settings. 

#### Key features

- Changing the current power plan to High performance.
- Increasing the paging file size to a maximum of 15360 MB.
- Disabling system visual effects (if possible).
- Disabling search indexing.
- Enabling Large Pages Support.
- Disabling the w32time service.

The script allows you to check system compliance, save initial settings to a JSON file and tune requirements, or restore initial settings.

To see all available options, run:

    system_tuning.ps1 -h

#### Prerequisites

- Windows Server 2019, Windows Server 2022, Windows 10 PRO, Windows 11 PRO, or Windows 20HS.
- Administrative privileges.

#### Example

    system_tuning.ps1 -c
    system_tuning.ps1 -s -f "C:\Users\<some username>\systemOriginalSettings.json"
    system_tuning.ps1 -r -f "C:\Users\<some username>\systemOriginalSettings.json"

#### Limitations

For achieving better performance, consider disabling Windows Defender, disabling unnecessary tasks and applications, and tuning the BIOS as described within the [Rivermax Performance Tuning Guide](https://developer.nvidia.com/downloads/networkingsecuredocumentationriverma-performance-tuning-guidepdf)

## interface_tuning Script

The Interface Tuning script automates the tuning of network adapter settings.

#### Key features

- Tuning NUMA Selection.
- Disabling Flow Control.
- Disabling Local Port Loopback.

The script allows you to check adapter compliance, save initial settings to a JSON file and tune requirements, or restore initial settings. 
You need to specify an adapter name and can also specify the profile {Both | Send | Receive; default=Both}.

To see all available options, run:

    interface_tuning.ps1 -h

#### Prerequisites

- Windows Server 2019, Windows Server 2022, Windows 10 PRO, Windows 11 PRO, or Windows 20HS.
- Administrative privileges.
- Adapter name to be tuned.

#### Profiles

- **'Both'** or **'Send'**: The script will check, set, and reset the selection of the closest NUMA to the NIC, disabling flow control and local Loopback if present.
- **'Receive'**: The script will only check, set, and reset the flow control of the valid NIC given.

#### Example

    interface_tuning.ps1 -c -i "Ethernet 2"
    interface_tuning.ps1 -s -i "Ethernet 2" -p "Both" -f "C:\Users\<some username>\InterfaceOriginalSettings.json"
    interface_tuning.ps1 -r -i "Ethernet 2" -p "Send" -f "C:\Users\<some username>\InterfaceOriginalSettings.json"
