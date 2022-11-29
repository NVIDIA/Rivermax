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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <time.h>
#include <unistd.h>

#include <doca_argp.h>
#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_rmax.h>

DOCA_LOG_REGISTER(DOCA_RMAX_PERF);

#define APP_NAME "doca_rmax_perf"
#define APP_VERSION "1.0"

#define MAX_BUFFERS 2

#define DOCA_RMAX_CPUELT(_cpu)  ((_cpu) / DOCA_RMAX_NCPUBITS)
#define DOCA_RMAX_CPUMASK(_cpu) ((doca_rmax_cpu_mask_t) 1 << ((_cpu) % DOCA_RMAX_NCPUBITS))
#define DOCA_RMAX_CPU_SET(_cpu, _cpusetp) \
	do { \
		size_t _cpu2 = (_cpu); \
		if (_cpu2 < (8 * sizeof(struct doca_rmax_cpu_affinity_mask))) \
			(((doca_rmax_cpu_mask_t *)((_cpusetp)->cpu_bits))[DOCA_RMAX_CPUELT(_cpu2)] |= \
			 DOCA_RMAX_CPUMASK(_cpu2)); \
	} while (0)

struct perf_app_config {
	bool list;
	bool dump;
	enum doca_rmax_in_stream_type type;
	enum doca_rmax_in_stream_scatter_type scatter_type;
	enum doca_rmax_in_stream_ts_fmt_type tstamp_format;
	struct in_addr dev_ip;
	struct in_addr dst_ip;
	struct in_addr src_ip;
	struct in_addr clock_ip;
	uint16_t dst_port;
	uint16_t hdr_size;
	uint16_t data_size;
	uint32_t num_elements;
	bool affinity_mask_set;
	struct doca_rmax_cpu_affinity_mask affinity_mask;
	useconds_t sleep_us;
	uint32_t min_packets;
	uint32_t max_packets;
};

struct globals {
	struct doca_mmap *mmap;
	struct doca_buf_inventory *inventory;
	struct doca_workq *wq;
};

struct stream_data {
	size_t num_buffers;
	struct timespec start;
	struct doca_rmax_in_stream *stream;
	struct doca_buf *buffer;
	struct doca_rmax_flow *flow;
	uint16_t pkt_size[MAX_BUFFERS];
	uint16_t stride_size[MAX_BUFFERS];
	/* statistics */
	size_t recv_pkts;
	size_t recv_bytes;
};

noreturn
doca_error_t print_version(void *param, void *config)
{
	printf("%s version: %s\n", APP_NAME, APP_VERSION);
	printf("DOCA SDK     version (Compilation): %s\n", doca_version());
	printf("DOCA Runtime version (Runtime):     %s\n", doca_version_runtime());
	/* We assume that when printing DOCA's versions there is no need to continue the program's execution */
	exit(EXIT_SUCCESS);
}

static void init_config(struct perf_app_config *config)
{
	config->list = false;
	config->dump = false;
	config->type = DOCA_RMAX_IN_STREAM_TYPE_GENERIC;
	config->scatter_type = DOCA_RMAX_IN_STREAM_SCATTER_TYPE_RAW;
	config->tstamp_format = DOCA_RMAX_IN_STREAM_TS_FMT_TYPE_RAW_COUNTER;
	config->dev_ip.s_addr = 0;
	config->dst_ip.s_addr = 0;
	config->src_ip.s_addr = 0;
	config->clock_ip.s_addr = 0;
	config->dst_port = 0;
	config->hdr_size = 0;
	config->data_size = 1500;
	config->num_elements = 1024;
	config->affinity_mask_set = false;
	memset(&config->affinity_mask, 0, sizeof(config->affinity_mask));
	config->sleep_us = 0;
	config->min_packets = 0;
	config->max_packets = 0;
}

static doca_error_t set_list_flag(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;

	config->list = true;

	return DOCA_SUCCESS;
}

static doca_error_t set_type_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const char *str = (const char *)param;

	if (strcasecmp(str, "GENERIC") == 0)
		config->type = DOCA_RMAX_IN_STREAM_TYPE_GENERIC;
	else if (strcasecmp(str, "RTP-2110") == 0)
		config->type = DOCA_RMAX_IN_STREAM_TYPE_RTP_2110;
	else {
		DOCA_LOG_ERR("unknown stream type '%s' was specified", str);
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

static doca_error_t set_scatter_type_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const char *str = (const char *)param;

	if (strcasecmp(str, "RAW") == 0)
		config->scatter_type = DOCA_RMAX_IN_STREAM_SCATTER_TYPE_RAW;
	else if (strcasecmp(str, "ULP") == 0)
		config->scatter_type = DOCA_RMAX_IN_STREAM_SCATTER_TYPE_ULP;
	else if (strcasecmp(str, "PAYLOAD") == 0)
		config->scatter_type = DOCA_RMAX_IN_STREAM_SCATTER_TYPE_PAYLOAD;
	else {
		DOCA_LOG_ERR("unknown scatter type '%s' was specified", str);
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

static doca_error_t set_tstamp_format_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const char *str = (const char *)param;

	if (strcasecmp(str, "counter") == 0)
		config->tstamp_format = DOCA_RMAX_IN_STREAM_TS_FMT_TYPE_RAW_COUNTER;
	else if (strcasecmp(str, "nano") == 0)
		config->tstamp_format = DOCA_RMAX_IN_STREAM_TS_FMT_TYPE_RAW_NANO;
	else if (strcasecmp(str, "synced") == 0)
		config->tstamp_format = DOCA_RMAX_IN_STREAM_TS_FMT_TYPE_SYNCED;
	else {
		DOCA_LOG_ERR("unknown timestamp format '%s' was specified", str);
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

static doca_error_t set_ip_param(const char *label, const char *str, struct in_addr *out)
{
	unsigned int ip[4];
	union {
		uint8_t octet[4];
		uint32_t addr;
	} addr;
	char dummy;

	if (sscanf(str, "%u.%u.%u.%u%c", &ip[0], &ip[1], &ip[2], &ip[3], &dummy) == 4
			&& ip[0] < 256 && ip[1] < 256 && ip[2] < 256 && ip[3] < 256) {
		addr.octet[0] = ip[0];
		addr.octet[1] = ip[1];
		addr.octet[2] = ip[2];
		addr.octet[3] = ip[3];
		out->s_addr = addr.addr;
	} else {
		DOCA_LOG_ERR("bad %s IP address format '%s'", label, str);
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

static doca_error_t set_dev_ip_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const char *str = (const char *)param;

	return set_ip_param("local interface", str, &config->dev_ip);
}

static doca_error_t set_dst_ip_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const char *str = (const char *)param;

	return set_ip_param("destination", str, &config->dst_ip);
}

static doca_error_t set_src_ip_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const char *str = (const char *)param;

	return set_ip_param("source", str, &config->src_ip);
}

static doca_error_t set_clock_ip_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const char *str = (const char *)param;

	return set_ip_param("clock source", str, &config->clock_ip);
}

static doca_error_t set_dst_port_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const int value = *(const int *)param;

	if (value > 0 && value <= UINT16_MAX)
		config->dst_port = (uint16_t)value;
	else {
		DOCA_LOG_ERR("bad source port '%d' was specified", value);
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

static doca_error_t set_hdr_size_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const int value = *(const int *)param;

	if (value >= 0 && value <= UINT16_MAX)
		config->hdr_size = (uint16_t)value;
	else {
		DOCA_LOG_ERR("bad header size '%d' was specified", value);
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

static doca_error_t set_data_size_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const int value = *(const int *)param;

	if (value >= 0 && value <= UINT16_MAX)
		config->data_size = (uint16_t)value;
	else {
		DOCA_LOG_ERR("bad data size '%d' was specified", value);
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

static doca_error_t set_num_elements_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const int value = *(const int *)param;

	if (value > 0 && value <= UINT32_MAX)
		config->num_elements = (uint32_t)value;
	else {
		DOCA_LOG_ERR("bad number of elements '%d' was specified", value);
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

static doca_error_t set_cpu_affinity_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const char *input = (const char *)param;
	char *str, *alloc;

	alloc = str = strdup(input);
	if (str == NULL) {
		DOCA_LOG_ERR("unable to allocate memory: %s", strerror(errno));
		return DOCA_ERROR_NO_MEMORY;
	}

	while ((str = strtok(str, ",")) != NULL) {
		int idx;
		char dummy;

		if (sscanf(str, "%d%c", &idx, &dummy) != 1 || idx < 0 || idx >= DOCA_RMAX_CPU_SETSIZE) {
			DOCA_LOG_ERR("bad CPU index '%s' was specified", str);
			free(alloc);
			return DOCA_ERROR_INVALID_VALUE;
		}

		DOCA_RMAX_CPU_SET(idx, &config->affinity_mask);

		str = NULL;
	}

	config->affinity_mask_set = true;
	free(alloc);

	return DOCA_SUCCESS;
}

static doca_error_t set_sleep_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const int value = *(const int *)param;

	if (value > 0)
		config->sleep_us = value;
	else {
		DOCA_LOG_ERR("bad sleep duration '%d' was specified", value);
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

static doca_error_t set_min_packets_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const int value = *(const int *)param;

	if (value >= 0 || value > UINT32_MAX)
		config->min_packets = value;
	else {
		DOCA_LOG_ERR("bad minimum packets count '%d' was specified", value);
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

static doca_error_t set_max_packets_param(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;
	const int value = *(const int *)param;

	if (value >= 0 || value > UINT32_MAX)
		config->max_packets = value;
	else {
		DOCA_LOG_ERR("bad maximum packets count '%d' was specified", value);
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

static doca_error_t set_dump_flag(void *param, void *opaque)
{
	struct perf_app_config *config = (struct perf_app_config *)opaque;

	config->dump = true;

	return DOCA_SUCCESS;
}

bool register_argp_params(void)
{
	doca_error_t ret;
	struct doca_argp_param *list_flag;
	struct doca_argp_param *type_param;
	struct doca_argp_param *scatter_type_param;
	struct doca_argp_param *tstamp_format_param;
	struct doca_argp_param *dev_ip_param;
	struct doca_argp_param *dst_ip_param;
	struct doca_argp_param *src_ip_param;
	struct doca_argp_param *dst_port_param;
	struct doca_argp_param *hdr_size_param;
	struct doca_argp_param *data_size_param;
	struct doca_argp_param *num_elements_param;
	struct doca_argp_param *cpu_affinity_param;
	struct doca_argp_param *clock_ip_param;
	struct doca_argp_param *min_packets_param;
	struct doca_argp_param *max_packets_param;
	struct doca_argp_param *sleep_param;
	struct doca_argp_param *dump_flag;

	/* --list flag */
	ret = doca_argp_param_create(&list_flag);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_long_name(list_flag, "list");
	doca_argp_param_set_description(list_flag, "List available devices");
	doca_argp_param_set_callback(list_flag, set_list_flag);
	doca_argp_param_set_type(list_flag, DOCA_ARGP_TYPE_BOOLEAN);
	ret = doca_argp_register_param(list_flag);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* -t,--stream-type parameter */
	ret = doca_argp_param_create(&type_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_short_name(type_param, "t");
	doca_argp_param_set_long_name(type_param, "stream-type");
	doca_argp_param_set_description(type_param, "Stream type: generic (default) or RTP-2110");
	doca_argp_param_set_callback(type_param, set_type_param);
	doca_argp_param_set_type(type_param, DOCA_ARGP_TYPE_STRING);
	ret = doca_argp_register_param(type_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* --scatter-type parameter */
	ret = doca_argp_param_create(&scatter_type_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_long_name(scatter_type_param, "scatter-type");
	doca_argp_param_set_description(scatter_type_param, "Scattering type: RAW (default), ULP or payload");
	doca_argp_param_set_callback(scatter_type_param, set_scatter_type_param);
	doca_argp_param_set_type(scatter_type_param, DOCA_ARGP_TYPE_STRING);
	ret = doca_argp_register_param(scatter_type_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* --tstamp-format parameter */
	ret = doca_argp_param_create(&tstamp_format_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_long_name(tstamp_format_param, "tstamp-format");
	doca_argp_param_set_description(tstamp_format_param, "Timestamp format: counter (default), nano or synced");
	doca_argp_param_set_callback(tstamp_format_param, set_tstamp_format_param);
	doca_argp_param_set_type(tstamp_format_param, DOCA_ARGP_TYPE_STRING);
	ret = doca_argp_register_param(tstamp_format_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* -i,--interface-ip parameter */
	ret = doca_argp_param_create(&dev_ip_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_short_name(dev_ip_param, "i");
	doca_argp_param_set_long_name(dev_ip_param, "interface-ip");
	doca_argp_param_set_description(dev_ip_param, "IP of the local interface to receive data");
	doca_argp_param_set_callback(dev_ip_param, set_dev_ip_param);
	doca_argp_param_set_type(dev_ip_param, DOCA_ARGP_TYPE_STRING);
	ret = doca_argp_register_param(dev_ip_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* -m,--multicast-dst parameter */
	ret = doca_argp_param_create(&dst_ip_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_short_name(dst_ip_param, "m");
	doca_argp_param_set_long_name(dst_ip_param, "multicast-dst");
	doca_argp_param_set_description(dst_ip_param, "Multicast address to bind to");
	doca_argp_param_set_callback(dst_ip_param, set_dst_ip_param);
	doca_argp_param_set_type(dst_ip_param, DOCA_ARGP_TYPE_STRING);
	ret = doca_argp_register_param(dst_ip_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* -s,--multicast-src parameter */
	ret = doca_argp_param_create(&src_ip_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_short_name(src_ip_param, "s");
	doca_argp_param_set_long_name(src_ip_param, "multicast-src");
	doca_argp_param_set_description(src_ip_param, "Source address to read from");
	doca_argp_param_set_callback(src_ip_param, set_src_ip_param);
	doca_argp_param_set_type(src_ip_param, DOCA_ARGP_TYPE_STRING);
	ret = doca_argp_register_param(src_ip_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* -p,--port parameter */
	ret = doca_argp_param_create(&dst_port_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_short_name(dst_port_param, "p");
	doca_argp_param_set_long_name(dst_port_param, "port");
	doca_argp_param_set_description(dst_port_param, "Destination port to read from");
	doca_argp_param_set_callback(dst_port_param, set_dst_port_param);
	doca_argp_param_set_type(dst_port_param, DOCA_ARGP_TYPE_INT);
	ret = doca_argp_register_param(dst_port_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* -r,--header-size parameter */
	ret = doca_argp_param_create(&hdr_size_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_short_name(hdr_size_param, "r");
	doca_argp_param_set_long_name(hdr_size_param, "header-size");
	doca_argp_param_set_description(hdr_size_param, "Header size (default 0)");
	doca_argp_param_set_callback(hdr_size_param, set_hdr_size_param);
	doca_argp_param_set_type(hdr_size_param, DOCA_ARGP_TYPE_INT);
	ret = doca_argp_register_param(hdr_size_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* -d,--data-size parameter */
	ret = doca_argp_param_create(&data_size_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_short_name(data_size_param, "d");
	doca_argp_param_set_long_name(data_size_param, "data-size");
	doca_argp_param_set_description(data_size_param, "Data size (default 1500)");
	doca_argp_param_set_callback(data_size_param, set_data_size_param);
	doca_argp_param_set_type(data_size_param, DOCA_ARGP_TYPE_INT);
	ret = doca_argp_register_param(data_size_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* -k,--packets parameter */
	ret = doca_argp_param_create(&num_elements_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_short_name(num_elements_param, "k");
	doca_argp_param_set_long_name(num_elements_param, "packets");
	doca_argp_param_set_description(num_elements_param, "Number of packets to allocate memory for (default 1024)");
	doca_argp_param_set_callback(num_elements_param, set_num_elements_param);
	doca_argp_param_set_type(num_elements_param, DOCA_ARGP_TYPE_INT);
	ret = doca_argp_register_param(num_elements_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* -a,--cpu-affinity parameter */
	ret = doca_argp_param_create(&cpu_affinity_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_short_name(cpu_affinity_param, "a");
	doca_argp_param_set_long_name(cpu_affinity_param, "cpu-affinity");
	doca_argp_param_set_description(cpu_affinity_param, "Comma separated list of CPU affinity cores for the application main thread");
	doca_argp_param_set_callback(cpu_affinity_param, set_cpu_affinity_param);
	doca_argp_param_set_type(cpu_affinity_param, DOCA_ARGP_TYPE_STRING);
	ret = doca_argp_register_param(cpu_affinity_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

#if 0
	/* --clock-device parameter */
	ret = doca_argp_param_create(&clock_ip_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_long_name(clock_ip_param, "clock-device");
	doca_argp_param_set_description(clock_ip_param, "IP address of NIC to be used as clock source");
	doca_argp_param_set_callback(clock_ip_param, set_clock_ip_param);
	doca_argp_param_set_type(clock_ip_param, DOCA_ARGP_TYPE_STRING);
	ret = doca_argp_register_param(clock_ip_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}
#else
	/* disabled because of argp parameters count limitation */
	(void)clock_ip_param;
	(void)set_clock_ip_param;
#endif

	/* --sleep parameter */
	ret = doca_argp_param_create(&sleep_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_long_name(sleep_param, "sleep");
	doca_argp_param_set_description(sleep_param, "Amount of microseconds to sleep between requests (default 0)");
	doca_argp_param_set_callback(sleep_param, set_sleep_param);
	doca_argp_param_set_type(sleep_param, DOCA_ARGP_TYPE_INT);
	ret = doca_argp_register_param(sleep_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* --min parameter */
	ret = doca_argp_param_create(&min_packets_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_long_name(min_packets_param, "min");
	doca_argp_param_set_description(min_packets_param, "Block until at least this number of packets are received (default 0)");
	doca_argp_param_set_callback(min_packets_param, set_min_packets_param);
	doca_argp_param_set_type(min_packets_param, DOCA_ARGP_TYPE_INT);
	ret = doca_argp_register_param(min_packets_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* --max parameter */
	ret = doca_argp_param_create(&max_packets_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_long_name(max_packets_param, "max");
	doca_argp_param_set_description(max_packets_param, "Maximum number of packets to return in one completion");
	doca_argp_param_set_callback(max_packets_param, set_max_packets_param);
	doca_argp_param_set_type(max_packets_param, DOCA_ARGP_TYPE_INT);
	ret = doca_argp_register_param(max_packets_param);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* --dump flag */
	ret = doca_argp_param_create(&dump_flag);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(ret));
		return false;
	}
	doca_argp_param_set_long_name(dump_flag, "dump");
	doca_argp_param_set_description(dump_flag, "Dump packet content");
	doca_argp_param_set_callback(dump_flag, set_dump_flag);
	doca_argp_param_set_type(dump_flag, DOCA_ARGP_TYPE_BOOLEAN);
	ret = doca_argp_register_param(dump_flag);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(ret));
		return false;
	}

	/* version callback */
	ret = doca_argp_register_version_callback(print_version);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register version callback: %s", doca_get_error_string(ret));
		return false;
	}

	return true;
}

bool mandatory_args_set(struct perf_app_config *config)
{
	bool status = true;

	if (config->dev_ip.s_addr == 0) {
		DOCA_LOG_ERR("Local interface IP is not set");
		status = false;
	}
	if (config->dst_ip.s_addr == 0) {
		DOCA_LOG_ERR("Destination multicast IP is not set");
		status = false;
	}
	if (config->src_ip.s_addr == 0) {
		DOCA_LOG_ERR("Source IP is not set");
		status = false;
	}
	if (config->dst_port == 0) {
		DOCA_LOG_ERR("Destination port is not set");
		status = false;
	}
	return status;
}

void list_devices(void)
{
	struct doca_devinfo **devinfo;
	uint32_t nb_devs;
	doca_error_t ret;

	ret = doca_devinfo_list_create(&devinfo, &nb_devs);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to enumerate devices: %s", doca_get_error_string(ret));
		return;
	}
	printf("Iface\t\tIB dev\t\tBus ID\tIP addr\t\tPTP\n");
	for (uint32_t i = 0; i < nb_devs; ++i) {
		struct doca_pci_bdf dev_bus;
		char netdev[DOCA_DEVINFO_IFACE_NAME_SIZE];
		char ibdev[DOCA_DEVINFO_IBDEV_NAME_SIZE];
		uint8_t addr[4];
		bool has_ptp = false;

		/* get network interface name */
		ret = doca_devinfo_get_iface_name(devinfo[i], netdev, sizeof(netdev));
		if (ret != DOCA_SUCCESS) {
			DOCA_LOG_WARN("Failed to get interface name for device %d: %s", i, doca_get_error_string(ret));
			continue;
		}
		/* get Infiniband device name */
		ret = doca_devinfo_get_ibdev_name(devinfo[i], ibdev, sizeof(ibdev));
		if (ret != DOCA_SUCCESS) {
			DOCA_LOG_WARN("Failed to get Infiniband name for device %d: %s", i, doca_get_error_string(ret));
			continue;
		}
		/* get PCI address */
		ret = doca_devinfo_get_pci_addr(devinfo[i], &dev_bus);
		if (ret != DOCA_SUCCESS) {
			DOCA_LOG_WARN("Failed to get PCI address for device %d: %s", i, doca_get_error_string(ret));
			continue;
		}
		/* get IP address */
		ret = doca_devinfo_get_ipv4_addr(devinfo[i], (uint8_t *)&addr,
				sizeof(addr));
		if (ret == DOCA_SUCCESS) {
			/* query PTP capability */
			ret = doca_rmax_get_ptp_clock_supported(devinfo[i]);
			switch (ret) {
			case DOCA_SUCCESS:
				has_ptp = true;
				break;
			case DOCA_ERROR_NOT_SUPPORTED:
				has_ptp = false;
				break;
			default: {
				DOCA_LOG_WARN("Failed to query PTP capability for device %d: %s", i, doca_get_error_string(ret));
				continue;
			}
			}
		} else {
			memset(&addr, 0, sizeof(addr));
			if (ret != DOCA_ERROR_NOT_FOUND)
				DOCA_LOG_WARN("Failed to query IP address for device %d: %s", i, doca_get_error_string(ret));
		}

		printf("%-8s\t%-8s\t%x:%x.%x\t%03d.%03d.%03d.%03d\t%c\n",
				netdev, ibdev,
				dev_bus.bus, dev_bus.device, dev_bus.function,
				addr[0], addr[1], addr[2], addr[3],
				(has_ptp) ? 'y' : 'n');
	}
	ret = doca_devinfo_list_destroy(devinfo);
	if (ret != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to clean up devices list: %s", doca_get_error_string(ret));
}

struct doca_dev *open_device(struct in_addr *dev_ip)
{
	struct doca_devinfo **devinfo;
	struct doca_devinfo *found_devinfo = NULL;
	uint32_t nb_devs;
	doca_error_t ret;
	struct in_addr addr;
	struct doca_dev *dev = NULL;

	ret = doca_devinfo_list_create(&devinfo, &nb_devs);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to enumerate devices: %s", doca_get_error_string(ret));
		return NULL;
	}
	for (uint32_t i = 0; i < nb_devs; ++i) {
		ret = doca_devinfo_get_ipv4_addr(devinfo[i], (uint8_t *)&addr,
				sizeof(addr));
		if (ret != DOCA_SUCCESS)
			continue;
		if (addr.s_addr != dev_ip->s_addr)
			continue;
		found_devinfo = devinfo[i];
		break;
	}
	if (found_devinfo) {
		ret = doca_dev_open(found_devinfo, &dev);
		if (ret != DOCA_SUCCESS)
			DOCA_LOG_WARN("Error opening network device: %s", doca_get_error_string(ret));
	} else
		DOCA_LOG_ERR("Device not found");

	ret = doca_devinfo_list_destroy(devinfo);
	if (ret != DOCA_SUCCESS)
		DOCA_LOG_WARN("Failed to clean up devices list: %s", doca_get_error_string(ret));

	return dev;
}

doca_error_t init_globals(struct perf_app_config *config, struct doca_dev *dev, struct globals *globals)
{
	doca_error_t ret;
	size_t num_buffers = (config->hdr_size > 0) ? 2 : 1;

	/* create memory-related DOCA objects */
	ret = doca_mmap_create(NULL, &globals->mmap);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Error creating mmap: %s", doca_get_error_string(ret));
		return ret;
	}
	ret = doca_mmap_set_max_num_chunks(globals->mmap, num_buffers);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Error setting number of chunks: %s", doca_get_error_string(ret));
		return ret;
	}
	ret = doca_mmap_start(globals->mmap);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Error starting mmap: %s", doca_get_error_string(ret));
		return ret;
	}
	ret = doca_mmap_dev_add(globals->mmap, dev);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Error adding device to mmap: %s", doca_get_error_string(ret));
		return ret;
	}
	/* linked list extension must be enabled to chain buffers */
	ret = doca_buf_inventory_create(NULL, num_buffers,
			DOCA_BUF_EXTENSION_LINKED_LIST, &globals->inventory);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Error creating inventory: %s", doca_get_error_string(ret));
		return ret;
	}
	ret = doca_buf_inventory_start(globals->inventory);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Error starting inventory: %s", doca_get_error_string(ret));
		return ret;
	}

	ret = doca_workq_create(1, &globals->wq);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Error destroying workq: %s", doca_get_error_string(ret));
		return ret;
	}

	return DOCA_SUCCESS;
}

bool destroy_globals(struct globals *globals, struct doca_dev *dev)
{
	doca_error_t ret;
	bool is_ok = true;

	ret = doca_workq_destroy(globals->wq);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error destroying workq: %s", doca_get_error_string(ret));
		is_ok = false;
	}
	ret = doca_buf_inventory_stop(globals->inventory);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error stopping inventory: %s", doca_get_error_string(ret));
		is_ok = false;
	}
	ret = doca_buf_inventory_destroy(globals->inventory);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error destroying inventory: %s", doca_get_error_string(ret));
		is_ok = false;
	}
	ret = doca_mmap_dev_rm(globals->mmap, dev);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error disconnecting device from mmap: %s", doca_get_error_string(ret));
		is_ok = false;
	}
	ret = doca_mmap_stop(globals->mmap);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error stopping mmap: %s", doca_get_error_string(ret));
		is_ok = false;
	}
	/* will also free all allocated memory via callback */
	ret = doca_mmap_destroy(globals->mmap);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error destroying mmap: %s", doca_get_error_string(ret));
		is_ok = false;
	}

	return is_ok;
}

void free_callback(void *addr, size_t len, void *opaque)
{
	(void)len;
	(void)opaque;
	free(addr);
}

doca_error_t init_stream(struct perf_app_config *config, struct doca_dev *dev,
		struct globals *globals, struct stream_data *data)
{
	static const size_t page_size = 4096;
	doca_error_t ret;
	size_t num_buffers;
	size_t size[MAX_BUFFERS];
	void *ptr[MAX_BUFFERS];

	/* create stream object */
	ret = doca_rmax_in_stream_create(&data->stream);
	if (ret != DOCA_SUCCESS)
		return ret;

	/* fill stream parameters */
	ret = doca_rmax_in_stream_set_type(data->stream, config->type);
	if (ret != DOCA_SUCCESS)
		return ret;
	ret = doca_rmax_in_stream_set_scatter_type(data->stream, config->scatter_type);
	if (ret != DOCA_SUCCESS)
		return ret;
	ret = doca_rmax_in_stream_set_timestamp_format(data->stream, config->tstamp_format);
	if (ret != DOCA_SUCCESS)
		return ret;
	ret = doca_rmax_in_stream_set_elements_count(data->stream, config->num_elements);
	if (ret != DOCA_SUCCESS)
		return ret;
	ret = doca_rmax_in_stream_set_min_packets(data->stream, config->min_packets);
	if (ret != DOCA_SUCCESS)
		return ret;
	if (config->max_packets > 0) {
		ret = doca_rmax_in_stream_set_max_packets(data->stream, config->max_packets);
		if (ret != DOCA_SUCCESS)
			return ret;
	}

	if (config->hdr_size == 0) {
		num_buffers = 1;
		data->pkt_size[0] = config->data_size;
	} else {
		/* Header-Data Split mode */
		num_buffers = 2;
		data->pkt_size[0] = config->hdr_size;
		data->pkt_size[1] = config->data_size;
	}

	data->num_buffers = num_buffers;
	ret = doca_rmax_in_stream_set_memblks_count(data->stream, num_buffers);
	if (ret != DOCA_SUCCESS)
		return ret;
	ret = doca_rmax_in_stream_memblk_desc_set_min_size(data->stream, data->pkt_size);
	if (ret != DOCA_SUCCESS)
		return ret;
	ret = doca_rmax_in_stream_memblk_desc_set_max_size(data->stream, data->pkt_size);
	if (ret != DOCA_SUCCESS)
		return ret;

	/* connect stream to device */
	ret = doca_ctx_dev_add(doca_rmax_in_stream_as_ctx(data->stream), dev);
	if (ret != DOCA_SUCCESS)
		return ret;

	/* query buffer size */
	ret = doca_rmax_in_stream_get_memblk_size(data->stream, size);
	/* query stride size */
	ret = doca_rmax_in_stream_get_memblk_stride_size(data->stream, data->stride_size);

	/* allocate memory */
	if (num_buffers == 1) {
		ptr[0] = aligned_alloc(page_size, size[0]);
	} else {
		ptr[0] = aligned_alloc(page_size, size[0]); /*< header */
		ptr[1] = aligned_alloc(page_size, size[1]); /*< data */
	}
	/* build memory buffer chain */
	for (size_t i = 0; i < num_buffers; ++i) {
		struct doca_buf *buf;

		if (ptr[i] == NULL)
			return DOCA_ERROR_NO_MEMORY;
		ret = doca_mmap_populate(globals->mmap, ptr[i], size[i],
				page_size, free_callback, NULL);
		if (ret != DOCA_SUCCESS)
			return ret;
		ret = doca_buf_inventory_buf_by_addr(globals->inventory,
				globals->mmap, ptr[i], size[i], &buf);
		if (ret != DOCA_SUCCESS)
			return ret;
		if (i == 0)
			data->buffer = buf;
		else {
			/* chain buffers */
			ret = doca_buf_list_chain(data->buffer, buf);
			if (ret != DOCA_SUCCESS)
				return ret;
		}
	}
	/* set memory buffer(s) */
	ret = doca_rmax_in_stream_set_memblk(data->stream, data->buffer);
	if (ret != DOCA_SUCCESS)
		return ret;

	/* start stream */
	ret = doca_ctx_start(doca_rmax_in_stream_as_ctx(data->stream));
	if (ret != DOCA_SUCCESS)
		return ret;
	/* attach to workq */
	ret = doca_ctx_workq_add(doca_rmax_in_stream_as_ctx(data->stream), globals->wq);
	if (ret != DOCA_SUCCESS)
		return ret;

	/* attach a flow */
	ret = doca_rmax_flow_create(&data->flow);
	if (ret != DOCA_SUCCESS)
		return ret;
	ret = doca_rmax_flow_set_src_ip(data->flow, &config->src_ip);
	if (ret != DOCA_SUCCESS)
		return ret;
	ret = doca_rmax_flow_set_dst_ip(data->flow, &config->dst_ip);
	if (ret != DOCA_SUCCESS)
		return ret;
	ret = doca_rmax_flow_set_dst_port(data->flow, config->dst_port);
	if (ret != DOCA_SUCCESS)
		return ret;
	ret = doca_rmax_flow_attach(data->flow, data->stream);
	if (ret != DOCA_SUCCESS)
		return ret;

	data->recv_pkts = 0;
	data->recv_bytes = 0;

	return DOCA_SUCCESS;
}

bool destroy_stream(struct doca_dev *dev, struct globals *globals, struct stream_data *data)
{
	doca_error_t ret;
	bool is_ok = true;

	/* detach flow */
	ret = doca_rmax_flow_detach(data->flow, data->stream);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error detaching flow: %s", doca_get_error_string(ret));
		is_ok = false;
	}
	ret = doca_rmax_flow_destroy(data->flow);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error destroying flow: %s", doca_get_error_string(ret));
		is_ok = false;
	}

	/* detach from workq */
	ret = doca_ctx_workq_rm(doca_rmax_in_stream_as_ctx(data->stream), globals->wq);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error detaching from workq: %s", doca_get_error_string(ret));
		is_ok = false;
	}
	/* stop stream */
	ret = doca_ctx_stop(doca_rmax_in_stream_as_ctx(data->stream));
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error stopping context: %s", doca_get_error_string(ret));
		is_ok = false;
	}
	/* will destroy all the buffers in the chain */
	ret = doca_buf_refcount_rm(data->buffer, NULL);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error removing buffers: %s", doca_get_error_string(ret));
		is_ok = false;
	}
	/* disconnect stream from device */
	ret = doca_ctx_dev_rm(doca_rmax_in_stream_as_ctx(data->stream), dev);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error detaching from device: %s", doca_get_error_string(ret));
		is_ok = false;
	}
	/* destroy stream */
	ret = doca_rmax_in_stream_destroy(data->stream);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Error destroying stream: %s", doca_get_error_string(ret));
		is_ok = false;
	}
	return is_ok;
}

////////////////////////////////////////////////////////////////////////////
char *
samples_hex_dump(const void *data, size_t size)
{
	/*
	 * <offset>:     <Hex bytes: 1-8>        <Hex bytes: 9-16>         <Ascii>
	 * 00000000: 31 32 33 34 35 36 37 38  39 30 61 62 63 64 65 66  1234567890abcdef
	 *    8     2         8 * 3          1          8 * 3         1       16       1
	 */
	const size_t line_size = 8 + 2 + 8 * 3 + 1 + 8 * 3 + 1 + 16 + 1;
	int i, j, r, read_index;
	size_t num_lines, buffer_size;
	char *buffer, *write_head;
	unsigned char cur_char, printable;
	char ascii_line[17];
	const unsigned char *input_buffer;

	/* Allocate a dynamic buffer to hold the full result */
	num_lines = (size + 16 - 1) / 16;
	buffer_size = num_lines * line_size + 1;
	buffer = (char *)malloc(buffer_size);
	if (buffer == NULL)
		return NULL;
	write_head = buffer;
	input_buffer = data;
	read_index = 0;

	for (i = 0; i < num_lines; i++)	{
		/* Offset */
		snprintf(write_head, buffer_size, "%08X: ", i * 16);
		write_head += 8 + 2;
		buffer_size -= 8 + 2;
		/* Hex print - 2 chunks of 8 bytes */
		for (r = 0; r < 2 ; r++) {
			for (j = 0; j < 8; j++) {
				/* If there is content to print */
				if (read_index < size) {
					cur_char = input_buffer[read_index++];
					snprintf(write_head, buffer_size, "%02X ", cur_char);
					/* Printable chars go "as-is" */
					if (' ' <= cur_char && cur_char <= '~')
						printable = cur_char;
					/* Otherwise, use a '.' */
					else
						printable = '.';
				/* Else, just use spaces */
				} else {
					snprintf(write_head, buffer_size, "   ");
					printable = ' ';
				}
				ascii_line[r * 8 + j] = printable;
				write_head += 3;
				buffer_size -= 3;
			}
			/* Spacer between the 2 hex groups */
			snprintf(write_head, buffer_size, " ");
			write_head += 1;
			buffer_size -= 1;
		}
		/* Ascii print */
		ascii_line[16] = '\0';
		snprintf(write_head, buffer_size, "%s\n", ascii_line);
		write_head += 16 + 1;
		buffer_size -= 16 + 1;
	}
	/* No need for the last '\n' */
	write_head[-1] = '\0';
	return buffer;
}
////////////////////////////////////////////////////////////////////////////

void handle_completion(const struct doca_rmax_in_stream_completion *comp, struct stream_data *data, bool dump)
{
	if (!comp)
		return;
	if (comp->elements_count <= 0)
		return;

	data->recv_pkts += comp->elements_count;
	for (size_t i = 0; i < data->num_buffers; ++i)
		data->recv_bytes += comp->elements_count * data->pkt_size[i];

	if (!dump)
		return;
	for (size_t i = 0; i < comp->elements_count; ++i)
		for (size_t chunk = 0; chunk < data->num_buffers; ++chunk) {
			const uint8_t *ptr = comp->memblk_ptr_arr[chunk] + data->stride_size[chunk] * i;
			char *dump_str = samples_hex_dump(ptr, data->pkt_size[chunk]);

			DOCA_LOG_INFO("pkt %zu chunk %zu\n%s", i, chunk, dump_str);
			free(dump_str);
		}
}

bool print_statistics(struct stream_data *data)
{
	static const uint64_t us_in_s = 1000000L;
	struct timespec now;
	int ret;
	uint64_t dt;
	double mbits_received;

	ret = clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	if (ret != 0) {
		DOCA_LOG_ERR("error getting time: %s", strerror(errno));
		return false;
	}

	dt = (now.tv_sec - data->start.tv_sec) * us_in_s;
	dt += now.tv_nsec / 1000 - data->start.tv_nsec / 1000;
	/* ignore intervals shorter than 1 second */
	if (dt < us_in_s)
		return true;

	printf("Got %7zu packets | ", data->recv_pkts);
	mbits_received = (double)(data->recv_bytes * 8) / dt;
	if (mbits_received > 1e3)
		printf("%7.2lf Gbps", mbits_received * 1e-3);
	else
		printf("%7.2lf Mbps", mbits_received);
	printf(" during %7.2lf sec\n", dt * 1e-6);

	/* clear stats */
	data->start.tv_sec = now.tv_sec;
	data->start.tv_nsec = now.tv_nsec;
	data->recv_pkts = 0;
	data->recv_bytes = 0;

	return true;
}

void handle_error(const struct doca_rmax_stream_error *err)
{
	if (err)
		DOCA_LOG_ERR("Error: code=%d message=%s", err->code, err->message);
	else
		DOCA_LOG_ERR("Unknown error");
}

bool run_recv_loop(const struct perf_app_config *config, struct globals *globals, struct stream_data *data)
{
	int ret;

	ret = clock_gettime(CLOCK_MONOTONIC_RAW, &data->start);
	if (ret != 0) {
		DOCA_LOG_ERR("error getting time: %s", strerror(errno));
		return false;
	}

	while (true) {
		struct doca_event ev;
		uint32_t flags = 0;
		doca_error_t ret;

		ret = doca_workq_progress_retrieve(globals->wq, &ev, flags);

		switch (ret) {
		case DOCA_SUCCESS:
			if ((enum doca_rmax_action_type)(ev.type) == DOCA_RMAX_ACTION_TYPE_RX_DATA)
				handle_completion((struct doca_rmax_in_stream_completion *)ev.result.ptr,
						data, config->dump);
			break;
		case DOCA_ERROR_AGAIN:
			/* no data available */
			break;
		case DOCA_ERROR_IO_FAILED:
			if ((enum doca_rmax_action_type)(ev.type) == DOCA_RMAX_ACTION_TYPE_RX_DATA) {
				handle_error((struct doca_rmax_stream_error *)ev.result.ptr);
				return false;
			}
			/* fallthrough */
		default:
			DOCA_LOG_ERR("Progress retrieval failed with error %s", doca_get_error_name(ret));
			/* exit from loop on error */
			return false;
		}

		if (!print_statistics(data))
			return false;
		if (config->sleep_us > 0) {
			if (usleep(config->sleep_us) != 0) {
				DOCA_LOG_ERR("usleep error: %s", strerror(errno));
				return false;
			}
		}
	}

	return true;
}

int main(int argc, char **argv)
{
	struct perf_app_config config;
	doca_error_t ret;
	int exit_code = EXIT_SUCCESS;

	init_config(&config);
	ret = doca_argp_init(APP_NAME, &config);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(ret));
		return EXIT_FAILURE;
	}
	if (!register_argp_params())
		return EXIT_FAILURE;
	ret = doca_argp_start(argc, argv);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application command line: %s", doca_get_error_string(ret));
		return EXIT_FAILURE;
	}

	if (config.list) {
		ret = doca_rmax_init();
		if (ret != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to initialize DOCA RMAX: %s", doca_get_error_string(ret));
			return EXIT_FAILURE;
		}

		list_devices();
	} else {
		struct globals globals;
		struct stream_data data;
		struct doca_dev *dev = NULL;
		struct doca_dev *clock_dev = NULL;

		if (!mandatory_args_set(&config)) {
			doca_argp_usage();
			DOCA_LOG_ERR("Not all mandatory arguments were set");
			return EXIT_FAILURE;
		}

		if (config.affinity_mask_set) {
			ret = doca_rmax_set_cpu_affinity_mask(&config.affinity_mask);
			if (ret != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Error setting CPU affinity mask: %s", doca_get_error_string(ret));
				return EXIT_FAILURE;
			}
		}
		ret = doca_rmax_init();
		if (ret != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to initialize DOCA RMAX: %s", doca_get_error_string(ret));
			return EXIT_FAILURE;
		}

		dev = open_device(&config.dev_ip);
		if (dev == NULL) {
			DOCA_LOG_ERR("Error opening device");
			exit_code = EXIT_FAILURE;
			goto cleanup;
		}
		if (config.clock_ip.s_addr != 0) {
			clock_dev = open_device(&config.clock_ip);
			if (clock_dev == NULL) {
				DOCA_LOG_ERR("Error opening PTP device");
				exit_code = EXIT_FAILURE;
				goto cleanup_device;
			}
			ret = doca_rmax_set_clock(clock_dev);
			if (ret != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Error setting PTP device: %s", doca_get_error_string(ret));
				exit_code = EXIT_FAILURE;
				goto cleanup_ptp_device;
			}
		}
		ret = init_globals(&config, dev, &globals);
		if (ret != DOCA_SUCCESS) {
			exit_code = EXIT_FAILURE;
			goto cleanup_ptp_device;
		}
		ret = init_stream(&config, dev, &globals, &data);
		if (ret != DOCA_SUCCESS) {
			exit_code = EXIT_FAILURE;
			goto cleanup_globals;
		}

		/* main loop */
		if (!run_recv_loop(&config, &globals, &data))
			exit_code = EXIT_FAILURE;

		if (!destroy_stream(dev, &globals, &data))
			exit_code = EXIT_FAILURE;
cleanup_globals:
		if (!destroy_globals(&globals, dev))
			exit_code = EXIT_FAILURE;
cleanup_ptp_device:
		if (clock_dev) {
			ret = doca_dev_close(clock_dev);
			if (ret != DOCA_SUCCESS) {
				exit_code = EXIT_FAILURE;
				DOCA_LOG_ERR("Error closing PTP device: %s", doca_get_error_string(ret));
			}
		}
cleanup_device:
		ret = doca_dev_close(dev);
		if (ret != DOCA_SUCCESS) {
			exit_code = EXIT_FAILURE;
			DOCA_LOG_ERR("Error closing device: %s", doca_get_error_string(ret));
		}
	}

cleanup:
	ret = doca_rmax_release();
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to release DOCA RMAX: %s", doca_get_error_string(ret));
		return EXIT_FAILURE;
	}
	doca_argp_destroy();

	return exit_code;
}
