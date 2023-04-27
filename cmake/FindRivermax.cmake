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
FindRivermax
------------

Finds the Rivermax library.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``Rivermax::Rivermax``
  The Rivermax library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``Rivermax_FOUND``
  True if the system has the Rivermax library.
``Rivermax_VERSION``
  The version of the Rivermax library which was found.
``Rivermax_INCLUDE_DIR``
  Include path needed to use Rivermax.
``Rivermax_LIBRARY``
  A path to Rivermax library.
``WinOF2_VERSION``
  The version of the WinOF2 which was found (Windows only)

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``Rivermax_INCLUDE_DIR``
  The directory containing ``rivermax_api.h``.
``Rivermax_LIBRARY``
  The path to the Rivermax library.

#]=======================================================================]

cmake_policy(PUSH)
cmake_policy(VERSION 3.17)

include(FindPackageHandleStandardArgs)

set(INCLUDE_PATH_SUFFIXES mellanox Rivermax/include)
set(LIBRARY_PATH_SUFFIXES mellanox Rivermax/lib)

#! find_library_files : finds a library files (its public header and library)
#
# The macro finds the public header and the library at the given locations
#
# \arg:_header        a public header file (e.g. API)
# \arg:_library       a library representing the components
# \arg:_include_path  a variable to store the include path
# \arg:_library_path  a variable to store the library path
#
macro(find_library_files _header _library _include_path _library_path)
  find_path(${_include_path} ${_header} PATH_SUFFIXES ${INCLUDE_PATH_SUFFIXES})
  find_library(${_library_path} NAMES ${_library} PATH_SUFFIXES ${LIBRARY_PATH_SUFFIXES})
endmacro()

#! validate_component : validate availability of the component based on REQUIRED_VARS
#
# - First, this macro adds the specified component into a must-list of the package.
# - If variables of the REQUIRED_VARS group are defined, the macro sets a new
#   variable named 'Rivermax_${_component}_FOUND' and sets it to TRUE.
#
# \arg:_component       a name of the component
# \group:REQUIRED_VARS  a group of names of variables that confirm component's 
#                       availability
#
macro(validate_component _component)
  set(multiValueKeywords REQUIRED_VARS)
  cmake_parse_arguments(${_component} "" "" "${multiValueKeywords}" ${ARGN})
  
  list(APPEND Rivermax_FIND_COMPONENTS ${_component})
  #set(Rivermax_FIND_REQUIRED_${_component} TRUE)
  
  set(Rivermax_${_component}_FOUND TRUE)

  foreach(_item ${${_component}_REQUIRED_VARS})
    mark_as_advanced(${_item})
    set(_variable ${${_item}})
    if(NOT _variable)
      unset(Rivermax_${_component}_FOUND)
      message(WARNING "${_item} not found!")
    endif()
  endforeach()
endmacro()

#! determine_component_version: determines and prints the version of the found component
#
# Attempts to determine the version of the specified component and if its found
# a corresponding STATUS message is printed.
# This macro currently successfully finds versions only for libraries that follow
# the conventional naming of distribution used by linux, i.e. 
#   <lib filename>.<ext>.<major>.<minor>.<patch>
#
# \arg:_component       a name of the component
# \arg:_library   a library representing the components
#
macro(determine_component_version _component _library_path)
  if (${_library_path})
    file(GLOB _filename_with_version_number "${${_library_path}}.*")
    string(REGEX REPLACE ".*\.([0-9]+\.[0-9]+\.[0-9]+)" "\\1" Rivermax_${_component}_VERSION "${_filename_with_version_number}")
    if (Rivermax_${_component}_VERSION)
      message(STATUS "${_component} version ${Rivermax_${_component}_VERSION} found!")
    endif()
    mark_as_advanced(Rivermax_${_component}_VERSION)
  endif()
endmacro()

#! find_component : finds and registers the specified component
#
# The macro finds the public header and the library and if found adds
# the component to dependencies of Rivermax library
#
# \arg:_component a name of the component
# \arg:_library   a library representing the components
#
macro(find_component _component _library)
  find_library(${_component}_LIBRARY NAMES ${_library} PATH_SUFFIXES ${LIBRARY_PATH_SUFFIXES})
  validate_component(${_component} REQUIRED_VARS ${_component}_LIBRARY)
  determine_component_version(${_component} ${_component}_LIBRARY)
endmacro()

#------------------------------------------------------------------------------

if (WIN32)
  list(APPEND CMAKE_PREFIX_PATH $ENV{ProgramW6432}\\Mellanox)
  find_file(WinOF2_BUILD_FILE NAMES build_id.txt PATH_SUFFIXES MLNX_WinOF2)
  validate_component(WinOF2 REQUIRED_VARS WinOF2_BUILD_FILE)
  if (WinOF2_BUILD_FILE)
    file(STRINGS "${WinOF2_BUILD_FILE}" Rivermax_WinOF2_VERSION REGEX "^Version:[ \t]*[0-9\.]+" )
    string(REGEX REPLACE "^Version:[ \t]*([0-9\.]+)" "\\1" Rivermax_WinOF2_VERSION "${Rivermax_WinOF2_VERSION}")
    message(STATUS "WindOF2 version ${Rivermax_WinOF2_VERSION} found!")
    mark_as_advanced(Rivermax_WinOF2_VERSION)
  endif()
else()
  find_component(DPCP dpcp)
endif()

# Identify Rivermax files and version
find_library_files(rivermax_defs.h rivermax Rivermax_INCLUDE_DIR Rivermax_LIBRARY)
if (Rivermax_INCLUDE_DIR)
  file(STRINGS ${Rivermax_INCLUDE_DIR}/rivermax_defs.h _RMX_DEFS REGEX "^#define[ \t]+RMX_VERSION_(MAJOR|MINOR|PATCH)[ \t]")
  if(_RMX_DEFS)
    foreach(_version_item "MAJOR" "MINOR" "PATCH")
      string(REGEX REPLACE ".*${_version_item}[ \t]+([0-9]+).*" "\\1" Rivermax_VERSION_${_version_item} "${_RMX_DEFS}")
    endforeach()
    set(Rivermax_VERSION "${Rivermax_VERSION_MAJOR}.${Rivermax_VERSION_MINOR}.${Rivermax_VERSION_PATCH}")
    mark_as_advanced(Rivermax_VERSION Rivermax_VERSION_MAJOR Rivermax_VERSION_MINOR Rivermax_VERSION_PATCH)
  else()
    message(WARNING "Failed to determine the version of Rivermax!")
  endif()
  unset(_RMX_DEFS)
endif()

# Handle findings
find_package_handle_standard_args(Rivermax
  REQUIRED_VARS Rivermax_LIBRARY Rivermax_INCLUDE_DIR
  VERSION_VAR Rivermax_VERSION
  HANDLE_COMPONENTS
)
if (Rivermax_FOUND)
  if (NOT TARGET Rivermax::Rivermax)
    add_library(Rivermax::Rivermax UNKNOWN IMPORTED)
    set_target_properties(Rivermax::Rivermax PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${Rivermax_INCLUDE_DIR}"
      IMPORTED_LOCATION "${Rivermax_LIBRARY}"
      VERSION ${Rivermax_VERSION}
    )
  endif()
endif()

cmake_policy(POP)
