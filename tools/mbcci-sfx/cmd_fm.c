// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: FM API commands (MLD Components / 54h).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include <ccan/endian/endian.h>

#include "mbcci-sfx.h"

#define FM_GET_LD_ALLOC_DEFAULT_LIMIT 16

#define FM_GET_LD_ALLOC_USAGE \
	"fm-get-ld-alloc [--start-ld-id <n>] [--limit <n>] [--raw-dump <file>]"

#define FM_SET_LD_ALLOC_USAGE \
	"fm-set-ld-alloc --input <hexfile> [--number-ld <n>] [--start-ld-id <n>]"

#define QOS_TELEM_EGRESS_PORT_CONG_SUPPORTED (1U << 0)
#define QOS_TELEM_TPR_SUPPORTED (1U << 1)

/* Capacities are reported in 256 MiB multiples, per CXL spec. */
#define CAP_UNIT_MIB 256ULL

void print_qos_telemetry_capability(uint8_t val)
{
	printf("QoS Telemetry Capability: 0x%02x\n", val);
	if (val & QOS_TELEM_EGRESS_PORT_CONG_SUPPORTED)
		printf("  [0] Egress Port Congestion Supported\n");
	if (val & QOS_TELEM_TPR_SUPPORTED)
		printf("  [1] Temporary Throughput Reduction Supported\n");
	if (val & 0xfc)
		printf("  [7:2] Reserved (0x%x)\n", (val >> 2) & 0x3f);
}

void print_fm_get_ld_info(const struct cxlmi_cmd_fmapi_get_ld_info_rsp *rsp)
{
	printf("Memory Size: %llu MiB\n",
	       (unsigned long long)(CAP_UNIT_MIB * rsp->memory_size));
	printf("LD Count:    %u\n", rsp->ld_count);
	print_qos_telemetry_capability(rsp->qos_telemetry_capability);
}

int parse_fm_get_ld_alloc_req(int argc, char **argv,
			      struct fm_get_ld_alloc_params *params)
{
	unsigned long val;
	int i;

	memset(params, 0, sizeof(*params));
	params->req.ld_allocation_list_limit = FM_GET_LD_ALLOC_DEFAULT_LIMIT;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--start-ld-id") == 0 && i + 1 < argc) {
			val = strtoul(argv[++i], NULL, 0);
			if (val > UINT8_MAX) {
				fprintf(stderr, "--start-ld-id: out of range (0-255)\n");
				return -1;
			}
			params->req.start_ld_id = (uint8_t)val;
		} else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
			val = strtoul(argv[++i], NULL, 0);
			if (val == 0 || val > UINT8_MAX) {
				fprintf(stderr, "--limit: out of range (1-255)\n");
				return -1;
			}
			params->req.ld_allocation_list_limit = (uint8_t)val;
		} else if (strcmp(argv[i], "--raw-dump") == 0 && i + 1 < argc) {
			params->raw_dump_file = argv[++i];
		} else {
			fprintf(stderr, "Usage: %s\n", FM_GET_LD_ALLOC_USAGE);
			return -1;
		}
	}

	return 0;
}

#define FM_GET_LD_ALLOC_HDR_SZ 4
#define FM_SET_LD_ALLOC_HDR_SZ MBCCI_FM_SET_LD_ALLOC_HDR_SZ
#define FM_LD_ALLOC_ENTRY_SZ sizeof(struct cxlmi_cmd_fmapi_ld_allocations_list)

size_t fm_set_ld_alloc_payload_size(uint8_t number_ld)
{
	return FM_SET_LD_ALLOC_HDR_SZ + number_ld * FM_LD_ALLOC_ENTRY_SZ;
}

size_t fm_get_ld_alloc_payload_size(uint8_t ld_allocation_list_len)
{
	return FM_GET_LD_ALLOC_HDR_SZ + ld_allocation_list_len * FM_LD_ALLOC_ENTRY_SZ;
}

int fm_ld_alloc_build_get_rsp_payload(
	const struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *rsp,
	uint8_t *buf, size_t buf_sz, size_t *out_len)
{
	size_t need;
	unsigned int i;

	if (!rsp->ld_allocation_list_len)
		return -1;

	need = fm_get_ld_alloc_payload_size(rsp->ld_allocation_list_len);
	if (buf_sz < need)
		return -1;

	buf[0] = rsp->number_ld;
	buf[1] = rsp->memory_granularity;
	buf[2] = rsp->start_ld_id;
	buf[3] = rsp->ld_allocation_list_len;

	for (i = 0; i < rsp->ld_allocation_list_len; i++) {
		uint64_t r1 = rsp->ld_allocation_list[i].range_1_allocation_mult;
		uint64_t r2 = rsp->ld_allocation_list[i].range_2_allocation_mult;
		size_t off = FM_GET_LD_ALLOC_HDR_SZ + i * FM_LD_ALLOC_ENTRY_SZ;
		uint64_t r1_le = cpu_to_le64(r1);
		uint64_t r2_le = cpu_to_le64(r2);

		memcpy(buf + off, &r1_le, sizeof(r1_le));
		memcpy(buf + off + 8, &r2_le, sizeof(r2_le));
	}

	*out_len = need;
	return 0;
}

int fm_ld_alloc_normalize_set_payload(uint8_t *payload, size_t payload_len,
				      const struct fm_set_ld_alloc_params *params,
				      uint8_t *number_ld_out)
{
	uint8_t set_buf[MBCCI_FM_SET_LD_ALLOC_HDR_SZ +
			255 * FM_LD_ALLOC_ENTRY_SZ];
	size_t list_count;
	uint8_t number_ld;

	if (payload_len < FM_GET_LD_ALLOC_HDR_SZ ||
	    (payload_len - FM_GET_LD_ALLOC_HDR_SZ) % FM_LD_ALLOC_ENTRY_SZ)
		return -1;

	list_count = (payload_len - FM_GET_LD_ALLOC_HDR_SZ) / FM_LD_ALLOC_ENTRY_SZ;

	if (payload[3] == list_count &&
	    payload_len == fm_get_ld_alloc_payload_size((uint8_t)list_count)) {
		/* Get LD Allocations response payload from --raw-dump */
		number_ld = params->has_number_ld ? params->number_ld :
			    payload[3];
		set_buf[0] = number_ld;
		set_buf[1] = params->has_start_ld_id ? params->start_ld_id :
			     payload[2];
		set_buf[2] = 0;
		set_buf[3] = 0;
		memcpy(set_buf + FM_SET_LD_ALLOC_HDR_SZ,
		       payload + FM_GET_LD_ALLOC_HDR_SZ,
		       list_count * FM_LD_ALLOC_ENTRY_SZ);
		memcpy(payload, set_buf, payload_len);
	} else if (payload[0] == list_count &&
		   payload_len == fm_set_ld_alloc_payload_size((uint8_t)list_count)) {
		number_ld = params->has_number_ld ? params->number_ld : payload[0];
		if (params->has_start_ld_id)
			payload[1] = params->start_ld_id;
		if (params->has_number_ld)
			payload[0] = params->number_ld;
	} else {
		return -1;
	}

	number_ld = payload[0];
	if (fm_set_ld_alloc_payload_size(number_ld) != payload_len)
		return -1;

	*number_ld_out = number_ld;
	return 0;
}

int parse_fm_set_ld_alloc_req(int argc, char **argv,
			      struct fm_set_ld_alloc_params *params)
{
	unsigned long val;
	int i;

	memset(params, 0, sizeof(*params));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
			params->input_file = argv[++i];
		} else if (strcmp(argv[i], "--number-ld") == 0 && i + 1 < argc) {
			val = strtoul(argv[++i], NULL, 0);
			if (val == 0 || val > UINT8_MAX) {
				fprintf(stderr, "--number-ld: out of range (1-255)\n");
				return -1;
			}
			params->number_ld = (uint8_t)val;
			params->has_number_ld = 1;
		} else if (strcmp(argv[i], "--start-ld-id") == 0 && i + 1 < argc) {
			val = strtoul(argv[++i], NULL, 0);
			if (val > UINT8_MAX) {
				fprintf(stderr, "--start-ld-id: out of range (0-255)\n");
				return -1;
			}
			params->start_ld_id = (uint8_t)val;
			params->has_start_ld_id = 1;
		} else {
			fprintf(stderr, "Usage: %s\n", FM_SET_LD_ALLOC_USAGE);
			return -1;
		}
	}

	if (!params->input_file) {
		fprintf(stderr, "Usage: %s\n", FM_SET_LD_ALLOC_USAGE);
		return -1;
	}

	return 0;
}

void print_fm_set_ld_alloc(const struct cxlmi_cmd_fmapi_set_ld_allocations_rsp *rsp)
{
	unsigned int i;

	printf("Number LD:   %u\n", rsp->number_ld);
	printf("Start LD ID: %u\n", rsp->start_ld_id);

	for (i = 0; i < rsp->number_ld; i++) {
		printf("  LD[%u] range_1_mult: %llu\n",
		       rsp->start_ld_id + i,
		       (unsigned long long)
		       rsp->ld_allocation_list[i].range_1_allocation_mult);
		printf("  LD[%u] range_2_mult: %llu\n",
		       rsp->start_ld_id + i,
		       (unsigned long long)
		       rsp->ld_allocation_list[i].range_2_allocation_mult);
	}
}

void print_fm_get_ld_alloc(const struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *rsp)
{
	unsigned int i;

	printf("Number LD:            %u\n", rsp->number_ld);
	printf("Memory Granularity:   %u\n", rsp->memory_granularity);
	printf("Start LD ID:          %u\n", rsp->start_ld_id);
	printf("Allocation List Len:  %u\n", rsp->ld_allocation_list_len);

	for (i = 0; i < rsp->ld_allocation_list_len; i++) {
		printf("  LD[%u] range_1_mult: %llu\n",
		       rsp->start_ld_id + i,
		       (unsigned long long)
		       rsp->ld_allocation_list[i].range_1_allocation_mult);
		printf("  LD[%u] range_2_mult: %llu\n",
		       rsp->start_ld_id + i,
		       (unsigned long long)
		       rsp->ld_allocation_list[i].range_2_allocation_mult);
	}
}
