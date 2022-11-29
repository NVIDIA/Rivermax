NVIDIA Rivermax: _Tools, Apps & Code Samples_
===========

[![GitHub license](https://img.shields.io/github/license/NVIDIA/nvidia-docker?style=flat-square)](https://github.com/nvidia/rivermax/blob/master/License.md)
[![Rivermax SDK repository](https://img.shields.io/badge/Rivermax-SDK-blue?style=flat-square)](https://developer.nvidia.com/networking/rivermax-getting-started)

<img src="https://developer.nvidia.com/sites/default/files/akamai/networking/rivermax/Rivermax_SDK.jpg" width="450" title="NVIDIA Rivermax SDK"/>

Introduction to Rivermax
--------------------

NVIDIA® Rivermax® SDK -  Optimized networking SDK for Media and Data streaming applications.
Rivermax offers a unique IP-based solution for any media and data streaming use case. Rivermax together with NVIDIA GPU accelerated computing technologies unlocks innovation for a  wide range of applications in Media and Entertainment (M&E), Broadcast, Healthcare, Smart Cities and more.

The NVIDIA Rivermax GitHub Repository allows users to build and run sample codes, tools and applications on top of Rivermax. The GitHub includes code samples that won't be part of the official Rivermax SDK but are commonly used by Rivermax customers and engineering teams.
The code is provided As Is - tested only by the code owner and been tested before uploading with a specific hardware and software.

Product release highlights, documentation, platform support, installation and usage guides can be found in the [Rivermax SDK Page](https://developer.nvidia.com/networking/rivermax-getting-started).

Frequently asked questions, customers product highlights, Video link and more are available on the [Rivermax Product Page](https://developer.nvidia.com/networking/rivermax).

 Getting Started
--------------------

**Make sure you have registered to the NVIDIA Rivermax SDK on the DevZone, downloaded the preferred SDK, obtained the hardware and have a valid license.**  

> More details can be found on the [Rivermax SDK Page](https://developer.nvidia.com/networking/rivermax-getting-started).  

Building
--------------------

The repository relies on [CMake](https://cmake.org/) to build binaries, and it can be built both on Linux and Windows.  

> In a lack of any dependency the build will gracefully terminate. For a smoother start, it is better to follow the installation guidelines of the [Rivermax SDK](https://developer.nvidia.com/networking/rivermax-getting-started). 

### CMake Version Compatibility

`CMakeLists.txt` file of each application states its version requirements at the top. Some applications (e.g. the ones that support CUDA) require CMake of version 3.17 or higher. In certain distros default application managers are limited to an older version of CMake. For this reason it is suggested to install CMake using `pip`, which doesn't have such limitations. `pip` itself can be installed on any modern distro using default application manager. 

For example, on Debian-based distros use the following commands:

```shell
$ sudo apt install python3-pip
$ sudo pip install cmake
```

On RHEL-based distros use the following commands:

```shell
$ sudo yum install python3-pip
$ sudo pip<latest-version> install cmake
```
> Note that in some distros there can be several `pip` instances of different versions by default, 
> and when called a specific version needs to be specified (e.g. the last example above). 
> One should strive to use the latest version possible to avoid compatibility issues.

### Build Command-line Parameters

Each code example contains its build instructions in the respective folder's `README.md` file.

Some applications (e.g. `generic_receiver`) can be built with CUDA support to demonstrate GPU utilization. The following boolean build-parameters are supported:

- `RIVERMAX_ENABLE_CUDA` - when set, this flag enables integration with CUDA
- `RIVERMAX_ENABLE_TEGRA` - this flag shall be set along with `RIVERMAX_ENABLE_CUDA` when building for TEGRA-family SoC (e.g. NVIDIA Jetson AGX Xavier).

Following is an example command-line for building with CUDA support:

```shell
$ cmake -B ./build -DRIVERMAX_ENABLE_CUDA=ON
```

> Note that this repo contains useful CMake scripts, e.g. finders for Rivermax and DOCA. Please refer to examples in the application directories.

Usage
--------------------

Each application or sample code provides its usage instructions.

Application List
--------------------

- [**generic_receiver**](generic_receiver/README.md) - A demo for a receiver application that uses Rivermax Generic API

Issues and Contribution
--------------------

We welcome contributions to the Rivermax Apps repository:  
* To contribute and make pull requests, please follow the guidelines outlined in the [Contributing](CONTRIBUTING.md) document.
* Please let us know about any bugs or issues by [filing a new issue](https://github.com/NVIDIA/rivermax/issues/new).
