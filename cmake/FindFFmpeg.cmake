# SPDX-FileComment: source from https://github.com/KDE/ffmpegthumbs/commit/d76e5aa530d7aa643fb055096c5ec5436417462e
#
# SPDX-FileCopyrightText: 2006 Matthias Kretz <kretz@kde.org>
# SPDX-FileCopyrightText: 2008 Alexander Neundorf <neundorf@kde.org>
# SPDX-FileCopyrightText: 2011 Michael Jansen <kde@michael-jansen.biz>
# SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

#[=======================================================================[.rst:
FindFFmpeg
------------

Finds the FFmpeg libraries. The finder can be configured to a specific set
of concrete libraries through the means OF COMPONENTS arguments.
By default it comprises: avcodec avdevice avformat avutil swresample swscale.
Each component name refers to a library of the same name.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``FFmpeg::FFmpeg``
  The package of all the specified FFmpeg libraries

``FFmpeg::<LIBRARY> ...
  Each library is represented by such a target, in which the <LIBRARY> 
  specifier is an upper-case version of the library's name

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``FFmpeg_FOUND``
  True if the system has all the required libraries of FFmpeg.
``FFmpeg_<COMPONENT>_FOUND``
  True if the system has the library represented by this COMPONENT.
``FFmpeg_INCLUDE_DIRS``
  Include directories needed to use Rivermax.
``FFmpeg_LIBRARIES``
  Found libraries from the list of the specified COMPONENTS

#]=======================================================================]
include(FindPackageHandleStandardArgs)

#! find_component : searches for the specified component in the system
#
# The macro looks up the libraries and include directories of the 
# component.
#
# \_component: the name of component and of its library
# \_header: the name of the API header file
#
macro(find_component _component _header)
  find_path(${_component}_INCLUDE_DIRS "${_header}" PATH_SUFFIXES ffmpeg)
  find_library(${_component}_LIBRARY NAMES "${_component}" PATH_SUFFIXES ffmpeg)
  
  if (${_component}_LIBRARY AND ${_component}_INCLUDE_DIRS)
    set(FFmpeg_${_component}_FOUND TRUE)
    set(FFmpeg_LINK_LIBRARIES ${FFmpeg_LINK_LIBRARIES} ${${_component}_LIBRARY})
    list(APPEND FFmpeg_INCLUDE_DIRS ${${_component}_INCLUDE_DIRS}) 

    if (NOT TARGET FFmpeg::${_component})
      add_library(FFmpeg_${_component} UNKNOWN IMPORTED)
      set_target_properties(FFmpeg_${_component} PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${${_component}_INCLUDE_DIRS}"
        IMPORTED_LOCATION "${${_component}_LIBRARY}"
      )
      add_library(FFmpeg::${_component} ALIAS FFmpeg_${_component})
    endif ()
  endif ()
  
  mark_as_advanced(${_component}_INCLUDE_DIRS)
  mark_as_advanced(${_component}_LIBRARY)
endmacro()

#------------------------------------------------------------------------------

# The default components
if (NOT FFmpeg_FIND_COMPONENTS)
  set(FFmpeg_FIND_COMPONENTS avcodec avfilter avformat avdevice avutil swresample swscale)
endif ()

# Traverse the user-selected components of the package and find them
set(FFmpeg_INCLUDE_DIRS)
set(FFmpeg_LINK_LIBRARIES)
foreach(_component ${FFmpeg_FIND_COMPONENTS})
  find_component(${_component} lib${_component}/${_component}.h)
endforeach()
mark_as_advanced(FFmpeg_INCLUDE_DIRS)
mark_as_advanced(FFmpeg_LINK_LIBRARIES)

# Handle findings
list(LENGTH FFmpeg_FIND_COMPONENTS FFmpeg_COMPONENTS_COUNT)
find_package_handle_standard_args(FFmpeg REQUIRED_VARS FFmpeg_COMPONENTS_COUNT HANDLE_COMPONENTS)

# Publish targets if succeeded to find the FFmpeg package and the requested components
if (FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
  add_library(FFmpeg INTERFACE)
  set_target_properties(FFmpeg PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${FFmpeg_LINK_LIBRARIES}"
  )
  add_library(FFmpeg::FFmpeg ALIAS FFmpeg)
endif()
