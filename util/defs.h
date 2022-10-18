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

#ifndef TESTS_UTIL_DEFS_H_
#define TESTS_UTIL_DEFS_H_

#define NOT_IN_USE(a) ((void)(a))

#define SLEEP_THRESHOLD_MS (2)

#define CMAX_MIN (4)

#define CB_SIZE_VIDEO (50)
#define CB_SIZE_AUDIO (90)
#define VIDEO_TRO_DEFAULT_MODIFICATION (4)
#define SIZE_OF_EXTENSION_SEQ (2)
#define IPV4_HDR_SIZE (20)
#define UDP_HDR_SIZE (8)
#define VIDEO_FORMAT (0x02) // 2 - 010 - 16:9

#define FHD_HEIGHT (1080)
#define FHD_WIDTH (1920)
#define UHD_HEIGHT (2160)
#define UHD_WIDTH (3840)

#define FHD_TOTAL_LINES_PER_FRAME (1125)

#define PX_IN_422_GRP (2)
#define BYTES_IN_422_10B_GRP (5)
#define BYTES_IN_422_8B_GRP (4)
#define HD_PACKETS_PER_FRAME_422_10B (4320)
#define UHD_PACKETS_PER_FRAME_422_10B (17280)
#define HD_PACKETS_PER_FRAME_422_8B (3240)
#define UHD_PACKETS_PER_FRAME_422_8B (12960)

#define BYTES_PER_PACKET (1200)

#define HD_FRAME_SIZE (HD_PACKETS_PER_FRAME_422_10B * BYTES_PER_PACKET) // 5184000
#define UHD_FRAME_SIZE (UHD_PACKETS_PER_FRAME_422_10B * BYTES_PER_PACKET) // 20736000

const uint32_t HBRM_HEADER_SIZE = 12;
const uint32_t MEDIA_PAYLOAD_BYTES = 1376;
const size_t DEFAULT_MEM_BLOCK_LEN = 10;

#define FRAMES_PER_MEMBLOCK_HD (5)
#define FRAMES_PER_MEMBLOCK_UHD (2)

#define BITS_IN_BYTES (8)

const std::string ANY_IPV4_ADDR = "0.0.0.0";

const std::string START_AVAILABLE_MC_ADDR_JT_NM = "224.0.2.0";
const std::string END_AVAILABLE_MC_ADDR_JT_NM = "239.255.255.255";

const uint32_t RTP_HEADER_EXT_SEQ_NUM_SIZE = 2;
const uint32_t RTP_HEADER_SRD_MIN_SIZE = RTP_HEADER_EXT_SEQ_NUM_SIZE + 2;  // When first SRD length is 0
const uint32_t RTP_HEADER_SRD_SIZE = 6;
const uint32_t RTP_HEADER_SIZE = 12;
const uint32_t RTP_HEADER_SMPTE_2110_20_MAX_SRDS_NUM = 3;
const uint32_t RTP_HEADER_MAX_CSRCS = 15;
const uint32_t RTP_HEADER_CSRC_GRANULARITY_BYTES = 4;

const uint32_t RTP_2110_20_STREAM_MIN_HEADER_SIZE = \
    RTP_HEADER_SIZE + RTP_HEADER_EXT_SEQ_NUM_SIZE + RTP_HEADER_SRD_SIZE;
const uint32_t RTP_2110_20_STREAM_MAX_HEADER_SIZE = \
    RTP_HEADER_SIZE + RTP_HEADER_EXT_SEQ_NUM_SIZE + RTP_HEADER_SRD_SIZE * RTP_HEADER_SMPTE_2110_20_MAX_SRDS_NUM;
const uint32_t RTP_2110_20_WITH_CSRC_STREAM_MAX_HEADER_SIZE = \
    RTP_2110_20_STREAM_MAX_HEADER_SIZE + RTP_HEADER_MAX_CSRCS * RTP_HEADER_CSRC_GRANULARITY_BYTES;
const uint32_t RTP_2022_6_STREAM_MIN_HEADER_SIZE = RTP_HEADER_SIZE;

const uint32_t RAW_2022_6_STREAM_HEADER_SIZE = RTP_2022_6_STREAM_MIN_HEADER_SIZE + HBRM_HEADER_SIZE;

#define ETH_TYPE_802_1Q (0x8100)          /* 802.1Q VLAN Extended Header  */

/* This DSCP value is required by AES67 section 6.2 for audio streams */
#define DSCP_MEDIA_RTP_CLASS (34)
/* TAI is currently ahead of UTC by 37 seconds */
#define LEAP_SECONDS (37)

#define river_align_down_pow2(_n, _alignment) \
    ( (_n) & ~((_alignment) - 1) )

#define river_align_up_pow2(_n, _alignment) \
    river_align_down_pow2((_n) + (_alignment) - 1, _alignment)

// Round up a given 'num' to be a multiple of 'round'
#define round_up(num, round) \
    ((((num) + (round) - 1) / (round)) * (round))

#endif /* TESTS_UTIL_DEFS_H_ */
