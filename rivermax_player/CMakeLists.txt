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

cmake_minimum_required(VERSION 3.17...3.24 FATAL_ERROR)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)

project(RivermaxPlayer)

#------------------------------------------------------------------------------
# Include common logic
#
include(ProjectCommon)

#------------------------------------------------------------------------------
# Configure the executable
#
add_executable(rivermax_player)

# TODO: Fix the deprecated warnings and remove this script-block
if (MSVC)
    target_compile_options(rivermax_player PRIVATE /wd4996)
else()
    target_compile_options(rivermax_player PRIVATE -Wno-deprecated-declarations)
endif()

target_include_directories(rivermax_player PRIVATE
    .
    ${UTILS_SOURCE_DIR}/
)

target_sources(rivermax_player
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/rivermax_player.cpp
        ${UTILS_SOURCE_DIR}/rt_threads.cpp
        ${UTILS_SOURCE_DIR}/rational.cpp
)

include(FetchFFmpeg)
find_package(Rivermax 1.41.11 REQUIRED)
find_package(FFmpeg REQUIRED)
target_link_libraries(rivermax_player PRIVATE apps_common_base Rivermax::Rivermax FFmpeg::FFmpeg)