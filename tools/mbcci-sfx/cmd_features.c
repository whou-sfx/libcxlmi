// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Get Supported Features (0500h).
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
	enum mbcci_feature_kind kind;
};

/*
 * UUID bytes match the 8-4-4-4-12 representation used by print_log_uuid().
 */
static const struct feature_uuid_name known_features[] = {
	{ { 0x89, 0x2b, 0xa4, 0x75, 0xfa, 0xd8, 0x47, 0x4e,
	    0x9d, 0x3e, 0x69, 0x2c, 0x91, 0x75, 0x68, 0xbb },
	  "sPPR", MBCCI_FEAT_SPPR },
	{ { 0x80, 0xea, 0x45, 0x21, 0x78, 0x6f, 0x41, 0x27,
	    0xaf, 0xb1, 0xec, 0x74, 0x59, 0xfb, 0x0e, 0x24 },
	  "hPPR", MBCCI_FEAT_HPPR },
	{ { 0x96, 0xda, 0xd7, 0xd6, 0xfd, 0xe8, 0x48, 0x2b,
	    0xa7, 0x33, 0x75, 0x77, 0x4e, 0x06, 0xdb, 0x8a },
	  "Device Patrol Scrub Control", MBCCI_FEAT_PARTIAL_SCRUB },
	{ { 0xe5, 0xb1, 0x3f, 0x22, 0x23, 0x28, 0x4a, 0x14,
	    0xb8, 0xba, 0xb9, 0x69, 0x1e, 0x89, 0x33, 0x86 },
	  "DDR5 ECS Control", MBCCI_FEAT_DDR5_ECS },
	{ { 0x14, 0x78, 0xad, 0x9d, 0xce, 0x00, 0x47, 0x33,
	    0x9d, 0xb8, 0xf3, 0x92, 0xa4, 0xc2, 0xd0, 0xcc },
	  "CVME Threshold", MBCCI_FEAT_CVME },
	{ { 0xf1, 0x82, 0xcc, 0xf8, 0x72, 0xbd, 0x11, 0xee,
	    0xb9, 0x62, 0x02, 0x42, 0xac, 0x12, 0x00, 0x02 },
	  "Addressing Policy", MBCCI_FEAT_ADDRESS_POLICY },
	{ { 0x51, 0x74, 0xe5, 0x99, 0x14, 0x30, 0x43, 0x3e,
	    0xaf, 0x4b, 0x57, 0x72, 0xba, 0xe6, 0xcc, 0x91 },
	  "RAS Features", MBCCI_FEAT_RAS },
	{ { 0xb4, 0x48, 0x97, 0xaf, 0xbd, 0xdb, 0x4e, 0x9b,
	    0x9d, 0x74, 0xdb, 0xab, 0x49, 0x06, 0x2f, 0x7b },
	  "CMC Refresh", MBCCI_FEAT_CMC_REFRESH },
	{ { 0xb0, 0x07, 0x26, 0xe4, 0xde, 0x86, 0x42, 0x05,
	    0xb2, 0x7f, 0xb0, 0xbb, 0x68, 0x25, 0x66, 0x0d },
	  "Dual Port", MBCCI_FEAT_DUAL_PORT },
};

enum mbcci_feature_kind mbcci_feature_kind(const uint8_t *uuid)
{
	size_t i;

	for (i = 0; i < sizeof(known_features) / sizeof(known_features[0]); i++) {
		if (memcmp(known_features[i].uuid, uuid, 16) == 0)
			return known_features[i].kind;
	}
	return MBCCI_FEAT_UNKNOWN;
}

static void feature_usage(FILE *out, const char *prefix)
{
	fprintf(out,
		"Usage: %s--feature-id <uuid> [--offset <n>] [--count <n>]"
		" [--selection <n>] [--dump <file>]\n"
		"  --feature-id  feature UUID (32-char hex or standard format)\n"
		"  --offset      byte offset into feature data (default 0)\n"
		"  --count       bytes to retrieve (default: get_feature_size)\n"
		"  --selection   feature selection value (default 0)\n"
		"  --dump        write response payload as continuous lowercase hex\n",
		prefix);
}

static void set_feature_usage(FILE *out, const char *prefix)
{
	fprintf(out,
		"Usage: %sset-feature --feature-id <uuid> --input <hexfile>"
		" [--offset <n>] [--flags <n>] [--version <n>]\n"
		"  --feature-id  feature UUID (32-char hex or standard format)\n"
		"  --input       continuous lowercase hex payload file\n"
		"  --offset      byte offset into feature data (default 0)\n"
		"  --flags       set_feature_flags (default 0)\n"
		"  --version     set feature version (default: from device or spec)\n",
		prefix);
}

int write_hex_payload_file(const char *path, const uint8_t *buf, size_t len)
{
	FILE *fp;
	size_t i;

	fp = fopen(path, "w");
	if (!fp)
		return -1;

	for (i = 0; i < len; i++) {
		if (fprintf(fp, "%02x", buf[i]) < 0) {
			fclose(fp);
			return -1;
		}
	}

	if (fclose(fp) != 0)
		return -1;

	return 0;
}

int read_hex_payload_file(const char *path, uint8_t *buf, size_t max_len,
			  size_t *out_len)
{
	FILE *fp;
	int c, hi = -1;
	size_t n = 0;

	fp = fopen(path, "r");
	if (!fp)
		return -1;

	while ((c = fgetc(fp)) != EOF) {
		if (isspace(c))
			continue;
		if (!isxdigit(c)) {
			fclose(fp);
			return -2;
		}
		if (hi < 0) {
			hi = c;
			continue;
		}

		if (n >= max_len) {
			fclose(fp);
			return -3;
		}

		buf[n++] = (uint8_t)((isdigit(hi) ? hi - '0' :
				      toupper(hi) - 'A' + 10) << 4);
		buf[n - 1] |= (uint8_t)(isdigit(c) ? c - '0' :
					toupper(c) - 'A' + 10);
		hi = -1;
	}

	fclose(fp);

	if (hi >= 0)
		return -2;

	*out_len = n;
	return 0;
}

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

int parse_get_feature_req(int argc, char **argv,
			  struct get_feature_params *params)
{
	int i;

	memset(params, 0, sizeof(*params));
	params->req.selection = 0;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--feature-id") == 0 && i + 1 < argc) {
			if (parse_log_uuid(argv[++i], params->req.feature_id) != 0) {
				fprintf(stderr,
					"get-feature: invalid --feature-id\n");
				return -1;
			}
		} else if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc) {
			char *end;
			unsigned long val = strtoul(argv[++i], &end, 0);

			if (*end != '\0' || val > UINT16_MAX) {
				fprintf(stderr,
					"get-feature: invalid --offset '%s'\n",
					argv[i]);
				return -1;
			}
			params->req.offset = (uint16_t)val;
		} else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
			char *end;
			unsigned long val = strtoul(argv[++i], &end, 0);

			if (*end != '\0' || val == 0 || val > UINT16_MAX) {
				fprintf(stderr,
					"get-feature: invalid --count '%s'\n",
					argv[i]);
				return -1;
			}
			params->req.count = (uint16_t)val;
			params->has_count = 1;
		} else if (strcmp(argv[i], "--selection") == 0 && i + 1 < argc) {
			char *end;
			unsigned long val = strtoul(argv[++i], &end, 0);

			if (*end != '\0' || val > UINT8_MAX) {
				fprintf(stderr,
					"get-feature: invalid --selection '%s'\n",
					argv[i]);
				return -1;
			}
			params->req.selection = (uint8_t)val;
		} else if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
			params->dump_file = argv[++i];
		} else {
			feature_usage(stderr, "get-feature ");
			return -1;
		}
	}

	if (!memcmp(params->req.feature_id, (uint8_t[16]){ 0 }, 16)) {
		feature_usage(stderr, "get-feature ");
		return -1;
	}

	return 0;
}

uint16_t lookup_feature_size(
	const struct cxlmi_cmd_get_supported_features_rsp *sfrsp,
	const uint8_t feature_id[16])
{
	uint16_t i;

	for (i = 0; i < sfrsp->num_supported_feature_entries; i++) {
		if (memcmp(sfrsp->supported_feature_entries[i].feature_id,
			   feature_id, 16) == 0)
			return sfrsp->supported_feature_entries[i].get_feature_size;
	}
	return 0;
}

/*
 * Documented Get Feature Size from docs/cxl_features.md / CXL_JEDEC_Tables.md.
 * Returns 0 for variable-size features (e.g. DDR5 ECS n*4+1) — caller must
 * query the device via Get Supported Features (0500h) or accept --count.
 */
uint16_t lookup_feature_size_doc(const uint8_t feature_id[16])
{
	switch (mbcci_feature_kind(feature_id)) {
	case MBCCI_FEAT_SPPR:
	case MBCCI_FEAT_HPPR:
		return 20;
	case MBCCI_FEAT_PARTIAL_SCRUB:
		return 4;
	case MBCCI_FEAT_DDR5_ECS:
		return 0;
	case MBCCI_FEAT_CVME:
		return 32;
	case MBCCI_FEAT_ADDRESS_POLICY:
		return 2;
	case MBCCI_FEAT_RAS:
		return 19;
	case MBCCI_FEAT_CMC_REFRESH:
		return 2;
	case MBCCI_FEAT_DUAL_PORT:
		return 33;
	default:
		return 0;
	}
}

static uint16_t mailbox_lookup_feature_size(struct cxlmi_endpoint *ep,
					    const uint8_t feature_id[16])
{
	struct cxlmi_cmd_get_supported_features_req sf_req = { 0 };
	struct cxlmi_cmd_get_supported_features_rsp *sfrsp;
	uint16_t size;

	sf_req.count = FEATURE_DEFAULT_COUNT;
	sf_req.starting_feature_index = 0;

	sfrsp = fetch_supported_features(ep, &sf_req);
	if (!sfrsp)
		return 0;

	size = lookup_feature_size(sfrsp, feature_id);
	free(sfrsp);
	return size;
}

uint16_t resolve_get_feature_count(struct cxlmi_endpoint *ep,
				   const uint8_t feature_id[16])
{
	uint16_t size;

	size = lookup_feature_size_doc(feature_id);
	if (size)
		return size;

	return mailbox_lookup_feature_size(ep, feature_id);
}

uint16_t lookup_set_feature_size(
	const struct cxlmi_cmd_get_supported_features_rsp *sfrsp,
	const uint8_t feature_id[16])
{
	uint16_t i;

	for (i = 0; i < sfrsp->num_supported_feature_entries; i++) {
		if (memcmp(sfrsp->supported_feature_entries[i].feature_id,
			   feature_id, 16) == 0)
			return sfrsp->supported_feature_entries[i].set_feature_size;
	}
	return 0;
}

uint8_t lookup_set_feature_version(
	const struct cxlmi_cmd_get_supported_features_rsp *sfrsp,
	const uint8_t feature_id[16])
{
	uint16_t i;

	for (i = 0; i < sfrsp->num_supported_feature_entries; i++) {
		if (memcmp(sfrsp->supported_feature_entries[i].feature_id,
			   feature_id, 16) == 0)
			return sfrsp->supported_feature_entries[i].set_feature_version;
	}
	return 0;
}

uint16_t lookup_set_feature_size_doc(const uint8_t feature_id[16])
{
	switch (mbcci_feature_kind(feature_id)) {
	case MBCCI_FEAT_SPPR:
		return 3;
	case MBCCI_FEAT_HPPR:
		return 12;
	case MBCCI_FEAT_PARTIAL_SCRUB:
		return 2;
	case MBCCI_FEAT_DDR5_ECS:
		return 0;
	case MBCCI_FEAT_CVME:
		return 32;
	case MBCCI_FEAT_ADDRESS_POLICY:
		return 2;
	case MBCCI_FEAT_RAS:
		return 12;
	case MBCCI_FEAT_CMC_REFRESH:
		return 2;
	case MBCCI_FEAT_DUAL_PORT:
		return 33;
	default:
		return 0;
	}
}

uint8_t lookup_set_feature_version_doc(const uint8_t feature_id[16])
{
	switch (mbcci_feature_kind(feature_id)) {
	case MBCCI_FEAT_SPPR:
	case MBCCI_FEAT_HPPR:
		return 3;
	case MBCCI_FEAT_PARTIAL_SCRUB:
	case MBCCI_FEAT_DDR5_ECS:
	case MBCCI_FEAT_CVME:
		return 1;
	default:
		return 0;
	}
}

static uint16_t mailbox_lookup_set_feature_size(struct cxlmi_endpoint *ep,
						const uint8_t feature_id[16])
{
	struct cxlmi_cmd_get_supported_features_req sf_req = { 0 };
	struct cxlmi_cmd_get_supported_features_rsp *sfrsp;
	uint16_t size;

	sf_req.count = FEATURE_DEFAULT_COUNT;
	sf_req.starting_feature_index = 0;

	sfrsp = fetch_supported_features(ep, &sf_req);
	if (!sfrsp)
		return 0;

	size = lookup_set_feature_size(sfrsp, feature_id);
	free(sfrsp);
	return size;
}

static uint8_t mailbox_lookup_set_feature_version(struct cxlmi_endpoint *ep,
						  const uint8_t feature_id[16])
{
	struct cxlmi_cmd_get_supported_features_req sf_req = { 0 };
	struct cxlmi_cmd_get_supported_features_rsp *sfrsp;
	uint8_t version;

	sf_req.count = FEATURE_DEFAULT_COUNT;
	sf_req.starting_feature_index = 0;

	sfrsp = fetch_supported_features(ep, &sf_req);
	if (!sfrsp)
		return 0;

	version = lookup_set_feature_version(sfrsp, feature_id);
	free(sfrsp);
	return version;
}

uint16_t resolve_set_feature_count(struct cxlmi_endpoint *ep,
				   const uint8_t feature_id[16])
{
	uint16_t size;

	size = lookup_set_feature_size_doc(feature_id);
	if (size)
		return size;

	return mailbox_lookup_set_feature_size(ep, feature_id);
}

uint8_t resolve_set_feature_version(struct cxlmi_endpoint *ep,
				    const uint8_t feature_id[16])
{
	uint8_t version;

	version = lookup_set_feature_version_doc(feature_id);
	if (version)
		return version;

	return mailbox_lookup_set_feature_version(ep, feature_id);
}

void print_feature_header(const struct cxlmi_cmd_get_feature_req *req)
{
	printf("Feature ID: ");
	print_log_uuid(req->feature_id);
	printf("  (%s)\n", lookup_feature_name(req->feature_id));
	printf("Offset: %u  Count: %u  Selection: %u\n",
	       req->offset, req->count, req->selection);
}

int cmd_get_feature(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct get_feature_params params;
	struct cxlmi_cmd_get_feature_rsp *rsp;
	int rc;

	rc = parse_get_feature_req(argc - 1, argv + 1, &params);
	if (rc)
		return rc;

	if (!params.has_count) {
		params.req.count = resolve_get_feature_count(ep,
					params.req.feature_id);
		if (params.req.count == 0) {
			fprintf(stderr,
				"get-feature: cannot determine feature data size "
				"(not in supported-features list; use --count <bytes>)\n");
			return -1;
		}
	}

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp) {
		fprintf(stderr, "get-feature: out of memory\n");
		return -1;
	}

	rc = cxlmi_cmd_get_feature(ep, NULL, &params.req, rsp);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get-feature failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get-feature ioctl failed\n");
		free(rsp);
		return rc;
	}

	print_feature_header(&params.req);

	if (params.dump_file) {
		rc = write_hex_payload_file(params.dump_file, rsp->feature_data,
					    params.req.count);
		if (rc) {
			fprintf(stderr, "get-feature: failed to write '%s': ",
				params.dump_file);
			perror(NULL);
			free(rsp);
			return -1;
		}
	}

	print_feature_data(params.req.feature_id, params.req.offset,
			   params.req.count, rsp->feature_data);
	free(rsp);
	return 0;
}

int parse_set_feature_req(int argc, char **argv,
			  struct set_feature_params *params)
{
	int i;

	memset(params, 0, sizeof(*params));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--feature-id") == 0 && i + 1 < argc) {
			if (parse_log_uuid(argv[++i], params->feature_id) != 0) {
				fprintf(stderr,
					"set-feature: invalid --feature-id\n");
				return -1;
			}
		} else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
			params->input_file = argv[++i];
		} else if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc) {
			char *end;
			unsigned long val = strtoul(argv[++i], &end, 0);

			if (*end != '\0' || val > UINT16_MAX) {
				fprintf(stderr,
					"set-feature: invalid --offset '%s'\n",
					argv[i]);
				return -1;
			}
			params->offset = (uint16_t)val;
		} else if (strcmp(argv[i], "--flags") == 0 && i + 1 < argc) {
			char *end;
			unsigned long val = strtoul(argv[++i], &end, 0);

			if (*end != '\0' || val > UINT32_MAX) {
				fprintf(stderr,
					"set-feature: invalid --flags '%s'\n",
					argv[i]);
				return -1;
			}
			params->set_feature_flags = (uint32_t)val;
		} else if (strcmp(argv[i], "--version") == 0 && i + 1 < argc) {
			char *end;
			unsigned long val = strtoul(argv[++i], &end, 0);

			if (*end != '\0' || val > UINT8_MAX) {
				fprintf(stderr,
					"set-feature: invalid --version '%s'\n",
					argv[i]);
				return -1;
			}
			params->version = (uint8_t)val;
			params->has_version = 1;
		} else {
			set_feature_usage(stderr, "");
			return -1;
		}
	}

	if (!memcmp(params->feature_id, (uint8_t[16]){ 0 }, 16) ||
	    !params->input_file) {
		set_feature_usage(stderr, "");
		return -1;
	}

	return 0;
}

int cmd_set_feature(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct set_feature_params params;
	struct cxlmi_cmd_set_feature_req *set_req;
	uint8_t payload[CXL_MAILBOX_MAX_PAYLOAD_SIZE];
	size_t payload_len = 0;
	uint16_t expected_size;
	uint8_t version;
	int rc;

	rc = parse_set_feature_req(argc - 1, argv + 1, &params);
	if (rc)
		return rc;

	expected_size = resolve_set_feature_count(ep, params.feature_id);
	if (expected_size == 0) {
		fprintf(stderr,
			"set-feature: feature not writable or cannot determine set_feature_size\n");
		return -1;
	}

	rc = read_hex_payload_file(params.input_file, payload,
				   sizeof(payload), &payload_len);
	if (rc == -1) {
		fprintf(stderr, "set-feature: cannot open '%s': ",
			params.input_file);
		perror(NULL);
		return -1;
	}
	if (rc == -2) {
		fprintf(stderr,
			"set-feature: invalid hex in '%s'\n",
			params.input_file);
		return -1;
	}
	if (rc == -3) {
		fprintf(stderr,
			"set-feature: payload in '%s' exceeds maximum size\n",
			params.input_file);
		return -1;
	}

	if (payload_len != expected_size) {
		fprintf(stderr,
			"set-feature: payload size %zu does not match expected %u bytes\n",
			payload_len, expected_size);
		return -1;
	}

	if (params.has_version)
		version = params.version;
	else {
		version = resolve_set_feature_version(ep, params.feature_id);
		if (!version) {
			fprintf(stderr,
				"set-feature: cannot determine set_feature_version "
				"(use --version <n>)\n");
			return -1;
		}
	}

	set_req = calloc(1, sizeof(*set_req) + payload_len);
	if (!set_req) {
		fprintf(stderr, "set-feature: out of memory\n");
		return -1;
	}

	memcpy(set_req->feature_id, params.feature_id, 16);
	set_req->set_feature_flags = params.set_feature_flags;
	set_req->offset = params.offset;
	set_req->version = version;
	memcpy(set_req->feature_data, payload, payload_len);

	rc = cxlmi_cmd_set_feature(ep, NULL, set_req, payload_len);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "set-feature failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "set-feature ioctl failed\n");
		free(set_req);
		return rc;
	}

	printf("Set feature ");
	print_log_uuid(params.feature_id);
	printf("  (%s), %zu bytes at offset %u\n",
	       lookup_feature_name(params.feature_id), payload_len,
	       params.offset);
	free(set_req);
	return 0;
}
