// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Dynamic Capacity commands (Opcodes 4800h-4803h).
 *
 *   get-dc-config        4800h  Get DC Configuration
 *   get-dc-extent-list   4801h  Get DC Extent List
 *   add-dc-response      4802h  Add DC Response
 *   release-dc           4803h  Release Dynamic Capacity
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

/* Library caps extent_cnt at 8 for 4801h; allocate accordingly. */
#define DC_GET_EXT_LIST_MAX  8
#define DC_MAX_EXTENTS       64

/* Parse "0x<hex>" or decimal into uint64. */
static int dc_parse_u64(const char *s, uint64_t *out, const char *name)
{
	char *end;

	*out = strtoull(s, &end, 0);
	if (end == s || *end != '\0') {
		fprintf(stderr, "%s: invalid value '%s'\n", name, s);
		return -1;
	}
	return 0;
}

/* Parse "<dpa>:<len>" for 4802h/4803h extent entries. */
static int dc_parse_extent(const char *arg, uint64_t *start_dpa, uint64_t *len)
{
	char *copy = strdup(arg);
	char *p, *end;
	int rc = -1;

	if (!copy) {
		perror("dc_parse_extent: strdup");
		return -1;
	}

	p = copy;
	*start_dpa = strtoull(p, &end, 0);
	if (end == p || *end != ':') {
		fprintf(stderr,
			"--extent: invalid format '%s' (expected dpa:len)\n", arg);
		goto out;
	}
	p = end + 1;
	*len = strtoull(p, &end, 0);
	if (end == p || *end != '\0') {
		fprintf(stderr,
			"--extent: invalid len in '%s'\n", arg);
		goto out;
	}
	rc = 0;
out:
	free(copy);
	return rc;
}

/* ------------------------------------------------------------------ */
/* 4800h: Get DC Configuration                                        */
/* ------------------------------------------------------------------ */

static void print_dc_config(const struct cxlmi_cmd_memdev_get_dc_config_rsp *r)
{
	uint8_t i;

	printf("  Num Regions:          %u\n", r->num_regions);
	printf("  Regions Returned:     %u\n", r->regions_returned);
	printf("  Extents Supported:    %u\n", r->num_extents_supported);
	printf("  Extents Available:    %u\n", r->num_extents_available);
	printf("  Tags Supported:       %u\n", r->num_tags_supported);
	printf("  Tags Available:       %u\n", r->num_tags_available);
	for (i = 0; i < r->regions_returned && i < 8; i++) {
		const typeof(r->region_configs[0]) *rc = &r->region_configs[i];

		printf("  Region[%u]:\n", i);
		printf("    Base:             0x%016llx\n",
		       (unsigned long long)rc->base);
		printf("    Decode Length:    0x%016llx\n",
		       (unsigned long long)rc->decode_len);
		printf("    Region Length:    0x%016llx\n",
		       (unsigned long long)rc->region_len);
		printf("    Block Size:       0x%016llx\n",
		       (unsigned long long)rc->block_size);
		printf("    DSMAD Handle:     0x%08x\n", rc->dsmadhandle);
		printf("    Flags:            0x%02x\n", rc->flags);
	}
}

int cmd_get_dc_config(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_get_dc_config_req req = { .region_cnt = 8 };
	struct cxlmi_cmd_memdev_get_dc_config_rsp rsp;
	int rc, i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--region-cnt") == 0 && i + 1 < argc) {
			uint64_t v;
			if (dc_parse_u64(argv[++i], &v, "--region-cnt"))
				return -1;
			req.region_cnt = (uint8_t)v;
		} else if (strcmp(argv[i], "--start-region-id") == 0 && i + 1 < argc) {
			uint64_t v;
			if (dc_parse_u64(argv[++i], &v, "--start-region-id"))
				return -1;
			req.start_region_id = (uint8_t)v;
		} else {
			fprintf(stderr,
				"Usage: get-dc-config"
				" [--region-cnt <n>] [--start-region-id <n>]\n");
			return -1;
		}
	}

	memset(&rsp, 0, sizeof(rsp));
	rc = cxlmi_cmd_memdev_get_dc_config(ep, NULL, &req, &rsp);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get-dc-config failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get-dc-config ioctl failed\n");
		return rc;
	}

	printf("DC Configuration:\n");
	print_dc_config(&rsp);
	return 0;
}

/* ------------------------------------------------------------------ */
/* 4801h: Get DC Extent List                                          */
/* ------------------------------------------------------------------ */

static void print_dc_extent_list(
		const struct cxlmi_cmd_memdev_get_dc_extent_list_rsp *rsp)
{
	uint32_t i;

	printf("  Extents Returned:  %u\n", rsp->num_extents_returned);
	printf("  Total Extents:     %u\n", rsp->total_num_extents);
	printf("  Generation Num:    %u\n", rsp->generation_num);
	for (i = 0; i < rsp->num_extents_returned; i++) {
		const typeof(rsp->extents[0]) *e = &rsp->extents[i];
		int j;

		printf("  [%u] DPA=0x%016llx Len=0x%016llx Tag=",
		       i,
		       (unsigned long long)e->start_dpa,
		       (unsigned long long)e->len);
		for (j = 0; j < 16; j++)
			printf("%02x", e->tag[j]);
		printf(" SharedSeq=%u\n", e->shared_seq);
	}
}

int cmd_get_dc_extent_list(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_get_dc_extent_list_req req = { .extent_cnt = 8 };
	struct cxlmi_cmd_memdev_get_dc_extent_list_rsp dummy;
	struct cxlmi_cmd_memdev_get_dc_extent_list_rsp *rsp;
	size_t rsp_sz;
	int rc, i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--extent-cnt") == 0 && i + 1 < argc) {
			uint64_t v;
			if (dc_parse_u64(argv[++i], &v, "--extent-cnt"))
				return -1;
			req.extent_cnt = (uint32_t)v;
		} else if (strcmp(argv[i], "--start-extent-idx") == 0 && i + 1 < argc) {
			uint64_t v;
			if (dc_parse_u64(argv[++i], &v, "--start-extent-idx"))
				return -1;
			req.start_extent_idx = (uint32_t)v;
		} else {
			fprintf(stderr,
				"Usage: get-dc-extent-list"
				" [--extent-cnt <n> (max 8)] [--start-extent-idx <n>]\n");
			return -1;
		}
	}

	/* Library caps extent_cnt at 8 internally. */
	rsp_sz = sizeof(dummy) +
		 DC_GET_EXT_LIST_MAX * sizeof(dummy.extents[0]);
	rsp = calloc(1, rsp_sz);
	if (!rsp) {
		perror("get-dc-extent-list: calloc");
		return -1;
	}

	rc = cxlmi_cmd_memdev_get_dc_extent_list(ep, NULL, &req, rsp);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get-dc-extent-list failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get-dc-extent-list ioctl failed\n");
		free(rsp);
		return rc;
	}

	printf("DC Extent List:\n");
	print_dc_extent_list(rsp);
	free(rsp);
	return 0;
}

/* ------------------------------------------------------------------ */
/* 4802h: Add DC Response                                             */
/* ------------------------------------------------------------------ */

int cmd_add_dc_response(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_add_dc_response_req *req;
	size_t req_sz;

	struct {
		uint64_t start_dpa, len;
	} extents[DC_MAX_EXTENTS];
	uint32_t ext_count = 0;
	uint8_t flags = 0;
	int rc, i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--flags") == 0 && i + 1 < argc) {
			uint64_t v;
			if (dc_parse_u64(argv[++i], &v, "--flags"))
				return -1;
			flags = (uint8_t)v;
		} else if (strcmp(argv[i], "--extent") == 0 && i + 1 < argc) {
			if (ext_count >= DC_MAX_EXTENTS) {
				fprintf(stderr, "--extent: too many (max %d)\n",
					DC_MAX_EXTENTS);
				return -1;
			}
			if (dc_parse_extent(argv[++i],
					    &extents[ext_count].start_dpa,
					    &extents[ext_count].len))
				return -1;
			ext_count++;
		} else {
			fprintf(stderr,
				"Usage: add-dc-response"
				" [--flags <n>]"
				" [--extent <dpa>:<len>] ...\n");
			return -1;
		}
	}

	req_sz = sizeof(*req) + ext_count * sizeof(req->extents[0]);
	req = calloc(1, req_sz);
	if (!req) {
		perror("add-dc-response: calloc");
		return -1;
	}

	req->updated_extent_list_size = ext_count;
	req->flags = flags;
	for (i = 0; i < (int)ext_count; i++) {
		req->extents[i].start_dpa = extents[i].start_dpa;
		req->extents[i].len       = extents[i].len;
	}

	rc = cxlmi_cmd_memdev_add_dc_response(ep, NULL, req);
	free(req);

	if (rc) {
		if (rc > 0)
			fprintf(stderr, "add-dc-response failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "add-dc-response ioctl failed\n");
		return rc;
	}

	printf("Add DC Response OK (ext_count=%u)\n", ext_count);
	return 0;
}

/* ------------------------------------------------------------------ */
/* 4803h: Release Dynamic Capacity                                    */
/* ------------------------------------------------------------------ */

int cmd_release_dc(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_release_dc_req *req;
	size_t req_sz;

	struct {
		uint64_t start_dpa, len;
	} extents[DC_MAX_EXTENTS];
	uint32_t ext_count = 0;
	uint8_t flags = 0;
	int rc, i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--flags") == 0 && i + 1 < argc) {
			uint64_t v;
			if (dc_parse_u64(argv[++i], &v, "--flags"))
				return -1;
			flags = (uint8_t)v;
		} else if (strcmp(argv[i], "--extent") == 0 && i + 1 < argc) {
			if (ext_count >= DC_MAX_EXTENTS) {
				fprintf(stderr, "--extent: too many (max %d)\n",
					DC_MAX_EXTENTS);
				return -1;
			}
			if (dc_parse_extent(argv[++i],
					    &extents[ext_count].start_dpa,
					    &extents[ext_count].len))
				return -1;
			ext_count++;
		} else {
			fprintf(stderr,
				"Usage: release-dc"
				" [--flags <n>]"
				" [--extent <dpa>:<len>] ...\n");
			return -1;
		}
	}

	req_sz = sizeof(*req) + ext_count * sizeof(req->extents[0]);
	req = calloc(1, req_sz);
	if (!req) {
		perror("release-dc: calloc");
		return -1;
	}

	req->updated_extent_list_size = ext_count;
	req->flags = flags;
	for (i = 0; i < (int)ext_count; i++) {
		req->extents[i].start_dpa = extents[i].start_dpa;
		req->extents[i].len       = extents[i].len;
	}

	rc = cxlmi_cmd_memdev_release_dc(ep, NULL, req);
	free(req);

	if (rc) {
		if (rc > 0)
			fprintf(stderr, "release-dc failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "release-dc ioctl failed\n");
		return rc;
	}

	printf("Release DC OK (ext_count=%u)\n", ext_count);
	return 0;
}
