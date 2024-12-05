# Rivermax Output Media API Demo Applications

## Description

This directory contains a set of demo applications that demonstrate the usage of
[Rivermax](https://developer.nvidia.com/networking/rivermax) Output Media API. The most basic
application is `rmx_media_send_demo_app` which demonstrates how to send a media stream to the network
using Rivermax Output Media API. All other applications are built on top of this basic application
and demonstrate more advanced features.

The `RmxOutputMediaApiBase` class provides basic (non Rivermax specific) functionality
for sending media streams to the network.

## Structure

```
rmx_api_demo_apps/rmx_output_media_api         # Rivermax Output Media API demo applications directory
├── README.md                                  # This file
├── rmx_output_media_api_base.cpp              # Rivermax Output Media API base class implementation
├── rmx_output_media_api_base.h                # Rivermax Output Media API base class header
├── rmx_media_send_demo_app                    # Rivermax Output Media API basic send demo application
└── rmx_memory_allocation_media_send_demo_app  # Rivermax Output Media API memory allocation send demo application
```
