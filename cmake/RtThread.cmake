# SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
RtThread
--------

Finds the RtThread Library.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``Utils::RtThread``
  The utility library comprising RtThread functionality

#]=======================================================================]

if (NOT TARGET Utils::RtThread)
    find_package(Threads REQUIRED)
    find_package(Rivermax 1.51.6 REQUIRED)

    set(RT_THREAD_SOURCE_DIR ${PROJECT_SOURCE_DIR}/../util)
    set(LIBS_SOURCE_DIR ${PROJECT_SOURCE_DIR}/../libs)

    add_subdirectory(${PROJECT_SOURCE_DIR}/../libs libs)
    target_link_libraries(rivermax_libs_config INTERFACE Rivermax::Rivermax)

    add_library(UtilsRtThread)
    set_target_properties(UtilsRtThread PROPERTIES CXX_EXTENSIONS OFF)
    target_sources(UtilsRtThread
      PRIVATE
        ${RT_THREAD_SOURCE_DIR}/rt_threads.cpp
        ${RT_THREAD_SOURCE_DIR}/rational.cpp
    )
    target_include_directories(UtilsRtThread PUBLIC ${RT_THREAD_SOURCE_DIR})
    target_link_libraries(UtilsRtThread PRIVATE
        Threads::Threads
        Rivermax::Rivermax
        rivermax_libs_processor
    )
    add_library(Utils::RtThread ALIAS UtilsRtThread)
endif()
