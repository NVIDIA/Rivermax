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

# ------------------------------------------------------------------------------
# Setup rmax_apps_lib dependency
#
set(RMAX_APPS_LIB "" CACHE FILEPATH "Path to rmax_apps_lib source")

if(NOT EXISTS "${RMAX_APPS_LIB}/io_node/CMakeLists.txt")
    message(FATAL_ERROR "RMAX_APPS_LIB has to be specified and point to installation directory of rmax_apps_lib")
endif()

set(RMAX_XTREAM_APPS "" CACHE INTERNAL "Disable rmax_apps_libs applications")

add_subdirectory(${RMAX_APPS_LIB} "${CMAKE_CURRENT_BINARY_DIR}/ral")
