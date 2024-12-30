# Rivermax Output Generic API Demo Applications

## Description

This directory contains a set of demo applications that demonstrate the usage of
[Rivermax](https://developer.nvidia.com/networking/rivermax) Output Generic API. The most basic
application is `rmx_generic_send_demo_app` which demonstrates how to send a generic stream to the network
using Rivermax Output Generic API. All other applications are built on top of this basic application
and demonstrate more advanced features.

The `RmxOutputGenericApiBase` class provides basic (non Rivermax specific) functionality
for sending generic streams to the network.

## Structure

```
rmx_api_demo_apps/rmx_output_generic_api  # Rivermax Output Generic API demo applications directory
├── README.md                             # This file
├── rmx_output_generic_api_base.cpp       # Rivermax Output Generic API base class implementation
├── rmx_output_generic_api_base.h         # Rivermax Output Generic API base class header
└── rmx_generic_send_demo_app             # Rivermax Output Generic API basic send demo application
```
