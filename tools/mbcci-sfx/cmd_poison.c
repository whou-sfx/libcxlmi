// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Media and Poison commands (Opcodes 4300h-4302h).
 */
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

/* Match libcxlmi MAX_POISON_RECORDS: header + records fit in 2048B mailbox. */
#define POISON_LIST_RSP_HDR_SZ 0x20
#define POISON_MAX_RECORDS \
	((CXL_MAILBOX_MAX_PAYLOAD_SIZE - POISON_LIST_RSP_HDR_SZ) / \
	 sizeof(struct cxlmi_memdev_media_err_record))
#define POISON_MAX_ITERATIONS 64

#define POISON_REQ_RESTART_BIT (1ULL << 0)
#define POISON_RSP_FLAG_MORE     (1U << 0)
#define POISON_RSP_FLAG_OVERFLOW (1U << 1)
#define POISON_ERR_SOURCE_MASK   0x7ULL

#define GET_POISON_LIST_USAGE \
	"get-poison-list --dpa <addr> --length <bytes> [--frestart]"

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

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int parse_write_data(const char *arg, uint8_t data[64])
{
	size_t len = strlen(arg);
	size_t i;

	if (len != 2 * 64) {
		fprintf(stderr,
			"--write-data requires exactly 64 bytes (128 hex digits)\n");
		return -1;
	}

	for (i = 0; i < 64; i++) {
		int hi = hex_nibble(arg[2 * i]);
		int lo = hex_nibble(arg[2 * i + 1]);

		if (hi < 0 || lo < 0) {
			fprintf(stderr, "--write-data contains invalid hex\n");
			return -1;
		}
		data[i] = (uint8_t)((hi << 4) | lo);
	}

	return 0;
}

int parse_get_poison_list_req(
	int argc, char **argv,
	struct cxlmi_cmd_memdev_get_poison_list_req *req,
	int *frestart)
{
	int has_dpa = 0;
	int has_length = 0;
	int i;

	memset(req, 0, sizeof(*req));
	if (frestart)
		*frestart = 0;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--dpa") == 0 && i + 1 < argc) {
			if (parse_u64_arg(argv[++i],
					  &req->get_poison_list_phy_addr,
					  "--dpa"))
				return -1;
			has_dpa = 1;
		} else if (strcmp(argv[i], "--length") == 0 &&
			   i + 1 < argc) {
			if (parse_u64_arg(argv[++i],
					  &req->get_poison_list_phy_addr_len,
					  "--length"))
				return -1;
			has_length = 1;
		} else if (strcmp(argv[i], "--frestart") == 0) {
			if (frestart)
				*frestart = 1;
		} else {
			fprintf(stderr, "Usage: %s\n", GET_POISON_LIST_USAGE);
			return -1;
		}
	}

	if (!has_dpa || !has_length) {
		fprintf(stderr, "Usage: %s\n", GET_POISON_LIST_USAGE);
		return -1;
	}
	return 0;
}

int parse_inject_poison_req(
	int argc, char **argv,
	struct cxlmi_cmd_memdev_inject_poison_req *req)
{
	int has_dpa = 0;
	int i;

	memset(req, 0, sizeof(*req));
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--dpa") == 0 && i + 1 < argc) {
			if (parse_u64_arg(argv[++i], &req->inject_poison_phy_addr,
					  "--dpa"))
				return -1;
			has_dpa = 1;
		} else {
			fprintf(stderr, "Usage: inject-poison --dpa <addr>\n");
			return -1;
		}
	}

	if (!has_dpa) {
		fprintf(stderr, "Usage: inject-poison --dpa <addr>\n");
		return -1;
	}
	return 0;
}

int parse_clear_poison_req(
	int argc, char **argv,
	struct cxlmi_cmd_memdev_clear_poison_req *req)
{
	int has_dpa = 0;
	int i;

	memset(req, 0, sizeof(*req));
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--dpa") == 0 && i + 1 < argc) {
			if (parse_u64_arg(argv[++i], &req->clear_poison_phy_addr,
					  "--dpa"))
				return -1;
			has_dpa = 1;
		} else if (strcmp(argv[i], "--write-data") == 0 &&
			   i + 1 < argc) {
			if (parse_write_data(argv[++i],
					     req->clear_poison_write_data))
				return -1;
		} else {
			fprintf(stderr,
				"Usage: clear-poison --dpa <addr> [--write-data <128-hex-digits>]\n");
			return -1;
		}
	}

	if (!has_dpa) {
		fprintf(stderr,
			"Usage: clear-poison --dpa <addr> [--write-data <128-hex-digits>]\n");
		return -1;
	}
	return 0;
}

static const char *poison_error_source_name(uint8_t source)
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

void print_poison_list(
	const struct cxlmi_cmd_memdev_get_poison_list_rsp *rsp)
{
	uint16_t i;

	printf("Poison List Flags:       0x%02x", rsp->poison_list_flags);
	if (rsp->poison_list_flags & POISON_RSP_FLAG_MORE)
		printf(" MORE");
	if (rsp->poison_list_flags & POISON_RSP_FLAG_OVERFLOW)
		printf(" OVERFLOW");
	printf("\n");
	printf("Overflow Timestamp:      0x%016llx\n",
	       (unsigned long long)rsp->overflow_timestamp);
	printf("Media Error Record Count:%u\n",
	       rsp->more_err_media_record_cnt);
	for (i = 0; i < rsp->more_err_media_record_cnt; i++) {
		uint64_t encoded_addr = rsp->records[i].media_err_addr;
		uint8_t source = encoded_addr & POISON_ERR_SOURCE_MASK;
		uint64_t dpa = encoded_addr & ~POISON_ERR_SOURCE_MASK;

		printf("  [%u] DPA=0x%016llx ErrorSource=%s (0x%x) "
		       "Length=0x%08x\n", i,
		       (unsigned long long)dpa,
		       poison_error_source_name(source), source,
		       rsp->records[i].media_err_len);
	}
}

int cmd_get_poison_list(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_get_poison_list_req req;
	struct cxlmi_cmd_memdev_get_poison_list_rsp *rsp;
	uint64_t base_dpa;
	uint64_t length;
	size_t rsp_sz;
	int frestart = 0;
	int iter;
	int rc;

	rc = parse_get_poison_list_req(argc - 1, argv + 1, &req, &frestart);
	if (rc)
		return rc;

	base_dpa = req.get_poison_list_phy_addr & ~POISON_REQ_RESTART_BIT;
	length = req.get_poison_list_phy_addr_len;

	rsp_sz = sizeof(*rsp) +
		 POISON_MAX_RECORDS * sizeof(struct cxlmi_memdev_media_err_record);
	rsp = calloc(1, rsp_sz);
	if (!rsp) {
		perror("get-poison-list: calloc");
		return -1;
	}

	for (iter = 0; iter < POISON_MAX_ITERATIONS; iter++) {
		memset(rsp, 0, rsp_sz);
		req.get_poison_list_phy_addr = base_dpa;
		if (iter == 0 && frestart)
			req.get_poison_list_phy_addr |= POISON_REQ_RESTART_BIT;
		req.get_poison_list_phy_addr_len = length;

		if (iter > 0)
			printf("--- poison list continuation %d ---\n", iter);

		rc = cxlmi_cmd_get_poison_list(ep, NULL, &req, rsp);
		if (rc) {
			if (rc > 0)
				fprintf(stderr, "get poison list failed: %s\n",
					cxlmi_cmd_retcode_tostr(rc));
			else
				fprintf(stderr, "get poison list ioctl failed\n");
			free(rsp);
			return rc;
		}

		print_poison_list(rsp);
		if (!(rsp->poison_list_flags & POISON_RSP_FLAG_MORE))
			break;
	}

	if (iter >= POISON_MAX_ITERATIONS &&
	    (rsp->poison_list_flags & POISON_RSP_FLAG_MORE)) {
		fprintf(stderr,
			"get-poison-list: reached max iterations (%d) with MORE still set\n",
			POISON_MAX_ITERATIONS);
		free(rsp);
		return -1;
	}

	free(rsp);
	return 0;
}

int cmd_inject_poison(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_inject_poison_req req;
	int rc;

	rc = parse_inject_poison_req(argc - 1, argv + 1, &req);
	if (rc)
		return rc;

	rc = cxlmi_cmd_memdev_inject_poison(ep, NULL, &req);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "inject poison failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "inject poison ioctl failed\n");
		return rc;
	}

	printf("Inject poison OK\n");
	return 0;
}

int cmd_clear_poison(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_clear_poison_req req;
	int rc;

	rc = parse_clear_poison_req(argc - 1, argv + 1, &req);
	if (rc)
		return rc;

	rc = cxlmi_cmd_memdev_clear_poison(ep, NULL, &req);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "clear poison failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "clear poison ioctl failed\n");
		return rc;
	}

	printf("Clear poison OK\n");
	return 0;
}
