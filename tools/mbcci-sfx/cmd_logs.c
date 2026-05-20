// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Get Supported Logs (0400h) and Get Log (0401h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

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
		printf("  log_size: %u bytes\n", rsp->entries[i].log_size);
	}

	free(rsp);
	return 0;
}

/* Parse a 32-char hex string into 16 bytes.  Returns 0 on success. */
static int parse_uuid(const char *hex, uint8_t *out)
{
	int i;

	if (strlen(hex) != 32) {
		fprintf(stderr,
			"get-log: --uuid must be exactly 32 hex characters\n");
		return -1;
	}
	for (i = 0; i < 16; i++) {
		unsigned int byte;

		if (sscanf(hex + 2 * i, "%02x", &byte) != 1) {
			fprintf(stderr,
				"get-log: invalid hex in --uuid at position %d\n",
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
	int has_uuid = 0, has_length = 0;
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
		} else {
			fprintf(stderr,
				"Usage: get-log --uuid <hex32> [--offset <n>] [--length <n>]\n");
			return -1;
		}
	}

	if (!has_uuid) {
		fprintf(stderr,
			"Usage: get-log --uuid <hex32> [--offset <n>] [--length <n>]\n"
			"  --uuid    16-byte log UUID as 32 hex chars (no dashes)\n"
			"  --offset  byte offset into log (default 0)\n"
			"  --length  bytes to retrieve (default: full log_size)\n");
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

	printf("Log UUID: ");
	print_uuid(uuid_bytes);
	printf("\nOffset: %u  Length: %u\n", offset, length);

	for (uint32_t i = 0; i < length; i++) {
		if (i % 16 == 0)
			printf("%04x: ", offset + i);
		printf("%02x ", buf[i]);
		if ((i + 1) % 16 == 0 || i + 1 == length)
			printf("\n");
	}

	free(buf);
	return 0;
}
