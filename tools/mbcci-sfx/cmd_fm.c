// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: FM API commands (MLD Components / 54h).
 */
#include <stdio.h>
#include <stdint.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define QOS_TELEM_EGRESS_PORT_CONG_SUPPORTED (1U << 0)
#define QOS_TELEM_TPR_SUPPORTED (1U << 1)

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
	printf("Memory Size: %llu bytes\n", (unsigned long long)rsp->memory_size);
	printf("LD Count:    %u\n", rsp->ld_count);
	print_qos_telemetry_capability(rsp->qos_telemetry_capability);
}
