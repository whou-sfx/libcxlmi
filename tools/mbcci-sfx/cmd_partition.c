// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Get/Set Partition Info (Opcodes 4100h / 4101h).
 *
 * Sends partition commands directly via the in-band ioctl path provided
 * by libcxlmi and pretty-prints the response or confirmation.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

/* Capacities are reported in 256 MiB multiples, per CXL spec. */
#define CAP_UNIT_MIB 256ULL

#define SET_PARTITION_USAGE \
	"set-partition --next-volatile <MiB> [--flags <n>] [--bp-dirty-shutdown]"

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

void print_set_partition_result(
	const struct cxlmi_cmd_memdev_set_partition_info_req *req)
{
	printf("Set partition OK\n");
	printf("  Next Volatile: %llu MiB (%llu blocks)\n",
	       (unsigned long long)(CAP_UNIT_MIB * req->volatile_capacity),
	       (unsigned long long)req->volatile_capacity);
	printf("  Flags:         0x%02x\n", req->flags);
}

int parse_set_partition_req(int argc, char **argv,
			    struct cxlmi_cmd_memdev_set_partition_info_req *req)
{
	int has_volatile = 0;
	int i;

	memset(req, 0, sizeof(*req));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--next-volatile") == 0 && i + 1 < argc) {
			unsigned long long mib = strtoull(argv[++i], NULL, 0);

			if (mib % CAP_UNIT_MIB != 0) {
				fprintf(stderr,
					"set-partition: --next-volatile must be a multiple of 256 MiB\n");
				return -1;
			}
			req->volatile_capacity = mib / CAP_UNIT_MIB;
			has_volatile = 1;
		} else if (strcmp(argv[i], "--flags") == 0 && i + 1 < argc) {
			unsigned long flags = strtoul(argv[++i], NULL, 0);

			if (flags > 255) {
				fprintf(stderr,
					"set-partition: --flags must be 0-255\n");
				return -1;
			}
			req->flags = (uint8_t)flags;
		} else if (strcmp(argv[i], "--bp-dirty-shutdown") == 0) {
			req->flags |= 0x01;
		} else {
			fprintf(stderr, "Usage: %s\n", SET_PARTITION_USAGE);
			return -1;
		}
	}

	if (!has_volatile) {
		fprintf(stderr, "Usage: %s\n", SET_PARTITION_USAGE);
		return -1;
	}

	return 0;
}

int cmd_set_partition(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_set_partition_info_req req;
	int rc;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s\n", SET_PARTITION_USAGE);
		return -1;
	}

	rc = parse_set_partition_req(argc - 1, argv + 1, &req);
	if (rc)
		return rc;

	rc = cxlmi_cmd_memdev_set_partition_info(ep, NULL, &req);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "set partition info failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "set partition info ioctl failed\n");
		return rc;
	}

	print_set_partition_result(&req);
	return 0;
}
