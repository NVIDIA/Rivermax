# Rivermax Input API Memory Registration Demo Application

## Description

This application demonstrates an example of explicit memory allocation and registration on application side when using the Rivermax Input API.
When memory allocation and registration are not performed explicitly, Rivermax handles these process internally. The advantage of performing registration on the application side is that it can register one large memory buffer for multiple streams, using a single memory key (mkey). This improves HW cache utilization, as only one mkey is required for multiple streams, making memory management more efficient.
In contrast, when Rivermax handles the registration internally, it registers each stream individually, resulting in worst HW cache utilization due to multiple mkeys being used.

## Structure

```
rmx_api_demo_apps/rmx_input_api/rmx_memory_registration_receive_demo_app  # Application directory
├── CMakeLists.txt                                                        # CMake build script
├── README.md                                                             # This file
├── rmx_memory_registration_receive_demo_app.cpp                          # Application source code
└── rmx_memory_registration_receive_demo_app.h                            # Application header
```
