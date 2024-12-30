# Rivermax API Demo Applications

## Description

This directory contains a set of demo applications that demonstrate the usage of
[Rivermax](https://developer.nvidia.com/networking/rivermax) API. The applications
are provided as a source code and can be used as a reference for developing your
own applications.

The applications are written to be as simple as possible and self-contained.
`RmxApiBaseDemoApp` class provides a basic (non Rivermax specific) interface and
functionality for the demo applications. Application main logic is implemented in
`operator()` method of the `RmxApiBaseDemoApp` interface. Each concrete application
class overrides this method to implement its own logic. All Rivermax API calls are
done in this method.

> The code is provided "As Is" and any express or implied warranties, including,
> but not limited to, the implied warranties of merchantability and fitness for a particular
> purpose are disclaimed.

## Build

All the applications in this directory are built using CMake. The following
instructions are common for all applications.

At first you need to download the latest Rivermax package from the
[Rivermax Downloads](https://developer.nvidia.com/networking/rivermax-getting-started)
page. Extract the `rmax_apps_lib` directory from this package somewhere (in our
example the archive is extracted to the user home directory).

From the application directory run the following commands:

```shell
$ cmake -DCMAKE_BUILD_TYPE=Release -DRMAX_APPS_LIB=/home/user/rmax_apps_lib -B ./build
$ cmake --build ./build --config Release --parallel
```

The resulting binary can be found in directory `build`.

## Structure

```
rmx_api_demo_apps              # Root directory
├── README.md                  # This file
├── rmx_api_base_demo_app.cpp  # Base demo application
├── rmx_api_base_demo_app.h    # Base demo application header
├── rmx_input_api              # Rivermax Input API demo applications
├── rmx_output_generic_api     # Rivermax Output Generic API demo applications
└── rmx_output_media_api       # Rivermax Output Media API demo applications
```
