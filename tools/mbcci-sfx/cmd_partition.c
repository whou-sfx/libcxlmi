// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Get Partition Info (Opcode 4100h).
 *
 * Sends 4100h directly via the in-band ioctl path provided by libcxlmi
 * and pretty-prints the response payload.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

/* Capacities are reported in 256 MiB multiples, per CXL spec. */
#define CAP_UNIT_MIB 256ULL

void print_memdev_partition_info(
	const struct cxlmi_cmd_memdev_get_partition_info_rsp *pi)
{
	printf("Active Volatile:     %llu MiB\n",
	       (unsigned long long)(CAP_UNIT_MIB * pi->active_vmem));
	printf("Active Persistent:   %llu MiB\n",
	       (unsigned long long)(CAP_UNIT_MIB * pi->active_pmem));
	printf("Next Volatile:       %llu MiB\n",
	       (unsigned long long)(CAP_UNIT_MIB * pi->next_vmem));
	printf("Next Persistent:     %llu MiB\n",
	       (unsigned long long)(CAP_UNIT_MIB * pi->next_pmem));
}

int cmd_get_partition(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_get_partition_info_rsp pi;
	int rc;

	(void)argc;
	(void)argv;

	memset(&pi, 0, sizeof(pi));
	rc = cxlmi_cmd_memdev_get_partition_info(ep, NULL, &pi);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get partition info failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get partition info ioctl failed\n");
		return rc;
	}

	print_memdev_partition_info(&pi);
	return 0;
}
