// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Media Operations (Opcode 4402h) — discovery and sanitize.
 */
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define MEDIA_OPS_DISCOVERY_RSP_HDR_SZ 12
#define MEDIA_OPS_MAX_SUPPORTED \
	((CXL_MAILBOX_MAX_PAYLOAD_SIZE - MEDIA_OPS_DISCOVERY_RSP_HDR_SZ) / \
	 sizeof(struct cxlmi_cmd_memdev_media_ops_supported_list_entry))

#define MEDIA_OPS_SANITIZE_REQ_HDR_SZ 8
#define MEDIA_OPS_MAX_DPA_RANGES \
	((CXL_MAILBOX_MAX_PAYLOAD_SIZE - MEDIA_OPS_SANITIZE_REQ_HDR_SZ) / \
	 sizeof(struct cxlmi_cmd_memdev_media_ops_dpa_range_list_entry))

/* CXL Table: Media Operation Class / Subclass */
#define MEDIA_OP_CLASS_GENERAL  0x00
#define MEDIA_OP_CLASS_SANITIZE 0x01
#define MEDIA_OP_SUBCLASS_DISCOVERY 0x00
#define MEDIA_OP_SUBCLASS_SANITIZE  0x00
#define MEDIA_OP_SUBCLASS_ZERO      0x01

#define MEDIA_OP_DISCOVERY_DEFAULT_NUM_OPS 16

#define MEDIA_OP_USAGE \
	"media-operation discovery|sanitize [args...]"
#define MEDIA_OP_DISCOVERY_USAGE \
	"media-operation discovery [--start-index <n>] [--num-ops <n>]"
#define MEDIA_OP_SANITIZE_USAGE \
	"media-operation sanitize --operation <sanitize|zero> " \
	"--dpa-range <start>:<length> [--dpa-range ...]"

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

static int parse_u16_arg(const char *arg, uint16_t *value, const char *name)
{
	uint64_t tmp;

	if (parse_u64_arg(arg, &tmp, name))
		return -1;
	if (tmp > 0xffff) {
		fprintf(stderr, "%s: value out of range '%s'\n", name, arg);
		return -1;
	}
	*value = (uint16_t)tmp;
	return 0;
}

static const char *media_op_name(uint8_t class, uint8_t subclass)
{
	if (class == MEDIA_OP_CLASS_GENERAL &&
	    subclass == MEDIA_OP_SUBCLASS_DISCOVERY)
		return "Discovery";
	if (class == MEDIA_OP_CLASS_SANITIZE &&
	    subclass == MEDIA_OP_SUBCLASS_SANITIZE)
		return "Sanitize";
	if (class == MEDIA_OP_CLASS_SANITIZE &&
	    subclass == MEDIA_OP_SUBCLASS_ZERO)
		return "Write Zero";
	return "Unknown";
}

static void print_media_ops_discovery(
	const struct cxlmi_cmd_memdev_media_operations_discovery_rsp *rsp)
{
	uint16_t i;

	printf("DPA Range Granularity: 0x%016llx\n",
	       (unsigned long long)rsp->dpa_range_granularity);
	printf("Total Supported Ops:  %u\n", rsp->total_supported_ops);
	printf("Returned Ops:         %u\n", rsp->num_supported_ops);
	for (i = 0; i < rsp->num_supported_ops; i++) {
		uint8_t cls = rsp->entry[i].media_op_class;
		uint8_t sub = rsp->entry[i].media_op_subclass;

		printf("  [%u] Class=0x%02x Subclass=0x%02x (%s)\n", i,
		       cls, sub, media_op_name(cls, sub));
	}
}

static int parse_discovery_args(int argc, char **argv,
				struct cxlmi_cmd_memdev_media_operations_discovery_req *req)
{
	int i;

	memset(req, 0, sizeof(*req));
	req->media_operation_class = MEDIA_OP_CLASS_GENERAL;
	req->media_operation_subclass = MEDIA_OP_SUBCLASS_DISCOVERY;
	req->dpa_range_count = 0;
	req->discovery_osa.start_index = 0;
	req->discovery_osa.num_ops = MEDIA_OP_DISCOVERY_DEFAULT_NUM_OPS;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--start-index") == 0 && i + 1 < argc) {
			if (parse_u16_arg(argv[++i],
					  &req->discovery_osa.start_index,
					  "--start-index"))
				return -1;
		} else if (strcmp(argv[i], "--num-ops") == 0 && i + 1 < argc) {
			if (parse_u16_arg(argv[++i],
					  &req->discovery_osa.num_ops,
					  "--num-ops"))
				return -1;
		} else {
			fprintf(stderr, "Usage: %s\n", MEDIA_OP_DISCOVERY_USAGE);
			return -1;
		}
	}

	if (req->discovery_osa.num_ops == 0) {
		fprintf(stderr, "--num-ops must be greater than 0\n");
		return -1;
	}
	if (req->discovery_osa.num_ops > MEDIA_OPS_MAX_SUPPORTED) {
		fprintf(stderr,
			"--num-ops %u exceeds mailbox payload limit (%zu)\n",
			req->discovery_osa.num_ops, MEDIA_OPS_MAX_SUPPORTED);
		return -1;
	}
	return 0;
}

static int parse_dpa_range(const char *arg,
			   struct cxlmi_cmd_memdev_media_ops_dpa_range_list_entry *entry)
{
	char *copy;
	char *colon;
	char *end;
	unsigned long long start;
	unsigned long long length;

	copy = strdup(arg);
	if (!copy) {
		perror("parse_dpa_range: strdup");
		return -1;
	}

	colon = strchr(copy, ':');
	if (!colon || colon == copy || *(colon + 1) == '\0') {
		fprintf(stderr,
			"--dpa-range: expected <start>:<length>, got '%s'\n",
			arg);
		free(copy);
		return -1;
	}
	*colon = '\0';

	errno = 0;
	start = strtoull(copy, &end, 0);
	if (errno || end == copy || *end != '\0') {
		fprintf(stderr, "--dpa-range: invalid start '%s'\n", copy);
		free(copy);
		return -1;
	}

	errno = 0;
	length = strtoull(colon + 1, &end, 0);
	if (errno || end == (colon + 1) || *end != '\0') {
		fprintf(stderr, "--dpa-range: invalid length '%s'\n", colon + 1);
		free(copy);
		return -1;
	}

	entry->starting_dpa = (uint64_t)start;
	entry->length = (uint64_t)length;
	free(copy);
	return 0;
}

/*
 * Parse sanitize args into a newly allocated request (caller frees).
 * Returns 0 on success.
 */
static int parse_sanitize_args(
	int argc, char **argv,
	struct cxlmi_cmd_memdev_media_operations_sanitize_req **out_req)
{
	struct cxlmi_cmd_memdev_media_ops_dpa_range_list_entry ranges[MEDIA_OPS_MAX_DPA_RANGES];
	struct cxlmi_cmd_memdev_media_operations_sanitize_req *req;
	uint8_t subclass = 0xff;
	uint32_t range_count = 0;
	size_t req_sz;
	int has_operation = 0;
	int i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--operation") == 0 && i + 1 < argc) {
			const char *op = argv[++i];

			if (strcmp(op, "sanitize") == 0) {
				subclass = MEDIA_OP_SUBCLASS_SANITIZE;
			} else if (strcmp(op, "zero") == 0) {
				subclass = MEDIA_OP_SUBCLASS_ZERO;
			} else {
				fprintf(stderr,
					"--operation: expected sanitize|zero, got '%s'\n",
					op);
				return -1;
			}
			has_operation = 1;
		} else if (strcmp(argv[i], "--dpa-range") == 0 && i + 1 < argc) {
			if (range_count >= MEDIA_OPS_MAX_DPA_RANGES) {
				fprintf(stderr,
					"--dpa-range: exceeds mailbox payload limit (%zu)\n",
					MEDIA_OPS_MAX_DPA_RANGES);
				return -1;
			}
			if (parse_dpa_range(argv[++i], &ranges[range_count]))
				return -1;
			range_count++;
		} else {
			fprintf(stderr, "Usage: %s\n", MEDIA_OP_SANITIZE_USAGE);
			return -1;
		}
	}

	if (!has_operation || range_count == 0) {
		fprintf(stderr, "Usage: %s\n", MEDIA_OP_SANITIZE_USAGE);
		return -1;
	}

	req_sz = sizeof(*req) +
		 range_count * sizeof(struct cxlmi_cmd_memdev_media_ops_dpa_range_list_entry);
	req = calloc(1, req_sz);
	if (!req) {
		perror("media-operation sanitize: calloc");
		return -1;
	}

	req->media_operation_class = MEDIA_OP_CLASS_SANITIZE;
	req->media_operation_subclass = subclass;
	req->dpa_range_count = range_count;
	memcpy(req->dpa_range_list, ranges,
	       range_count * sizeof(ranges[0]));

	*out_req = req;
	return 0;
}

static int cmd_media_operation_discovery(struct cxlmi_endpoint *ep,
					 int argc, char **argv)
{
	struct cxlmi_cmd_memdev_media_operations_discovery_req req;
	struct cxlmi_cmd_memdev_media_operations_discovery_rsp *rsp;
	size_t rsp_sz;
	int rc;

	rc = parse_discovery_args(argc, argv, &req);
	if (rc)
		return rc;

	rsp_sz = sizeof(*rsp) +
		 req.discovery_osa.num_ops *
		 sizeof(struct cxlmi_cmd_memdev_media_ops_supported_list_entry);
	rsp = calloc(1, rsp_sz);
	if (!rsp) {
		perror("media-operation discovery: calloc");
		return -1;
	}

	rc = cxlmi_cmd_memdev_media_operations_discovery(ep, NULL, &req, rsp);
	if (rc) {
		if (rc > 0)
			fprintf(stderr,
				"media-operation discovery failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"media-operation discovery ioctl failed\n");
		free(rsp);
		return rc;
	}

	print_media_ops_discovery(rsp);
	free(rsp);
	return 0;
}

static int cmd_media_operation_sanitize(struct cxlmi_endpoint *ep,
					int argc, char **argv)
{
	struct cxlmi_cmd_memdev_media_operations_sanitize_req *req = NULL;
	uint32_t i;
	int rc;

	rc = parse_sanitize_args(argc, argv, &req);
	if (rc)
		return rc;

	rc = cxlmi_cmd_memdev_media_operations_sanitize(ep, NULL, req);
	if (rc && rc != CXLMI_RET_BACKGROUND) {
		if (rc > 0)
			fprintf(stderr,
				"media-operation sanitize failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"media-operation sanitize ioctl failed\n");
		free(req);
		return rc;
	}

	if (rc == CXLMI_RET_BACKGROUND)
		printf("Media operations sanitize started as background operation\n");
	else
		printf("Media operations sanitize OK\n");

	printf("  Class:    0x%02x\n", req->media_operation_class);
	printf("  Subclass: 0x%02x (%s)\n", req->media_operation_subclass,
	       media_op_name(req->media_operation_class,
			     req->media_operation_subclass));
	printf("  Ranges:   %u\n", req->dpa_range_count);
	for (i = 0; i < req->dpa_range_count; i++) {
		printf("  [%u] DPA=0x%016llx Length=0x%016llx\n", i,
		       (unsigned long long)req->dpa_range_list[i].starting_dpa,
		       (unsigned long long)req->dpa_range_list[i].length);
	}

	free(req);
	return 0;
}

int cmd_media_operation(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s\n", MEDIA_OP_USAGE);
		return -1;
	}

	if (strcmp(argv[1], "discovery") == 0)
		return cmd_media_operation_discovery(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "sanitize") == 0)
		return cmd_media_operation_sanitize(ep, argc - 2, argv + 2);

	fprintf(stderr, "Unknown media-operation action: %s\n", argv[1]);
	fprintf(stderr, "Usage: %s\n", MEDIA_OP_USAGE);
	return -1;
}
