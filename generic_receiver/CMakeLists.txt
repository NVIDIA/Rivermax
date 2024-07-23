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

project(GenericReceiver)

#------------------------------------------------------------------------------
# Include common logic
#
include(ProjectCommon)
include(RtThread)

#------------------------------------------------------------------------------
# Configure the executable
#
add_executable(generic_receiver)

target_include_directories(generic_receiver PRIVATE
    .
    ${UTILS_SOURCE_DIR}/
)

target_sources(generic_receiver
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/generic_receiver.cpp
        ${UTILS_SOURCE_DIR}/memory_allocator.cpp
)

find_package(Rivermax 1.51.6 REQUIRED)
target_link_libraries(generic_receiver PRIVATE apps_common_base Rivermax::Rivermax Utils::RtThread)
