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

list(APPEND CMAKE_PREFIX_PATH ${PROJECT_BINARY_DIR})
message("-- Detecting/fetching ffmpeg...")

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows" AND ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64")
  set(RIVERMAX_FFMPEG_RELEASE "ffmpeg-master-latest-win64-lgpl-shared.zip")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "amd64")
    set(RIVERMAX_FFMPEG_RELEASE "ffmpeg-master-latest-linux64-lgpl-shared.tar.xz")
  elseif(${CMAKE_SYSTEM_PROCESSOR} STREQUAL aarch64)
    set(RIVERMAX_FFMPEG_RELEASE "ffmpeg-master-latest-linuxarm64-lgpl-shared.tar.xz")
  endif()
endif()

if(NOT RIVERMAX_FFMPEG_RELEASE)
  message(FATAL_ERROR "Platform ${CMAKE_SYSTEM_PROCESSOR} on system ${CMAKE_SYSTEM_NAME} is not supported!")
endif()

if (NOT "${PROJECT_BINARY_DIR}/.local" IN_LIST "${CMAKE_PREFIX_PATH}")
  list(APPEND CMAKE_PREFIX_PATH "${PROJECT_BINARY_DIR}/.local")
endif()

include(FetchContent)
FetchContent_Declare(FFmpeg
  URL  "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/${RIVERMAX_FFMPEG_RELEASE}"
  SOURCE_DIR "${PROJECT_BINARY_DIR}/.local"  
)
FetchContent_MakeAvailable(FFmpeg)
