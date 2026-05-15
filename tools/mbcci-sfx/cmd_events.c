// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Generic Component Event commands (Opcodes 0100h–0103h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

/*
 * CXL spec does not mandate a maximum record count per response; 64 is a
 * generous upper bound consistent with test usage in tests/cxl-test-generic.c.
 */
#define MAX_EVENT_RECORDS 64

/* rsp->flags bit definitions (CXL r3.1 Section 8.2.9.2.2) */
#define RSP_FLAG_OVERFLOW    (1 << 0)   /* event overflow occurred */
#define RSP_FLAG_MORE_EVENTS (1 << 1)   /* more records available, re-issue */

static const struct {
	const char *name;
	uint8_t value;
} event_log_map[] = {
	{ "info",    0 },
	{ "warn",    1 },
	{ "failure", 2 },
	{ "fatal",   3 },
	{ "dcd",     4 },
};

static int parse_event_log(const char *name, uint8_t *out)
{
	size_t i;

	for (i = 0; i < sizeof(event_log_map) / sizeof(event_log_map[0]); i++) {
		if (strcmp(name, event_log_map[i].name) == 0) {
			*out = event_log_map[i].value;
			return 0;
		}
	}
	fprintf(stderr, "Unknown event log '%s'. Valid: info warn failure fatal dcd\n",
		name);
	return -1;
}

/* ---- 0100h: Get Event Records ---- */

int cmd_get_event_records(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_get_event_records_req req = { 0 };
	struct cxlmi_cmd_get_event_records_rsp *rsp;
	const char *log_name = NULL;
	size_t rsp_sz;
	uint32_t round = 0;
	int rc = 0;

	for (int a = 1; a < argc; a++) {
		if (strcmp(argv[a], "--log") == 0 && a + 1 < argc)
			log_name = argv[++a];
		else {
			fprintf(stderr, "Usage: get-event-records --log <info|warn|failure|fatal|dcd>\n");
			return -1;
		}
	}

	if (!log_name) {
		fprintf(stderr, "Usage: get-event-records --log <info|warn|failure|fatal|dcd>\n");
		return -1;
	}

	if (parse_event_log(log_name, &req.event_log) != 0)
		return -1;

	rsp_sz = sizeof(*rsp) + MAX_EVENT_RECORDS * sizeof(rsp->records[0]);
	rsp = calloc(1, rsp_sz);
	if (!rsp) {
		fprintf(stderr, "out of memory\n");
		return -1;
	}

	printf("Log: %s (%u)\n", log_name, req.event_log);

	do {
		uint16_t i;
		uint16_t count;

		memset(rsp, 0, rsp_sz);
		rc = cxlmi_cmd_get_event_records(ep, NULL, &req, rsp);
		if (rc) {
			if (rc > 0)
				fprintf(stderr, "get-event-records failed: %s\n",
					cxlmi_cmd_retcode_tostr(rc));
			else
				fprintf(stderr, "get-event-records ioctl failed\n");
			break;
		}

		printf("\n--- Round %u ---\n", round + 1);
		printf("Flags:                 0x%02x [%s%s]\n",
		       rsp->flags,
		       (rsp->flags & RSP_FLAG_OVERFLOW)    ? "OVERFLOW "   : "",
		       (rsp->flags & RSP_FLAG_MORE_EVENTS) ? "MORE_EVENTS" : "");
		printf("Overflow Error Count:  %u\n", rsp->overflow_err_count);
		printf("First Overflow TS:     %llu\n",
		       (unsigned long long)rsp->first_overflow_timestamp);
		printf("Last Overflow TS:      %llu\n",
		       (unsigned long long)rsp->last_overflow_timestamp);

		count = rsp->record_count;
		if (count > MAX_EVENT_RECORDS) {
			fprintf(stderr,
				"warning: record_count %u exceeds buffer limit %u, clamping\n",
				count, MAX_EVENT_RECORDS);
			count = MAX_EVENT_RECORDS;
		}
		printf("Record Count:          %u\n", count);

		for (i = 0; i < count; i++) {
			const struct cxlmi_event_record *r = &rsp->records[i];
			int j;

			printf("\n  [Record %u]\n", i);
			printf("    UUID:           ");
			for (j = 0; j < 16; j++)
				printf("%02x", r->uuid[j]);
			printf("\n");
			printf("    Handle:         0x%04x\n", r->handle);
			printf("    Related Handle: 0x%04x\n", r->related_handle);
			printf("    Timestamp:      %llu\n",
			       (unsigned long long)r->timestamp);
			printf("    Flags:          0x%02x 0x%02x 0x%02x\n",
			       r->flags[0], r->flags[1], r->flags[2]);
			printf("    Length:         %u\n", r->length);
			printf("    MaintOpClass:   0x%02x  SubClass: 0x%02x\n",
			       r->maint_op_class, r->maint_op_subclass);
			printf("    LD ID:          %u  Head ID: %u\n",
			       r->ld_id, r->head_id);
			printf("    Data:           ");
			for (j = 0; j < 0x50; j++) {
				printf("%02x", r->data[j]);
				if ((j + 1) % 16 == 0 && j + 1 < 0x50)
					printf("\n                    ");
			}
			printf("\n");
		}

		round++;
	} while (rsp->flags & RSP_FLAG_MORE_EVENTS);

	free(rsp);
	return rc;
}

/* ---- 0101h: Clear Event Records ---- */

int cmd_clear_event_records(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_clear_event_records_req *req;
	const char *log_name = NULL;
	uint16_t handles[MAX_EVENT_RECORDS];
	uint8_t nr_recs = 0;
	uint8_t clear_all = 0;
	size_t req_sz;
	int rc;

	for (int a = 1; a < argc; a++) {
		if (strcmp(argv[a], "--log") == 0 && a + 1 < argc) {
			log_name = argv[++a];
		} else if (strcmp(argv[a], "--all") == 0) {
			clear_all = 1;
		} else if (strcmp(argv[a], "--handle") == 0 && a + 1 < argc) {
			if (nr_recs >= MAX_EVENT_RECORDS) {
				fprintf(stderr, "too many --handle values (max %d)\n",
					MAX_EVENT_RECORDS);
				return -1;
			}
			handles[nr_recs++] = (uint16_t)strtoul(argv[++a], NULL, 0);
		} else {
			fprintf(stderr,
				"Usage: clear-event-records --log <log> [--all] [--handle <h>...]\n");
			return -1;
		}
	}

	if (!log_name) {
		fprintf(stderr,
			"Usage: clear-event-records --log <info|warn|failure|fatal|dcd> [--all] [--handle <h>...]\n");
		return -1;
	}

	req_sz = sizeof(*req) + nr_recs * sizeof(uint16_t);
	req = calloc(1, req_sz);
	if (!req) {
		fprintf(stderr, "out of memory\n");
		return -1;
	}

	if (parse_event_log(log_name, &req->event_log) != 0) {
		free(req);
		return -1;
	}

	if (clear_all) {
		req->clear_flags = 0x1;
		req->nr_recs = 0;
	} else {
		req->clear_flags = 0;
		req->nr_recs = nr_recs;
		memcpy(req->handles, handles, nr_recs * sizeof(uint16_t));
	}

	rc = cxlmi_cmd_clear_event_records(ep, NULL, req);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "clear-event-records failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "clear-event-records ioctl failed\n");
	} else {
		printf("Event records cleared (log=%s%s)\n",
		       log_name, clear_all ? ", all" : "");
	}

	free(req);
	return rc;
}

/* ---- 0102h: Get Event Interrupt Policy ---- */

int cmd_get_event_interrupt_policy(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_get_event_interrupt_policy_rsp rsp = { 0 };
	int rc;

	(void)argc;
	(void)argv;

	rc = cxlmi_cmd_get_event_interrupt_policy(ep, NULL, &rsp);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get-event-interrupt-policy failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get-event-interrupt-policy ioctl failed\n");
		return rc;
	}

	printf("Informational Settings: 0x%02x\n", rsp.informational_settings);
	printf("Warning Settings:       0x%02x\n", rsp.warning_settings);
	printf("Failure Settings:       0x%02x\n", rsp.failure_settings);
	printf("Fatal Settings:         0x%02x\n", rsp.fatal_settings);
#ifndef SUPPORT_CXL_2_0
	printf("DCD Settings:           0x%02x\n", rsp.dcd_settings);
#endif
	return 0;
}

/* ---- 0103h: Set Event Interrupt Policy ---- */

int cmd_set_event_interrupt_policy(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_set_event_interrupt_policy_req req = { 0 };
	int has_info = 0, has_warn = 0, has_failure = 0, has_fatal = 0;
	int rc;

	for (int a = 1; a < argc; a++) {
		if (strcmp(argv[a], "--info") == 0 && a + 1 < argc) {
			req.informational_settings = (uint8_t)strtoul(argv[++a], NULL, 0);
			has_info = 1;
		} else if (strcmp(argv[a], "--warn") == 0 && a + 1 < argc) {
			req.warning_settings = (uint8_t)strtoul(argv[++a], NULL, 0);
			has_warn = 1;
		} else if (strcmp(argv[a], "--failure") == 0 && a + 1 < argc) {
			req.failure_settings = (uint8_t)strtoul(argv[++a], NULL, 0);
			has_failure = 1;
		} else if (strcmp(argv[a], "--fatal") == 0 && a + 1 < argc) {
			req.fatal_settings = (uint8_t)strtoul(argv[++a], NULL, 0);
			has_fatal = 1;
#ifndef SUPPORT_CXL_2_0
		} else if (strcmp(argv[a], "--dcd") == 0 && a + 1 < argc) {
			req.dcd_settings = (uint8_t)strtoul(argv[++a], NULL, 0);
#endif
		} else {
			fprintf(stderr,
				"Usage: set-event-interrupt-policy --info <hex> --warn <hex>"
				" --failure <hex> --fatal <hex> [--dcd <hex>]\n");
			return -1;
		}
	}

	if (!has_info || !has_warn || !has_failure || !has_fatal) {
		fprintf(stderr,
			"Usage: set-event-interrupt-policy --info <hex> --warn <hex>"
			" --failure <hex> --fatal <hex> [--dcd <hex>]\n");
		return -1;
	}

	rc = cxlmi_cmd_set_event_interrupt_policy(ep, NULL, &req);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "set-event-interrupt-policy failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "set-event-interrupt-policy ioctl failed\n");
	} else {
		printf("Event interrupt policy set\n");
	}

	return rc;
}
