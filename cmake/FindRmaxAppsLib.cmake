# SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#[=======================================================================[.rst:
FindRmaxAppsLib
---------------

Finds the Rivermax Applications Library.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``RmaxAppsLib::RmaxAppsLib``
  The Rivermax-Applications library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``RmaxAppsLib_FOUND``
  True if the library was successfully found.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``RMAX_APPS_LIB_PATH``
  The directory containing the library sources.

#]=======================================================================]

cmake_policy(PUSH)
cmake_policy(VERSION 3.17)

include(FindPackageHandleStandardArgs)
include(RtThread)

# Define Rivermax as component of RmaxAppsLib
find_package(Rivermax 1.30.16)
set(RmaxAppsLib_Rivermax_FOUND Rivermax_FOUND)
set(RmaxAppsLib_FIND_REQUIRED_Rivermax TRUE)
list(APPEND RmaxAppsLib_FIND_COMPONENTS Rivermax)

if(DEFINED RMAX_APPS_LIB)
    # Either user or cache have this value
    message(STATUS "RmaxAppsLib: given path = '${RMAX_APPS_LIB}'")
    set(RMAX_APPS_LIB_PATH "${RMAX_APPS_LIB}")
else()
    if (WIN32)
        # On Windows get the path from the installed folder
        set(RMAX_APPS_LIB_PATH "${Rivermax_INCLUDE_DIR}\\..\\rmax_apps_lib")
    else()
        # On Linux attempt to guess the location where it was extracted to
        set(RMAX_APPS_LIB_PATH "$ENV{HOME}\\rmax_apps_lib" CACHE INTERNAL "")
    endif()
    message(STATUS "RmaxAppsLib: attempting search in '${RMAX_APPS_LIB_PATH}'...")
endif()

# Set configurable variable to set path to RmaxAppsLib
set(RMAX_APPS_LIB "${RMAX_APPS_LIB_PATH}" CACHE FILEPATH "Path to rmax_apps_lib source")

# Check the specified path for the availability of the library files
find_path(RmaxAppsLib "api/rmax_apps_lib_api.h"
    PATHS "${RMAX_APPS_LIB}"
    NO_DEFAULT_PATH
)

# Handle findings
find_package_handle_standard_args(RmaxAppsLib
    HANDLE_COMPONENTS
)

if (RmaxAppsLib_FOUND)
    # Create RmaxAppsLib::RmaxAppsLib as complete target library for applications
    if (NOT TARGET RmaxAppsLib::RmaxAppsLib)
        # Find all source files of this library
        file(GLOB_RECURSE RMAX_APPS_LIB_SOURCES "${RMAX_APPS_LIB}/**/*.cpp")

        add_library(RmaxAppsLib)

        # Set compiler characteristics for the library
        target_compile_features(RmaxAppsLib PUBLIC cxx_std_17)
        set_target_properties(RmaxAppsLib PROPERTIES CXX_EXTENSIONS OFF)

        # Construct the contents of the library
        target_sources(RmaxAppsLib PRIVATE
            ${RMAX_APPS_LIB_SOURCES}
        )
        target_include_directories(RmaxAppsLib PUBLIC
            "${RMAX_APPS_LIB}"
            "${RMAX_APPS_LIB}/lib"
            "${RMAX_APPS_LIB}/io_node"
        )
        target_link_libraries(RmaxAppsLib PUBLIC Rivermax::Rivermax Utils::RtThread)

        add_library(RmaxAppsLib::RmaxAppsLib ALIAS RmaxAppsLib)
    endif()
endif()

cmake_policy(POP)
