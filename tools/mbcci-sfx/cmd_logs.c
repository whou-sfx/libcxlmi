// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Get Supported Logs (0400h) and Get Log (0401h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

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

/* Print UUID in standard 8-4-4-4-12 format. */
static void print_uuid(const uint8_t *uuid)
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

int cmd_get_supported_logs(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_get_supported_logs_rsp *rsp;
	uint16_t n, i;

	(void)argc;
	(void)argv;

	rsp = fetch_supported_logs(ep);
	if (!rsp) {
		fprintf(stderr, "get-supported-logs failed\n");
		return -1;
	}

	n = rsp->num_supported_log_entries;
	printf("Supported log entries: %u\n", n);
	for (i = 0; i < n; i++) {
		printf("  [%u] UUID: ", i);
		print_uuid(rsp->entries[i].uuid);
		printf("  log_size: %7u bytes  (%s)\n",
		       rsp->entries[i].log_size,
		       lookup_uuid_name(rsp->entries[i].uuid));
	}

	free(rsp);
	return 0;
}

/*
 * Parse a UUID string into 16 bytes.
 * Accepts both:
 *   - 32-char hex without dashes:  0da9c0b5bf414b788f7996b1623b3f17
 *   - 36-char standard format:     0da9c0b5-bf41-4b78-8f79-96b1623b3f17
 */
static int parse_uuid(const char *str, uint8_t *out)
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

int cmd_get_log(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_get_log_req req = { 0 };
	uint8_t uuid_bytes[16] = { 0 };
	int has_uuid = 0, has_length = 0, has_text = 0;
	uint32_t offset = 0, length = 0;
	uint8_t *buf = NULL;
	int rc = -1, a;

	for (a = 1; a < argc; a++) {
		if (strcmp(argv[a], "--uuid") == 0 && a + 1 < argc) {
			if (parse_uuid(argv[++a], uuid_bytes) != 0)
				return -1;
			has_uuid = 1;
		} else if (strcmp(argv[a], "--offset") == 0 && a + 1 < argc) {
			offset = (uint32_t)strtoul(argv[++a], NULL, 0);
		} else if (strcmp(argv[a], "--length") == 0 && a + 1 < argc) {
			length = (uint32_t)strtoul(argv[++a], NULL, 0);
			has_length = 1;
		} else if (strcmp(argv[a], "--text") == 0) {
			has_text = 1;
		} else {
			fprintf(stderr,
				"Usage: get-log --uuid <uuid> [--offset <n>] [--length <n>] [--text]\n");
			return -1;
		}
	}

	if (!has_uuid) {
		fprintf(stderr,
			"Usage: get-log --uuid <uuid> [--offset <n>] [--length <n>] [--text]\n"
			"  --uuid    log UUID (32-char hex or xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)\n"
			"  --offset  byte offset into log (default 0)\n"
			"  --length  bytes to retrieve (default: full log_size)\n"
			"  --text    print log as ASCII text instead of hex dump\n");
		return -1;
	}

	/* If length not specified, look it up from get-supported-logs. */
	if (!has_length) {
		struct cxlmi_cmd_get_supported_logs_rsp *srsp =
			fetch_supported_logs(ep);
		uint16_t i;

		if (!srsp) {
			fprintf(stderr,
				"get-log: cannot determine log size (get-supported-logs failed)\n");
			return -1;
		}
		for (i = 0; i < srsp->num_supported_log_entries; i++) {
			if (memcmp(srsp->entries[i].uuid, uuid_bytes, 16) == 0) {
				length = srsp->entries[i].log_size;
				break;
			}
		}
		free(srsp);

		if (length == 0) {
			fprintf(stderr,
				"get-log: UUID not found in supported logs list\n");
			return -1;
		}
	}

	memcpy(req.uuid, uuid_bytes, 16);
	req.offset = offset;
	req.length = length;

	buf = calloc(1, length);
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

	fprintf(stderr, "Log UUID: %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"
		"  (%s)\nOffset: %u  Length: %u\n",
		uuid_bytes[0],  uuid_bytes[1],  uuid_bytes[2],  uuid_bytes[3],
		uuid_bytes[4],  uuid_bytes[5],
		uuid_bytes[6],  uuid_bytes[7],
		uuid_bytes[8],  uuid_bytes[9],
		uuid_bytes[10], uuid_bytes[11], uuid_bytes[12],
		uuid_bytes[13], uuid_bytes[14], uuid_bytes[15],
		lookup_uuid_name(uuid_bytes), offset, length);

	if (has_text) {
		fwrite(buf, 1, length, stdout);
	} else {
		for (uint32_t i = 0; i < length; i++) {
			if (i % 16 == 0)
				printf("%04x: ", offset + i);
			printf("%02x ", buf[i]);
			if ((i + 1) % 16 == 0 || i + 1 == length)
				printf("\n");
		}
	}

	free(buf);
	return 0;
}
