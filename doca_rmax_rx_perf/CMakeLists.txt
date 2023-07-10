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

cmake_minimum_required(VERSION 3.16...3.24 FATAL_ERROR)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)

project(DocaRmaxRXPerf)

#------------------------------------------------------------------------------
# Include common logic
#
include(ProjectCommon)

verify_compatibility(doca_rmax_rx_perf ARCH aarch64 OS Linux)

#------------------------------------------------------------------------------
# Configure the executable
#
add_executable(doca_rmax_rx_perf)
target_sources(doca_rmax_rx_perf
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/doca_rmax_rx_perf.c
)

find_package(DOCA 2.5.0050 REQUIRED)
target_link_libraries(doca_rmax_rx_perf PRIVATE apps_common_base DOCA::DOCA DOCA::doca-rmax)

