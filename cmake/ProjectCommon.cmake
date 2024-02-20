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

include_guard(GLOBAL)

#------------------------------------------------------------------------------
# Command-line options
#
#   These options are set ON via command line e.g.:
#       $ cmake -DRIVERMAX_ENABLE_CUDA=ON
#
option(RIVERMAX_ENABLE_CUDA     "Enables CUDA"   OFF)
option(RIVERMAX_ENABLE_TEGRA    "Enables TEGRA"  OFF)

#------------------------------------------------------------------------------

function(verify_compatibility _app_name)
    # Input parameters
    set(_filters ARCH OS)
    cmake_parse_arguments(APP "" "" "${_filters}" ${ARGN})

    # Check HW architecture compatibility
    if (APP_ARCH AND NOT "${CMAKE_SYSTEM_PROCESSOR}" IN_LIST APP_ARCH)
        message(FATAL_ERROR "'${_app_name}' doesn't support architecture '${CMAKE_SYSTEM_PROCESSOR}'!")
        return()
    endif()

    # Check OS compatibility
    if (APP_OS AND NOT "${CMAKE_SYSTEM_NAME}" IN_LIST APP_OS)
        message(FATAL_ERROR "'${_app_name}' doesn't support '${CMAKE_SYSTEM_NAME}'!")
        return()
    endif()    
endfunction()

#------------------------------------------------------------------------------
# Setup configuration
find_package(Threads)

if(RIVERMAX_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
endif()

if (CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
elseif(RIVERMAX_ENABLE_CUDA)
    message(WARNING "Failed to find CUDA on this machine!")
elseif(RIVERMAX_ENABLE_TEGRA)
    message(WARNING "Enabling of TEGRA requires CUDA!")
endif()

set(UTILS_SOURCE_DIR ${PROJECT_SOURCE_DIR}/../util)

#------------------------------------------------------------------------------
# Compiler configuration

add_library(app_compilation_flags INTERFACE)
target_compile_features(app_compilation_flags INTERFACE cxx_std_11)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_definitions(app_compilation_flags INTERFACE 
        _AMD64_ AMD64
        _UNICODE UNICODE
        _WINSOCK_DEPRECATED_NO_WARNINGS
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
    set(RIVERMAX_COMMON_CXX_FLAGS /W3)
else()
    set(RIVERMAX_COMMON_CXX_FLAGS -Wall)
endif()

target_compile_options(app_compilation_flags INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:${RIVERMAX_COMMON_CXX_FLAGS}>
    $<$<COMPILE_LANGUAGE:CUDA>:-m64>
)

#------------------------------------------------------------------------------
# CUDA integration

if (CMAKE_CUDA_COMPILER)
    add_library(app_cuda_integration)
    target_sources(app_cuda_integration
        PRIVATE
            ${UTILS_SOURCE_DIR}/gpu.cpp
            ${UTILS_SOURCE_DIR}/cuda/checksum_kernel.cu
    )
    target_include_directories(app_cuda_integration
        PUBLIC
            ${PROJECT_SOURCE_DIR}/cuda
    )
    target_compile_options(app_cuda_integration PUBLIC -DCUDA_ENABLED)
    target_link_libraries(app_cuda_integration 
        PRIVATE 
            app_compilation_flags
        PUBLIC
            CUDA::cuda_driver
    )

    if (RIVERMAX_ENABLE_TEGRA)
        target_compile_options(app_compilation_flags INTERFACE -DTEGRA_ENABLED)
    else()
        target_link_libraries(app_cuda_integration PUBLIC CUDA::nvml)
    endif()
endif()

#------------------------------------------------------------------------------
# Common base for all applications

add_library(apps_common_base INTERFACE)
target_link_libraries(apps_common_base INTERFACE 
    app_compilation_flags
    Threads::Threads
)

if (CMAKE_CUDA_COMPILER)
    target_link_libraries(apps_common_base INTERFACE app_cuda_integration)
endif()

