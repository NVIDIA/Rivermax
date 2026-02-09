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

# ------------------------------------------------------------------------------
# Setup rivermax-dev-kit dependency
#
include(FetchContent)

set(RDK_GIT_HASH "154edc3da714c2dbbce0eb7a111018b13925c797")

message(STATUS "Fetching rivermax-dev-kit")
FetchContent_Declare(
    rivermax-dev-kit
    URL https://github.com/NVIDIA/rivermax-dev-kit/archive/${RDK_GIT_HASH}.zip
)
FetchContent_MakeAvailable(rivermax-dev-kit)
