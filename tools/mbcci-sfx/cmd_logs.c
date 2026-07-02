// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Get Supported Logs (0400h) and Get Log (0401h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define VENDOR_LOG_CHUNK  2048

/* Vendor Debug Log UUID: 5e1819d9-11a9-400c-811f-d60719403d86 */
static const uint8_t vendor_debug_log_uuid[16] = {
	0x5e, 0x18, 0x19, 0xd9, 0x11, 0xa9, 0x40, 0x0c,
	0x81, 0x1f, 0xd6, 0x07, 0x19, 0x40, 0x3d, 0x86
};

static const uint8_t ecs_log_uuid[16] = {
	0xf1, 0x72, 0x0d, 0x60, 0xa7, 0xa9, 0x43, 0x06,
	0xa0, 0x03, 0x11, 0x94, 0x8f, 0x9e, 0x07, 0x7c
};

static const uint8_t media_test_cap_log_uuid[16] = {
	0xe6, 0xdf, 0xa3, 0x2c, 0xd1, 0x3e, 0x4a, 0x5c,
	0x8c, 0xa8, 0x99, 0xbe, 0xbb, 0xf7, 0x31, 0xa4
};

static const uint8_t media_test_short_log_uuid[16] = {
	0x2c, 0x25, 0x55, 0x22, 0x8c, 0xe4, 0x11, 0xec,
	0xb9, 0x09, 0x02, 0x42, 0xac, 0x12, 0x00, 0x02
};

static const uint8_t media_test_long_log_uuid[16] = {
	0xc1, 0xfe, 0x0b, 0x3e, 0x7a, 0x00, 0x44, 0x8e,
	0xa2, 0x4e, 0xa6, 0xaa, 0xbb, 0xfe, 0x58, 0x7a
};

/* Known log UUIDs from CXL spec (Generic-Component-Commands.md). */
struct uuid_name {
	uint8_t     uuid[16];
	const char *name;
};

/*
 * UUID bytes are stored in the byte order that matches the printed
 * 8-4-4-4-12 representation when read left-to-right.
 */
static const struct uuid_name known_uuids[] = {
	{ { 0x0d, 0xa9, 0xc0, 0xb5, 0xbf, 0x41, 0x4b, 0x78,
	    0x8f, 0x79, 0x96, 0xb1, 0x62, 0x3b, 0x3f, 0x17 },
	  "Command Effects Log (CEL)" },
	{ { 0x5e, 0x18, 0x19, 0xd9, 0x11, 0xa9, 0x40, 0x0c,
	    0x81, 0x1f, 0xd6, 0x07, 0x19, 0x40, 0x3d, 0x86 },
	  "Vendor Debug Log" },
	{ { 0xb3, 0xfa, 0xb4, 0xcf, 0x01, 0xb6, 0x43, 0x32,
	    0x94, 0x3e, 0x5e, 0x99, 0x62, 0xf2, 0x35, 0x67 },
	  "Component State Dump Log" },
	{ { 0xf1, 0x72, 0x0d, 0x60, 0xa7, 0xa9, 0x43, 0x06,
	    0xa0, 0x03, 0x11, 0x94, 0x8f, 0x9e, 0x07, 0x7c },
	  "DDR5 Error Check Scrub (ECS) Log" },
	{ { 0xe6, 0xdf, 0xa3, 0x2c, 0xd1, 0x3e, 0x4a, 0x5c,
	    0x8c, 0xa8, 0x99, 0xbe, 0xbb, 0xf7, 0x31, 0xa4 },
	  "Media Test Capability Log" },
	{ { 0x2c, 0x25, 0x55, 0x22, 0x8c, 0xe4, 0x11, 0xec,
	    0xb9, 0x09, 0x02, 0x42, 0xac, 0x12, 0x00, 0x02 },
	  "Media Test Results Short Log" },
	{ { 0xc1, 0xfe, 0x0b, 0x3e, 0x7a, 0x00, 0x44, 0x8e,
	    0xa2, 0x4e, 0xa6, 0xaa, 0xbb, 0xfe, 0x58, 0x7a },
	  "Media Test Results Long Log" },
};

static const char *lookup_uuid_name(const uint8_t *uuid)
{
	size_t i;

	for (i = 0; i < sizeof(known_uuids) / sizeof(known_uuids[0]); i++) {
		if (memcmp(known_uuids[i].uuid, uuid, 16) == 0)
			return known_uuids[i].name;
	}
	return "unknown";
}

static uint16_t read_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint64_t read_le64(const uint8_t *p)
{
	return (uint64_t)p[0] |
	       ((uint64_t)p[1] << 8) |
	       ((uint64_t)p[2] << 16) |
	       ((uint64_t)p[3] << 24) |
	       ((uint64_t)p[4] << 32) |
	       ((uint64_t)p[5] << 40) |
	       ((uint64_t)p[6] << 48) |
	       ((uint64_t)p[7] << 56);
}

static int ecs_uuid_match(const uint8_t uuid[16])
{
	return memcmp(uuid, ecs_log_uuid, 16) == 0;
}

static int media_test_cap_uuid_match(const uint8_t uuid[16])
{
	return memcmp(uuid, media_test_cap_log_uuid, 16) == 0;
}

static int media_test_short_uuid_match(const uint8_t uuid[16])
{
	return memcmp(uuid, media_test_short_log_uuid, 16) == 0;
}

static int media_test_long_uuid_match(const uint8_t uuid[16])
{
	return memcmp(uuid, media_test_long_log_uuid, 16) == 0;
}

static void print_ddr5_ecs_log(const uint8_t *buf, uint32_t length)
{
	const uint32_t header_sz = 4;
	const uint32_t entry_sz = 34;
	uint32_t entries, i;

	if (length < header_sz) {
		printf("DDR5 ECS log: payload too short (%u)\n", length);
		return;
	}

	entries = (length - header_sz) / entry_sz;
	printf("DDR5 ECS log decode:\n");
	printf("  Header raw: 0x%02x%02x%02x%02x\n",
	       buf[0], buf[1], buf[2], buf[3]);
	printf("  Entries: %u\n", entries);

	for (i = 0; i < entries; i++) {
		const uint8_t *e = buf + header_sz + i * entry_sz;
		uint64_t dram_addr = read_le64(e + 1) & 0x0000FFFFFFFFFFFFULL;
		uint16_t err_cnt = read_le16(e + 7);
		uint16_t threshold = read_le16(e + 9);

		printf("  [%u] flags=0x%02x dram_addr=0x%012" PRIx64
		       " ecs_error_count=%u ecs_threshold=%u\n",
		       i, e[0], dram_addr, err_cnt, threshold);
	}
}

static void print_media_test_capability_log(const uint8_t *buf, uint32_t length)
{
	const uint32_t header_sz = 16;
	const uint32_t entry_sz = 16;
	uint32_t entries, i;

	if (length < header_sz) {
		printf("Media Test Capability log: payload too short (%u)\n", length);
		return;
	}

	entries = (length - header_sz) / entry_sz;
	printf("Media Test Capability log decode:\n");
	printf("  Header[0:15] raw, entries (from size): %u\n", entries);

	for (i = 0; i < entries; i++) {
		const uint8_t *e = buf + header_sz + i * entry_sz;
		uint16_t expected_time_s_per_gb = read_le16(e + 2);

		printf("  [%u] algo=0x%02x flags=0x%02x expected_time_s_per_gb=%u\n",
		       i, e[0], e[1], expected_time_s_per_gb);
	}
}

static void print_media_test_result_short_log(const uint8_t *buf, uint32_t length)
{
	const uint32_t header_sz = 16;
	const uint32_t entry_sz = 32;
	uint32_t entries, i;

	if (length < header_sz) {
		printf("Media Test Result Short log: payload too short (%u)\n", length);
		return;
	}

	entries = (length - header_sz) / entry_sz;
	printf("Media Test Result Short log decode:\n");
	printf("  Header[0:15] raw, entries (from size): %u\n", entries);

	for (i = 0; i < entries; i++) {
		const uint8_t *e = buf + header_sz + i * entry_sz;
		uint64_t start_time = read_le64(e + 0x08);
		uint64_t end_time = read_le64(e + 0x10);

		printf("  [%u] status=0x%02x flags=0x%02x start_time=%" PRIu64
		       " end_time=%" PRIu64 "\n",
		       i, e[0], e[1], start_time, end_time);
	}
}

static void print_media_test_result_long_log(const uint8_t *buf, uint32_t length)
{
	const uint32_t header_sz = 16;
	const uint32_t summary_sz = 48;
	const uint32_t sig_sz = 80;
	uint16_t task_status, error_count;
	uint64_t start_time, end_time;
	uint64_t range_start, range_len;
	uint32_t remaining, sig_count, i;
	const uint8_t *summary;

	if (length < header_sz + summary_sz) {
		printf("Media Test Result Long log: payload too short (%u)\n", length);
		return;
	}

	summary = buf + header_sz;
	task_status = read_le16(summary + 0x00);
	error_count = read_le16(summary + 0x02);
	start_time = read_le64(summary + 0x08);
	end_time = read_le64(summary + 0x10);
	range_start = read_le64(summary + 0x18);
	range_len = read_le64(summary + 0x20);

	remaining = length - (header_sz + summary_sz);
	sig_count = remaining / sig_sz;

	printf("Media Test Result Long log decode:\n");
	printf("  task_status=0x%04x error_count=%u start_time=%" PRIu64
	       " end_time=%" PRIu64 "\n",
	       task_status, error_count, start_time, end_time);
	printf("  test_range_start=0x%016" PRIx64 " test_range_len=0x%016" PRIx64
	       " signatures=%u\n",
	       range_start, range_len, sig_count);

	for (i = 0; i < sig_count; i++) {
		const uint8_t *s = buf + header_sz + summary_sz + i * sig_sz;
		uint64_t failed_dpa = read_le64(s + 0x00);
		uint16_t error_type = read_le16(s + 0x08);

		printf("  [%u] failed_dpa=0x%016" PRIx64 " error_type=0x%04x\n",
		       i, failed_dpa, error_type);
	}
}

/* Print UUID in standard 8-4-4-4-12 format. */
void print_log_uuid(const uint8_t *uuid)
{
	printf("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
	       "%02x%02x%02x%02x%02x%02x",
	       uuid[0],  uuid[1],  uuid[2],  uuid[3],
	       uuid[4],  uuid[5],
	       uuid[6],  uuid[7],
	       uuid[8],  uuid[9],
	       uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
}

/*
 * Allocate and fill a get_supported_logs response.
 * Caller must free() the returned pointer.
 * Returns NULL on allocation or command failure.
 */
static struct cxlmi_cmd_get_supported_logs_rsp *
fetch_supported_logs(struct cxlmi_endpoint *ep)
{
	size_t sz = sizeof(struct cxlmi_cmd_get_supported_logs_rsp) +
		    CXLMI_MAX_SUPPORTED_LOGS *
		    sizeof(struct cxlmi_supported_log_entry);
	struct cxlmi_cmd_get_supported_logs_rsp *rsp = calloc(1, sz);

	if (!rsp)
		return NULL;

	if (cxlmi_cmd_get_supported_logs(ep, NULL, rsp)) {
		free(rsp);
		return NULL;
	}
	return rsp;
}

void print_supported_logs(const struct cxlmi_cmd_get_supported_logs_rsp *rsp)
{
	uint16_t n = rsp->num_supported_log_entries;
	uint16_t i;

	printf("Supported log entries: %u\n", n);
	for (i = 0; i < n; i++) {
		printf("  [%u] UUID: ", i);
		print_log_uuid(rsp->entries[i].uuid);
		printf("  log_size: %7u bytes  (%s)\n",
		       rsp->entries[i].log_size,
		       lookup_uuid_name(rsp->entries[i].uuid));
	}
}

int cmd_get_supported_logs(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_get_supported_logs_rsp *rsp;

	(void)argc;
	(void)argv;

	rsp = fetch_supported_logs(ep);
	if (!rsp) {
		fprintf(stderr, "get-supported-logs failed\n");
		return -1;
	}

	print_supported_logs(rsp);
	free(rsp);
	return 0;
}

/*
 * Parse a UUID string into 16 bytes.
 * Accepts both:
 *   - 32-char hex without dashes:  0da9c0b5bf414b788f7996b1623b3f17
 *   - 36-char standard format:     0da9c0b5-bf41-4b78-8f79-96b1623b3f17
 */
int parse_log_uuid(const char *str, uint8_t *out)
{
	char hex[33] = { 0 };
	int j = 0;
	size_t i;

	for (i = 0; str[i] != '\0' && j < 32; i++) {
		if (str[i] == '-')
			continue;
		hex[j++] = str[i];
	}

	if (j != 32) {
		fprintf(stderr,
			"get-log: --uuid must be a 32-char hex string or "
			"standard UUID format (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)\n");
		return -1;
	}

	for (i = 0; i < 16; i++) {
		unsigned int byte;

		if (sscanf(hex + 2 * i, "%02x", &byte) != 1) {
			fprintf(stderr,
				"get-log: invalid hex in --uuid at position %zu\n",
				2 * i);
			return -1;
		}
		out[i] = (uint8_t)byte;
	}
	return 0;
}

int parse_get_log_req(int argc, char **argv, struct get_log_params *params)
{
	int i;

	memset(params, 0, sizeof(*params));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--uuid") == 0 && i + 1 < argc) {
			if (parse_log_uuid(argv[++i], params->uuid) != 0)
				return -1;
			params->has_uuid = 1;
		} else if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc) {
			params->offset = (uint32_t)strtoul(argv[++i], NULL, 0);
		} else if (strcmp(argv[i], "--length") == 0 && i + 1 < argc) {
			params->length = (uint32_t)strtoul(argv[++i], NULL, 0);
			params->has_length = 1;
		} else if (strcmp(argv[i], "--text") == 0) {
			params->has_text = 1;
		} else {
			fprintf(stderr,
				"Usage: get-log --uuid <uuid> [--offset <n>] [--length <n>] [--text]\n");
			return -1;
		}
	}

	if (!params->has_uuid) {
		fprintf(stderr,
			"Usage: get-log --uuid <uuid> [--offset <n>] [--length <n>] [--text]\n"
			"  --uuid    log UUID (32-char hex or xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)\n"
			"  --offset  byte offset into log (default 0)\n"
			"  --length  bytes to retrieve (default: full log_size)\n"
			"  --text    print log as ASCII text instead of hex dump\n");
		return -1;
	}

	return 0;
}

uint32_t lookup_log_size(const struct cxlmi_cmd_get_supported_logs_rsp *srsp,
			 const uint8_t uuid[16])
{
	uint16_t i;

	for (i = 0; i < srsp->num_supported_log_entries; i++) {
		if (memcmp(srsp->entries[i].uuid, uuid, 16) == 0)
			return srsp->entries[i].log_size;
	}
	return 0;
}

void print_log_header(const uint8_t uuid[16], uint32_t offset, uint32_t length)
{
	fprintf(stderr,
		"Log UUID: %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"
		"  (%s)\nOffset: %u  Length: %u\n",
		uuid[0],  uuid[1],  uuid[2],  uuid[3],
		uuid[4],  uuid[5],
		uuid[6],  uuid[7],
		uuid[8],  uuid[9],
		uuid[10], uuid[11], uuid[12],
		uuid[13], uuid[14], uuid[15],
		lookup_uuid_name(uuid), offset, length);
}

void print_log_payload(const uint8_t uuid[16], uint32_t offset, uint32_t length,
		       const uint8_t *buf, int has_text)
{
	uint32_t i;

	if (has_text) {
		fwrite(buf, 1, length, stdout);
	} else if (cel_uuid_match(uuid)) {
		print_cel_log(buf, length, offset);
	} else if (offset == 0 && ecs_uuid_match(uuid)) {
		print_ddr5_ecs_log(buf, length);
	} else if (offset == 0 && media_test_cap_uuid_match(uuid)) {
		print_media_test_capability_log(buf, length);
	} else if (offset == 0 && media_test_short_uuid_match(uuid)) {
		print_media_test_result_short_log(buf, length);
	} else if (offset == 0 && media_test_long_uuid_match(uuid)) {
		print_media_test_result_long_log(buf, length);
	} else {
		for (i = 0; i < length; i++) {
			if (i % 16 == 0)
				printf("%04x: ", offset + i);
			printf("%02x ", buf[i]);
			if ((i + 1) % 16 == 0 || i + 1 == length)
				printf("\n");
		}
	}
}

int cmd_get_log(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_get_log_req req = { 0 };
	struct get_log_params params;
	uint8_t *buf = NULL;
	int rc;

	rc = parse_get_log_req(argc - 1, argv + 1, &params);
	if (rc)
		return rc;

	if (!params.has_length) {
		struct cxlmi_cmd_get_supported_logs_rsp *srsp =
			fetch_supported_logs(ep);

		if (!srsp) {
			fprintf(stderr,
				"get-log: cannot determine log size (get-supported-logs failed)\n");
			return -1;
		}
		params.length = lookup_log_size(srsp, params.uuid);
		free(srsp);

		if (params.length == 0) {
			fprintf(stderr,
				"get-log: UUID not found in supported logs list\n");
			return -1;
		}
	}

	memcpy(req.uuid, params.uuid, 16);
	req.offset = params.offset;
	req.length = params.length;

	buf = calloc(1, params.length);
	if (!buf) {
		fprintf(stderr, "get-log: out of memory\n");
		return -1;
	}

	rc = cxlmi_cmd_get_log(ep, NULL, &req, buf);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get-log failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get-log ioctl failed\n");
		free(buf);
		return rc;
	}

	print_log_header(params.uuid, params.offset, params.length);
	print_log_payload(params.uuid, params.offset, params.length, buf,
			  params.has_text);

	free(buf);
	return 0;
}

int parse_log_uuid_req(int argc, char **argv, uint8_t uuid[16],
		       const char *usage)
{
	int i, has_uuid = 0;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--uuid") == 0 && i + 1 < argc) {
			if (parse_log_uuid(argv[++i], uuid) != 0)
				return -1;
			has_uuid = 1;
		} else {
			fprintf(stderr, "%s", usage);
			return -1;
		}
	}

	if (!has_uuid) {
		fprintf(stderr, "%s", usage);
		return -1;
	}

	return 0;
}

static void print_log_uuid_action(const char *action, const uint8_t uuid[16])
{
	printf("%s log ", action);
	print_log_uuid(uuid);
	printf("  (%s)\n", lookup_uuid_name(uuid));
}

int cmd_get_log_cap(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_get_log_capabilities_req req = { 0 };
	struct cxlmi_cmd_get_log_capabilities_rsp rsp = { 0 };
	int rc;

	rc = parse_log_uuid_req(argc - 1, argv + 1, req.uuid,
				"Usage: get-log-cap --uuid <uuid>\n"
				"  --uuid  log UUID (32-char hex or standard format)\n");
	if (rc)
		return rc;

	rc = cxlmi_cmd_get_log_capabilities(ep, NULL, &req, &rsp);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get-log-cap failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get-log-cap ioctl failed\n");
		return rc;
	}

	printf("Log capabilities for ");
	print_log_uuid(req.uuid);
	printf("  (%s)\n", lookup_uuid_name(req.uuid));
	printf("  parameter_flags: 0x%08x\n", rsp.parameter_flags);
	printf("  clear_log_supported: %s\n",
	       (rsp.parameter_flags & 0x01) ? "yes" : "no");
	printf("  populate_log_supported: %s\n",
	       (rsp.parameter_flags & 0x02) ? "yes" : "no");
	printf("  auto_populate_log_supported: %s\n",
	       (rsp.parameter_flags & 0x04) ? "yes" : "no");
	printf("  persistent_across_cold_reset: %s\n",
	       (rsp.parameter_flags & 0x08) ? "yes" : "no");

	return 0;
}

int cmd_clear_log(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_clear_log_req req = { 0 };
	int rc;

	rc = parse_log_uuid_req(argc - 1, argv + 1, req.uuid,
				"Usage: clear-log --uuid <uuid>\n"
				"  --uuid  log UUID (32-char hex or standard format)\n");
	if (rc)
		return rc;

	rc = cxlmi_cmd_clear_log(ep, NULL, &req);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "clear-log failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "clear-log ioctl failed\n");
		return rc;
	}

	print_log_uuid_action("Cleared", req.uuid);
	return 0;
}

int cmd_populate_log(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_populate_log_req req = { 0 };
	int rc;

	rc = parse_log_uuid_req(argc - 1, argv + 1, req.uuid,
				"Usage: populate-log --uuid <uuid>\n"
				"  --uuid  log UUID (32-char hex or standard format)\n");
	if (rc)
		return rc;

	rc = cxlmi_cmd_populate_log(ep, NULL, &req);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "populate-log failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "populate-log ioctl failed\n");
		return rc;
	}

	print_log_uuid_action("Populated", req.uuid);
	return 0;
}

int cmd_get_vendor_log(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_get_supported_logs_rsp *srsp;
	struct cxlmi_cmd_get_log_req req = { 0 };
	const char *outfile = NULL;
	uint32_t total_size = 0, offset = 0, chunk;
	uint8_t *buf = NULL;
	FILE *fp = NULL;
	uint16_t i;
	int rc = -1, a;

	for (a = 1; a < argc; a++) {
		if (strcmp(argv[a], "-f") == 0 && a + 1 < argc) {
			outfile = argv[++a];
		} else {
			fprintf(stderr,
				"Usage: get-vendor-log -f <output_file>\n");
			return -1;
		}
	}

	if (!outfile) {
		fprintf(stderr,
			"Usage: get-vendor-log -f <output_file>\n"
			"  -f <file>  file to append vendor debug log into\n");
		return -1;
	}

	/* Step 1: find Vendor Debug Log UUID and total size. */
	srsp = fetch_supported_logs(ep);
	if (!srsp) {
		fprintf(stderr, "get-vendor-log: get-supported-logs failed\n");
		return -1;
	}
	for (i = 0; i < srsp->num_supported_log_entries; i++) {
		if (memcmp(srsp->entries[i].uuid, vendor_debug_log_uuid, 16) == 0) {
			total_size = srsp->entries[i].log_size;
			break;
		}
	}
	free(srsp);

	if (total_size == 0) {
		fprintf(stderr,
			"get-vendor-log: Vendor Debug Log not found on this device\n");
		return -1;
	}

	/* Step 2: open output file in append mode. */
	fp = fopen(outfile, "a");
	if (!fp) {
		perror("get-vendor-log: fopen");
		return -1;
	}

	buf = malloc(VENDOR_LOG_CHUNK);
	if (!buf) {
		fprintf(stderr, "get-vendor-log: out of memory\n");
		fclose(fp);
		return -1;
	}

	fprintf(stderr,
		"get-vendor-log: Vendor Debug Log size=%u bytes, chunk=%d, file=%s\n",
		total_size, VENDOR_LOG_CHUNK, outfile);

	/* Step 3: fetch in 2K chunks and append to file. */
	memcpy(req.uuid, vendor_debug_log_uuid, 16);

	while (offset < total_size) {
		chunk = total_size - offset;
		if (chunk > VENDOR_LOG_CHUNK)
			chunk = VENDOR_LOG_CHUNK;

		req.offset = offset;
		req.length = chunk;

		rc = cxlmi_cmd_get_log(ep, NULL, &req, buf);
		if (rc) {
			if (rc > 0)
				fprintf(stderr,
					"get-vendor-log: get-log failed at offset %u: %s\n",
					offset, cxlmi_cmd_retcode_tostr(rc));
			else
				fprintf(stderr,
					"get-vendor-log: ioctl failed at offset %u\n",
					offset);
			free(buf);
			fclose(fp);
			return rc;
		}

		if (fwrite(buf, 1, chunk, fp) != chunk) {
			perror("get-vendor-log: fwrite");
			free(buf);
			fclose(fp);
			return -1;
		}

		offset += chunk;
	}

	free(buf);
	fclose(fp);
	fprintf(stderr, "get-vendor-log: done, %u bytes written to %s\n",
		total_size, outfile);
	return 0;
}
