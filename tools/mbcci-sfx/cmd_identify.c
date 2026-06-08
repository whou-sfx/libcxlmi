// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Memory Device Identify (Opcode 4000h).
 *
 * Sends 4000h directly via the in-band ioctl path provided by libcxlmi
 * (CXL_MEM_SEND_COMMAND with CXL_MEM_COMMAND_ID_RAW under the hood) and
 * pretty-prints the response payload.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

/* Capacities are reported in 256MB multiples, per CXL spec. */
#define CAP_UNIT_MIB 256ULL

static uint32_t poison_max_mer(const uint8_t v[3])
{
	return (uint32_t)v[0] | ((uint32_t)v[1] << 8) | ((uint32_t)v[2] << 16);
}

void print_memdev_identify(const struct cxlmi_cmd_memdev_identify_rsp *id)
{
	char fwrev[sizeof(id->fw_revision) + 1];

	memcpy(fwrev, id->fw_revision, sizeof(id->fw_revision));
	fwrev[sizeof(id->fw_revision)] = '\0';

	printf("FW Revision:        %s\n", fwrev);
	printf("Total Capacity:     %llu MiB\n",
	       (unsigned long long)(CAP_UNIT_MIB * id->total_capacity));
	printf("  Volatile:         %llu MiB\n",
	       (unsigned long long)(CAP_UNIT_MIB * id->volatile_capacity));
	printf("  Persistent:       %llu MiB\n",
	       (unsigned long long)(CAP_UNIT_MIB * id->persistent_capacity));
	printf("Partition Align:    %llu MiB\n",
	       (unsigned long long)(CAP_UNIT_MIB * id->partition_align));
	printf("LSA Size:           %u bytes\n", id->lsa_size);
	printf("Info  Event Log:    %u\n", id->info_event_log_size);
	printf("Warn  Event Log:    %u\n", id->warning_event_log_size);
	printf("Fail  Event Log:    %u\n", id->failure_event_log_size);
	printf("Fatal Event Log:    %u\n", id->fatal_event_log_size);
	printf("Poison List Max:    %u\n", poison_max_mer(id->poison_list_max_mer));
	printf("Inject Poison Lim:  %u\n", id->inject_poison_limit);
	printf("Poison Caps:        0x%02x\n", id->poison_caps);
	printf("QoS Telemetry Caps: 0x%02x\n", id->qos_telemetry_caps);
#ifndef SUPPORT_CXL_2_0
	printf("DC Event Log Size:  %u\n", id->dc_event_log_size);
#endif
}

int cmd_identify_memdev(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_identify_rsp id;
	int rc;

	(void)argc;
	(void)argv;

	memset(&id, 0, sizeof(id));
	rc = cxlmi_cmd_memdev_identify(ep, NULL, &id);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "memdev identify failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "memdev identify ioctl failed\n");
		return rc;
	}

	print_memdev_identify(&id);
	return 0;
}
