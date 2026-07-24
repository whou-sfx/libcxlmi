// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Scan Media commands (Opcodes 4303h-4305h).
 */
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define SCAN_MEDIA_RESULTS_RSP_HDR_SZ 0x20
#define SCAN_MEDIA_MAX_RECORDS \
	((CXL_MAILBOX_MAX_PAYLOAD_SIZE - SCAN_MEDIA_RESULTS_RSP_HDR_SZ) / \
	 sizeof(struct cxlmi_media_error_record))
#define SCAN_MEDIA_MAX_ITERATIONS 64

#define SCAN_MEDIA_FLAG_NO_EVENT_LOG (1U << 0)

#define SCAN_RESULTS_FLAG_MORE    (1U << 0)
#define SCAN_RESULTS_FLAG_STOPPED (1U << 1)

#define SCAN_ERR_SOURCE_MASK 0x7ULL

#define GET_SCAN_MEDIA_CAP_USAGE \
	"get-scan-media-cap --dpa <addr> --length <bytes>"
#define SCAN_MEDIA_USAGE \
	"scan-media --dpa <addr> --length <bytes> [--no-evtlog]"

static int parse_u64_arg(const char *arg, uint64_t *value, const char *name)
{
	char *end;
	unsigned long long parsed;

	errno = 0;
	parsed = strtoull(arg, &end, 0);
	if (errno || end == arg || *end != '\0') {
		fprintf(stderr, "%s: invalid value '%s'\n", name, arg);
		return -1;
	}

	*value = (uint64_t)parsed;
	return 0;
}

static const char *scan_error_source_name(uint8_t source)
{
	switch (source) {
	case 0:
		return "Unknown";
	case 1:
		return "External";
	case 2:
		return "Internal";
	case 3:
		return "Injected";
	case 7:
		return "Vendor Specific";
	default:
		return "Reserved";
	}
}

int parse_get_scan_media_cap_req(
	int argc, char **argv,
	struct cxlmi_cmd_memdev_get_scan_media_capabilities_req *req)
{
	int has_dpa = 0;
	int has_length = 0;
	int i;

	memset(req, 0, sizeof(*req));
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--dpa") == 0 && i + 1 < argc) {
			if (parse_u64_arg(argv[++i],
					  &req->get_scan_media_capabilities_start_physaddr,
					  "--dpa"))
				return -1;
			has_dpa = 1;
		} else if (strcmp(argv[i], "--length") == 0 &&
			   i + 1 < argc) {
			if (parse_u64_arg(argv[++i],
					  &req->get_scan_media_capabilities_physaddr_length,
					  "--length"))
				return -1;
			has_length = 1;
		} else {
			fprintf(stderr, "Usage: %s\n", GET_SCAN_MEDIA_CAP_USAGE);
			return -1;
		}
	}

	if (!has_dpa || !has_length) {
		fprintf(stderr, "Usage: %s\n", GET_SCAN_MEDIA_CAP_USAGE);
		return -1;
	}
	return 0;
}

int parse_scan_media_req(int argc, char **argv,
			 struct cxlmi_cmd_memdev_scan_media_req *req)
{
	int has_dpa = 0;
	int has_length = 0;
	int i;

	memset(req, 0, sizeof(*req));
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--dpa") == 0 && i + 1 < argc) {
			if (parse_u64_arg(argv[++i], &req->scan_media_physaddr,
					  "--dpa"))
				return -1;
			has_dpa = 1;
		} else if (strcmp(argv[i], "--length") == 0 &&
			   i + 1 < argc) {
			if (parse_u64_arg(argv[++i],
					  &req->scan_media_physaddr_length,
					  "--length"))
				return -1;
			has_length = 1;
		} else if (strcmp(argv[i], "--no-evtlog") == 0) {
			req->scan_media_flags |= SCAN_MEDIA_FLAG_NO_EVENT_LOG;
		} else {
			fprintf(stderr, "Usage: %s\n", SCAN_MEDIA_USAGE);
			return -1;
		}
	}

	if (!has_dpa || !has_length) {
		fprintf(stderr, "Usage: %s\n", SCAN_MEDIA_USAGE);
		return -1;
	}
	return 0;
}

void print_scan_media_capabilities(
	const struct cxlmi_cmd_memdev_get_scan_media_capabilities_rsp *rsp)
{
	printf("Estimated Scan Media Time: %u microseconds\n",
	       rsp->estimated_scan_media_time);
}

void print_scan_media_results(
	const struct cxlmi_cmd_memdev_get_scan_media_results_rsp *rsp)
{
	uint16_t i;

	printf("Restart PhysAddr:        0x%016llx\n",
	       (unsigned long long)rsp->scan_media_restart_physaddr);
	printf("Restart PhysAddr Length: 0x%016llx\n",
	       (unsigned long long)rsp->scan_media_restart_physaddr_length);
	printf("Scan Media Flags:        0x%02x", rsp->scan_media_flags);
	if (rsp->scan_media_flags & SCAN_RESULTS_FLAG_MORE)
		printf(" MORE");
	if (rsp->scan_media_flags & SCAN_RESULTS_FLAG_STOPPED)
		printf(" STOPPED");
	printf("\n");
	if (rsp->scan_media_flags & SCAN_RESULTS_FLAG_STOPPED) {
		printf("  Scan stopped prematurely; restart at DPA=0x%016llx "
		       "Length=0x%016llx\n",
		       (unsigned long long)rsp->scan_media_restart_physaddr,
		       (unsigned long long)rsp->scan_media_restart_physaddr_length);
	}
	printf("Media Error Count:       %u\n", rsp->media_error_count);
	for (i = 0; i < rsp->media_error_count; i++) {
		uint64_t encoded = rsp->record[i].media_error_address;
		uint8_t source = encoded & SCAN_ERR_SOURCE_MASK;
		uint64_t dpa = encoded & ~SCAN_ERR_SOURCE_MASK;

		printf("  [%u] DPA=0x%016llx ErrorSource=%s (0x%x) "
		       "Length=0x%08x\n", i,
		       (unsigned long long)dpa,
		       scan_error_source_name(source), source,
		       rsp->record[i].media_error_length);
	}
}

int cmd_get_scan_media_cap(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_get_scan_media_capabilities_req req;
	struct cxlmi_cmd_memdev_get_scan_media_capabilities_rsp rsp;
	int rc;

	rc = parse_get_scan_media_cap_req(argc - 1, argv + 1, &req);
	if (rc)
		return rc;

	memset(&rsp, 0, sizeof(rsp));
	rc = cxlmi_cmd_memdev_get_scan_media_capabilities(ep, NULL, &req, &rsp);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get scan media capabilities failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"get scan media capabilities ioctl failed\n");
		return rc;
	}

	print_scan_media_capabilities(&rsp);
	return 0;
}

int cmd_scan_media(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_scan_media_req req;
	int rc;

	rc = parse_scan_media_req(argc - 1, argv + 1, &req);
	if (rc)
		return rc;

	rc = cxlmi_cmd_memdev_scan_media(ep, NULL, &req);
	if (rc && rc != CXLMI_RET_BACKGROUND) {
		if (rc > 0)
			fprintf(stderr, "scan media failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "scan media ioctl failed\n");
		return rc;
	}

	if (rc == CXLMI_RET_BACKGROUND)
		printf("Scan media started as background operation\n");
	else
		printf("Scan media OK\n");
	printf("  DPA:    0x%016llx\n",
	       (unsigned long long)req.scan_media_physaddr);
	printf("  Length: 0x%016llx\n",
	       (unsigned long long)req.scan_media_physaddr_length);
	printf("  Flags:  0x%02x%s\n", req.scan_media_flags,
	       (req.scan_media_flags & SCAN_MEDIA_FLAG_NO_EVENT_LOG) ?
	       " NO_EVTLOG" : "");
	return 0;
}

int cmd_get_scan_media_results(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_get_scan_media_results_rsp *rsp;
	size_t rsp_sz;
	int iter;
	int rc;

	if (argc > 1) {
		fprintf(stderr, "Usage: get-scan-media-results\n");
		return -1;
	}

	rsp_sz = sizeof(*rsp) +
		 SCAN_MEDIA_MAX_RECORDS * sizeof(struct cxlmi_media_error_record);
	rsp = calloc(1, rsp_sz);
	if (!rsp) {
		perror("get-scan-media-results: calloc");
		return -1;
	}

	for (iter = 0; iter < SCAN_MEDIA_MAX_ITERATIONS; iter++) {
		memset(rsp, 0, rsp_sz);

		if (iter > 0)
			printf("--- scan media results continuation %d ---\n",
			       iter);

		rc = cxlmi_cmd_memdev_get_scan_media_results(ep, NULL, rsp);
		if (rc) {
			if (rc > 0)
				fprintf(stderr,
					"get scan media results failed: %s\n",
					cxlmi_cmd_retcode_tostr(rc));
			else
				fprintf(stderr,
					"get scan media results ioctl failed\n");
			free(rsp);
			return rc;
		}

		print_scan_media_results(rsp);
		if (!(rsp->scan_media_flags & SCAN_RESULTS_FLAG_MORE))
			break;
	}

	if (iter >= SCAN_MEDIA_MAX_ITERATIONS &&
	    (rsp->scan_media_flags & SCAN_RESULTS_FLAG_MORE)) {
		fprintf(stderr,
			"get-scan-media-results: reached max iterations (%d) with MORE still set\n",
			SCAN_MEDIA_MAX_ITERATIONS);
		free(rsp);
		return -1;
	}

	free(rsp);
	return 0;
}
