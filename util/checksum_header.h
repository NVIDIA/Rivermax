/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _CHECKSUM_HEADER_H_
#define _CHECKSUM_HEADER_H_

/**
 * A header that is used by the generic sender and receiver to
 * perform sequence (for dropped packets) and checksum checking.
 */
struct ChecksumHeader
{
    uint32_t sequence;
    uint32_t checksum;
};

#endif // _CHECKSUM_HEADER_H_
