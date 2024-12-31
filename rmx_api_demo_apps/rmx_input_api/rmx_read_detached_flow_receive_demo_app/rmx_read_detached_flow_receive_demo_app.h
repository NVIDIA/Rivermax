/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#ifndef RMX_API_DEMO_APPS_RMX_INPUT_API_RMX_READ_DETACHED_FLOW_RECEIVE_DEMO_APP_RMX_READ_DETACHED_FLOW_RECEIVE_DEMO_APP_H_
#define RMX_API_DEMO_APPS_RMX_INPUT_API_RMX_READ_DETACHED_FLOW_RECEIVE_DEMO_APP_RMX_READ_DETACHED_FLOW_RECEIVE_DEMO_APP_H_

#include "rmx_input_api_base.h"
#include "api/rmax_apps_lib_api.h"

using namespace ral::lib::services;


constexpr const char* APP_DESCRIPTION = "NVIDIA Rivermax API Receive-after-detach Demo App";
constexpr const char* APP_EXAMPLES = \
    "Example:\n"
    "  rmx_read_detached_flow_demo_app --local-ip 1.2.3.4 --src-ip 1.2.10.1 --dst-ips 231.1.2.3,231.1.2.4 --dst-ports 2001,2002";

/**
 * @brief Rivermax Input API Receive-after-detach Demo App.
 *
 * This application demonstrates how to receive data from a detached flow.
 *
 * Key features include:
 * - Attaching and detaching multiple flows to the same receive stream.
 * - Using flow\_id to distinguish between different flows.
 * - Processing packets from a flow that has already detached but still has
 *   remaining data in the receive buffer.
 */
class RmxReadDetachedFlowDemoApp : public RmxAPIBaseDemoApp
{
public:
    RmxReadDetachedFlowDemoApp();
    virtual ~RmxReadDetachedFlowDemoApp() = default;
protected:
    virtual ReturnStatus operator()() override;
    virtual void add_cli_options() override;
    virtual void post_cli_parse_initialization() override;
private:
    /* Source address */
    sockaddr_in m_source_address;
    /* Remote destination addresses */
    std::vector<sockaddr_in> m_destination_address;

    /**
     * @brief Receive packets from the stream for an interval in time.
     *
     * @param [in] stream_id: Stream to receive from.
     * @param [in] sub_block_id: Index of packet sub-block to parse.
     * @param [in] data_stride_size_bytes: Data stride size in bytes.
     *
     * @return Status of the operation.
     */
    ReturnStatus receive_packets(rmx_stream_id stream_id, size_t sub_block_id, size_t data_stride_size_bytes);
};

#endif // RMX_API_DEMO_APPS_RMX_INPUT_API_RMX_READ_DETACHED_FLOW_RECEIVE_DEMO_APP_RMX_READ_DETACHED_FLOW_RECEIVE_DEMO_APP_H_
