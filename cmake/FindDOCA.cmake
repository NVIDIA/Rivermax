# SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
DOCA
------------

Finds the DOCA libraries that comprise DOCA-Rivermax package, a combination
of DOCA libraries required to access Rivermax functionality

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``DOCA::DOCA``
  The DOCA library

``DOCA::doca-common``, ``DOCA::doca-argp``, ``DOCA::doca-rmax``
  The DOCA libraries comprising the 

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``DOCA_FOUND``
  True if the system has the required DOCA libraries.
``DOCA_VERSION``
  The version of the DOCA package which was found.

For each <library> in {doca-common doca-argp doca-rmax} it will define:

``DOCA_<library>_FOUND``
  True if the system has <library>.

#]=======================================================================]

# Set default set of DOCA components, if none is specified
if(NOT DOCA_FIND_COMPONENTS)
set(DOCA_FIND_COMPONENTS doca-common doca-argp doca-rmax)
endif()

find_package(PkgConfig)
if (NOT PKG_CONFIG_FOUND) 
    message(FATAL_ERROR "DOCA Finder currently depends on pkg-config, which wasn't found on this system.")
endif()

#------------------------------------------------------------------------------
# Identify components and construct DOCA targets for each one

set(DOCA_TARGETS)
foreach(_component ${DOCA_FIND_COMPONENTS})
  # Attempt to find the component with PKG-Config
  pkg_check_modules(DOCA_${_component} ${_component} QUIET IMPORTED_TARGET) 
  if (DOCA_${_component}_FOUND)
    add_library(DOCA::${_component} ALIAS PkgConfig::DOCA_${_component})
    list(APPEND DOCA_TARGETS DOCA::${_component})
  endif()
endforeach()

#------------------------------------------------------------------------------
# Handle findings
#

include(FindPackageHandleStandardArgs)

set(DOCA_VERSION "${DOCA_doca-rmax_VERSION}")
find_package_handle_standard_args(DOCA
  VERSION_VAR DOCA_VERSION
  HANDLE_COMPONENTS
)
if (DOCA_FOUND)
  if (NOT TARGET DOCA::DOCA)
    add_library(DOCA INTERFACE)
    target_compile_options(DOCA 
      INTERFACE -DDOCA_ALLOW_EXPERIMENTAL_API
    )
    target_link_libraries(DOCA INTERFACE ${DOCA_TARGETS})
    add_library(DOCA::DOCA ALIAS DOCA)
  endif()
endif()

#------------------------------------------------------------------------------
# Tidy up
#
unset(DOCA_TARGETS)