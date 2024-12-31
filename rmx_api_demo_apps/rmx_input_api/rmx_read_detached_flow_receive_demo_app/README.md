# Rivermax Input API Receive-after-detach Demo Application

## Description

This application demonstrates how to receive data from a detached flow.

Key features include:
 * Attaching and detaching multiple flows to the same receive stream.
 * Using flow\_id to distinguish between different flows.
 * Processing packets from a flow that has already detached but still has
   remaining data in the receive buffer.

## Structure

```
rmx_api_demo_apps/rmx_input_api/rmx_read_detached_flow_receive_demo_app  # Application directory
├── CMakeLists.txt                                                       # CMake build script
├── README.md                                                            # This file
├── rmx_read_detached_flow_receive_demo_app.cpp                          # Application source code
└── rmx_read_detached_flow_receive_demo_app.h                            # Application header
```
