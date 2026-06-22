// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Get Supported Features (0500h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define FEATURE_ENTRY_SZ 48
#define FEATURE_DEFAULT_COUNT (16 * FEATURE_ENTRY_SZ)

#define FEATURE_RSP_BUF_SZ(count) \
	(sizeof(struct cxlmi_cmd_get_supported_features_rsp) + (count))

/* Known feature UUIDs from docs/sfx_features.md */
struct feature_uuid_name {
	uint8_t     uuid[16];
	const char *name;
};

/*
 * UUID bytes match the 8-4-4-4-12 representation used by print_log_uuid().
 */
static const struct feature_uuid_name known_features[] = {
	{ { 0x89, 0x2b, 0xa4, 0x75, 0xfa, 0xd8, 0x47, 0x4e,
	    0x9d, 0x3e, 0x69, 0x2c, 0x91, 0x75, 0x68, 0xbb },
	  "sPPR" },
	{ { 0x80, 0xea, 0x45, 0x21, 0x78, 0x6f, 0x41, 0x27,
	    0xaf, 0xb1, 0xec, 0x74, 0x59, 0xfb, 0x0e, 0x24 },
	  "hPPR" },
	{ { 0x96, 0xda, 0xd7, 0xd6, 0xfd, 0xe8, 0x48, 0x2b,
	    0xa7, 0x33, 0x75, 0x77, 0x4e, 0x06, 0xdb, 0x8a },
	  "Device Patrol Scrub Control" },
	{ { 0xe5, 0xb1, 0x3f, 0x22, 0x23, 0x28, 0x4a, 0x14,
	    0xb8, 0xba, 0xb9, 0x69, 0x1e, 0x89, 0x33, 0x86 },
	  "DDR5 ECS Control" },
	{ { 0x14, 0x78, 0xad, 0x9d, 0xce, 0x00, 0x47, 0x33,
	    0x9d, 0xb8, 0xf3, 0x92, 0xa4, 0xc2, 0xd0, 0xcc },
	  "CVME Threshold" },
	{ { 0xf1, 0x82, 0xcc, 0xf8, 0x72, 0xbd, 0x11, 0xee,
	    0xb9, 0x62, 0x02, 0x42, 0xac, 0x12, 0x00, 0x02 },
	  "Addressing Policy" },
	{ { 0x51, 0x74, 0xe5, 0x99, 0x14, 0x30, 0x43, 0x3e,
	    0xaf, 0x4b, 0x57, 0x72, 0xba, 0xe6, 0xcc, 0x91 },
	  "RAS Features" },
	{ { 0xb4, 0x48, 0x97, 0xaf, 0xbd, 0xdb, 0x4e, 0x9b,
	    0x9d, 0x74, 0xdb, 0xab, 0x49, 0x06, 0x2f, 0x7b },
	  "CMC Refresh" },
	{ { 0xb0, 0x07, 0x26, 0xe4, 0xde, 0x86, 0x42, 0x05,
	    0xb2, 0x7f, 0xb0, 0xbb, 0x68, 0x25, 0x66, 0x0d },
	  "Dual Port" },
};

static const char *lookup_feature_name(const uint8_t *uuid)
{
	size_t i;

	for (i = 0; i < sizeof(known_features) / sizeof(known_features[0]); i++) {
		if (memcmp(known_features[i].uuid, uuid, 16) == 0)
			return known_features[i].name;
	}
	return "unknown";
}

int parse_get_supported_features_req(int argc, char **argv,
				     struct cxlmi_cmd_get_supported_features_req *req)
{
	int i;

	memset(req, 0, sizeof(*req));
	req->count = FEATURE_DEFAULT_COUNT;
	req->starting_feature_index = 0;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
			char *end;
			unsigned long val = strtoul(argv[++i], &end, 0);

			if (*end != '\0' || val > UINT32_MAX) {
				fprintf(stderr,
					"get-supported-feat: invalid --count '%s'\n",
					argv[i]);
				return -1;
			}
			req->count = (uint32_t)val;
		} else if (strcmp(argv[i], "--start-index") == 0 && i + 1 < argc) {
			char *end;
			unsigned long val = strtoul(argv[++i], &end, 0);

			if (*end != '\0' || val > UINT16_MAX) {
				fprintf(stderr,
					"get-supported-feat: invalid --start-index '%s'\n",
					argv[i]);
				return -1;
			}
			req->starting_feature_index = (uint16_t)val;
		} else {
			fprintf(stderr,
				"Usage: get-supported-feat [--count <bytes>] [--start-index <n>]\n"
				"       (--count is feature entry data bytes, not entry count; "
				"each entry is %u bytes)\n",
				FEATURE_ENTRY_SZ);
			return -1;
		}
	}

	return 0;
}

void print_supported_features(
	const struct cxlmi_cmd_get_supported_features_rsp *rsp)
{
	uint16_t n = rsp->num_supported_feature_entries;
	uint16_t i;

	printf("Supported feature entries: %u\n", n);
	printf("Device supported features:   0x%04x\n",
	       rsp->device_supported_features);

	for (i = 0; i < n; i++) {
		printf("  [%u] feature_id: ", i);
		print_log_uuid(rsp->supported_feature_entries[i].feature_id);
		printf("  (%s)\n",
		       lookup_feature_name(rsp->supported_feature_entries[i].feature_id));
		printf("       feature_index:      %u\n",
		       rsp->supported_feature_entries[i].feature_index);
		printf("       get_feature_size:   %u\n",
		       rsp->supported_feature_entries[i].get_feature_size);
		printf("       set_feature_size:   %u\n",
		       rsp->supported_feature_entries[i].set_feature_size);
		printf("       attribute_flags:    0x%08x\n",
		       rsp->supported_feature_entries[i].attribute_flags);
		printf("       get_feature_version: %u\n",
		       rsp->supported_feature_entries[i].get_feature_version);
		printf("       set_feature_version: %u\n",
		       rsp->supported_feature_entries[i].set_feature_version);
		printf("       set_feature_effects: 0x%04x\n",
		       rsp->supported_feature_entries[i].set_feature_effects);
	}
}

static struct cxlmi_cmd_get_supported_features_rsp *
fetch_supported_features(struct cxlmi_endpoint *ep,
			 const struct cxlmi_cmd_get_supported_features_req *req)
{
	struct cxlmi_cmd_get_supported_features_rsp *rsp;
	int rc;

	rsp = calloc(1, FEATURE_RSP_BUF_SZ(req->count));
	if (!rsp)
		return NULL;

	rc = cxlmi_cmd_get_supported_features(ep, NULL,
					      (struct cxlmi_cmd_get_supported_features_req *)req,
					      rsp);
	if (rc) {
		free(rsp);
		return NULL;
	}

	return rsp;
}

int cmd_get_supported_feat(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_get_supported_features_req req;
	struct cxlmi_cmd_get_supported_features_rsp *rsp;
	int rc;

	rc = parse_get_supported_features_req(argc - 1, argv + 1, &req);
	if (rc)
		return rc;

	rsp = fetch_supported_features(ep, &req);
	if (!rsp) {
		fprintf(stderr, "get-supported-feat failed\n");
		return -1;
	}

	print_supported_features(rsp);
	free(rsp);
	return 0;
}
