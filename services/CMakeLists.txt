# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#------------------------------------------------------------------------------
project(rivermax-app-services)
add_library(${PROJECT_NAME} STATIC)

target_include_directories(${PROJECT_NAME} INTERFACE utils)

add_subdirectory(memory_allocation)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        rivermax-dev-kit-build
    PUBLIC
        rivermax-dev-kit-services-legacy-util
        rivermax-dev-kit-services-cpu
)
# This is used to pass the os folder path to the legacy util
get_target_property(rmax_apps_util_source_dir rivermax-dev-kit-services-legacy-util SOURCE_DIR)
set_target_properties(${PROJECT_NAME} PROPERTIES RMAX_APPS_UTIL_SOURCE_DIR ${rmax_apps_util_source_dir})
