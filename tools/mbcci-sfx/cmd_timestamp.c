// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Timestamp commands (Opcodes 0300h / 0301h).
 *
 * get-timestamp: Read device timestamp and display raw nanoseconds plus
 *                decoded local time.
 * set-timestamp: Write timestamp to device; defaults to current host time
 *                from clock_gettime(CLOCK_REALTIME).  Use --ts <ns> to
 *                supply an explicit nanosecond value.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

static void print_timestamp(uint64_t ns)
{
	time_t sec = (time_t)(ns / 1000000000ULL);
	uint32_t frac_ns = (uint32_t)(ns % 1000000000ULL);
	struct tm *tm;
	char buf[64];

	printf("Timestamp (raw):    %llu ns\n", (unsigned long long)ns);

	tm = localtime(&sec);
	if (tm && strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm))
		printf("Timestamp (local):  %s.%09u\n", buf, frac_ns);
	else
		printf("Timestamp (local):  (decode failed)\n");
}

static uint64_t host_timestamp_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int cmd_get_timestamp(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_get_timestamp_rsp rsp = { 0 };
	int rc;

	(void)argc;
	(void)argv;

	rc = cxlmi_cmd_get_timestamp(ep, NULL, &rsp);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get-timestamp failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get-timestamp ioctl failed\n");
		return rc;
	}

	print_timestamp(rsp.timestamp);
	return 0;
}

int cmd_set_timestamp(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_set_timestamp_req req = { 0 };
	int rc, i;

	/* Default: use current host time. */
	req.timestamp = host_timestamp_ns();

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--ts") == 0 && i + 1 < argc) {
			req.timestamp = strtoull(argv[++i], NULL, 0);
		} else {
			fprintf(stderr,
				"Usage: set-timestamp [--ts <nanoseconds>]\n");
			return -1;
		}
	}

	rc = cxlmi_cmd_set_timestamp(ep, NULL, &req);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "set-timestamp failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "set-timestamp ioctl failed\n");
		return rc;
	}

	printf("Timestamp set to %llu ns\n", (unsigned long long)req.timestamp);
	return 0;
}
