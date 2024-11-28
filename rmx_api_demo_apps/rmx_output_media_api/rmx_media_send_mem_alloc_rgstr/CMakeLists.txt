# SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake)

project(RmxMediaSendMemAllocRgstr CXX)

# ------------------------------------------------------------------------------
# Setup rmax_apps_lib dependency
#
include(AddRmaxAppsLib)

# ------------------------------------------------------------------------------
# Include common logic
#
include(ProjectCommon)

# ------------------------------------------------------------------------------
# Configure the executable
#
add_executable(rmx_media_send_mem_alloc_rgstr)

target_include_directories(rmx_media_send_mem_alloc_rgstr
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

target_link_libraries(rmx_media_send_mem_alloc_rgstr PRIVATE rmax-base-app)

target_sources(rmx_media_send_mem_alloc_rgstr
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../rmx_api_base_demo_app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rmx_output_media_api_base.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rmx_media_send_mem_alloc_rgstr.cpp
)
