// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: FM API commands (MLD Components / 54h).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define FM_GET_LD_ALLOC_DEFAULT_LIMIT 16

#define FM_GET_LD_ALLOC_USAGE \
	"fm-get-ld-alloc [--start-ld-id <n>] [--limit <n>]"

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
			      struct cxlmi_cmd_fmapi_get_ld_allocations_req *req)
{
	unsigned long val;
	int i;

	memset(req, 0, sizeof(*req));
	req->ld_allocation_list_limit = FM_GET_LD_ALLOC_DEFAULT_LIMIT;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--start-ld-id") == 0 && i + 1 < argc) {
			val = strtoul(argv[++i], NULL, 0);
			if (val > UINT8_MAX) {
				fprintf(stderr, "--start-ld-id: out of range (0-255)\n");
				return -1;
			}
			req->start_ld_id = (uint8_t)val;
		} else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
			val = strtoul(argv[++i], NULL, 0);
			if (val == 0 || val > UINT8_MAX) {
				fprintf(stderr, "--limit: out of range (1-255)\n");
				return -1;
			}
			req->ld_allocation_list_limit = (uint8_t)val;
		} else {
			fprintf(stderr, "Usage: %s\n", FM_GET_LD_ALLOC_USAGE);
			return -1;
		}
	}

	return 0;
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
