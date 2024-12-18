# Rivermax Input API Demo Applications

## Description

This directory contains a set of demo applications that demonstrate the usage of
[Rivermax](https://developer.nvidia.com/networking/rivermax) Input API. The most basic
application is `rmx_receive_demo_app` which demonstrates how to receive a stream from
the network using Rivermax Input API. All other applications are built on top of this
basic application and demonstrate more advanced features.

The `RmxInputApiBase` class provides basic (non Rivermax specific) functionality for receiving streams from the network.

## Structure

```
rmx_api_demo_apps/rmx_input_api               # Rivermax Input API demo applications directory
├── README.md                                 # This file
├── rmx_input_api_base.cpp                    # Rivermax Input API base class implementation
├── rmx_input_api_base.h                      # Rivermax Input API base class header
├── rmx_receive_demo_app                      # Rivermax Input API basic receive demo application
├── rmx_memory_allocation_receive_demo_app    # Rivermax Input API memory allocation receive demo application
├── rmx_memory_registration_receive_demo_app  # Rivermax Input API memory registration receive demo application
├── rmx_hds_receive_demo_app                  # Rivermax Input API header-data split receive demo application
└── rmx_multi_source_receive_demo_app         # Rivermax Input API multi source receive demo application
```
