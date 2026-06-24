// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static const struct {
	const uint8_t uuid[16];
	const char *name;
} known_log_uuids[] = {
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

static const char *lookup_log_name(const uint8_t uuid[16])
{
	size_t i;

	for (i = 0; i < sizeof(known_log_uuids) / sizeof(known_log_uuids[0]); i++) {
		if (!memcmp(known_log_uuids[i].uuid, uuid, 16))
			return known_log_uuids[i].name;
	}
	return "unknown";
}

static uint32_t lookup_log_size(const struct cxlmi_cmd_get_supported_logs_rsp *srsp,
				const uint8_t uuid[16])
{
	uint16_t i;

	for (i = 0; i < srsp->num_supported_log_entries; i++) {
		if (!memcmp(srsp->entries[i].uuid, uuid, 16))
			return srsp->entries[i].log_size;
	}
	return 0;
}

static struct cxlmi_cmd_get_supported_logs_rsp *fetch_supported_logs(
	struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti)
{
	size_t sz = sizeof(struct cxlmi_cmd_get_supported_logs_rsp) +
		    CXLMI_MAX_SUPPORTED_LOGS *
		    sizeof(struct cxlmi_supported_log_entry);
	struct cxlmi_cmd_get_supported_logs_rsp *rsp = calloc(1, sz);

	if (!rsp)
		return NULL;

	if (cxlmi_cmd_get_supported_logs(ep, ti, rsp)) {
		free(rsp);
		return NULL;
	}
	return rsp;
}

static int do_get_supported(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                            int argc, char **argv)
{
    struct cxlmi_cmd_get_supported_logs_rsp *rsp;
    size_t sz = sizeof(*rsp) +
                CXLMI_MAX_SUPPORTED_LOGS * sizeof(*rsp->entries);
    unsigned int i;
    int rc;
    (void)argc; (void)argv;

    rsp = calloc(1, sz);
    if (!rsp) { perror("calloc"); return -1; }

    rc = cxlmi_cmd_get_supported_logs(ep, ti, rsp);
    if (rc < 0) { free(rsp); return mctp_cci_report_libcxlmi_error("get-supported-logs"); }
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); free(rsp); return 1; }

    printf("Supported log entries: %u\n", rsp->num_supported_log_entries);
    for (i = 0; i < rsp->num_supported_log_entries; i++) {
        printf("  [%u] ", i);
        print_uuid(rsp->entries[i].uuid);
        printf("  log_size: %7u bytes  (%s)\n",
               rsp->entries[i].log_size,
               lookup_log_name(rsp->entries[i].uuid));
    }
    free(rsp);
    return 0;
}

static int do_get_log(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                      int argc, char **argv)
{
    struct cxlmi_cmd_get_log_req req = {0};
    struct cxlmi_cmd_get_supported_logs_rsp *srsp = NULL;
    uint8_t *buf = NULL;
    int has_uuid = 0;
    int force_hex = 0;
    int i, rc;

    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--uuid") && i + 1 < argc) {
            if (parse_log_uuid(argv[++i], req.uuid) < 0) {
                fprintf(stderr, "bad uuid\n"); return 2;
            }
            has_uuid = 1;
        } else if (!strcmp(argv[i], "--offset") && i + 1 < argc) {
            req.offset = strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--length") && i + 1 < argc) {
            req.length = strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--hex")) {
            force_hex = 1;
        }
    }

    if (!has_uuid) {
        fprintf(stderr, "usage: logs get --uuid <hex> [--offset N] [--length N] [--hex]\n");
        return 2;
    }

    if (!req.length) {
        srsp = fetch_supported_logs(ep, ti);
        if (!srsp) {
            fprintf(stderr, "get-log: cannot determine log size (get-supported-logs failed)\n");
            return -1;
        }
        req.length = lookup_log_size(srsp, req.uuid);
        if (!req.length) {
            unsigned int j;
            int found = 0;

            for (j = 0; j < srsp->num_supported_log_entries; j++) {
                if (!memcmp(srsp->entries[j].uuid, req.uuid, 16)) {
                    found = 1;
                    break;
                }
            }
            fprintf(stderr, "get-log: %s\n",
                    found ? "log_size is 0 for this UUID"
                          : "UUID not found in supported logs list");
            free(srsp);
            return 1;
        }
    }

    buf = calloc(1, req.length);
    if (!buf) {
        perror("calloc");
        free(srsp);
        return -1;
    }

    fprintf(stderr, "Log UUID: ");
    print_uuid(req.uuid);
    fprintf(stderr, "  (%s)\nOffset: %u  Length: %u\n",
            lookup_log_name(req.uuid), req.offset, req.length);

    rc = cxlmi_cmd_get_log(ep, ti, &req, buf);
    if (rc < 0) { free(buf); free(srsp); return mctp_cci_report_libcxlmi_error("get-log"); }
    if (rc > 0) {
        fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc));
        free(buf);
        free(srsp);
        return 1;
    }

    if (!force_hex && cel_uuid_match(req.uuid))
        print_cel_log(buf, req.length, req.offset);
    else
        print_log_payload(buf, req.length, 0, 1);

    free(buf);
    free(srsp);
    return 0;
}

static int do_clear_log(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                        int argc, char **argv)
{
    struct cxlmi_cmd_clear_log_req req = {0};
    int rc;

    if (argc < 1 || parse_log_uuid(argv[0], req.uuid) < 0) {
        fprintf(stderr, "usage: logs clear <uuid>\n"); return 2;
    }
    rc = cxlmi_cmd_clear_log(ep, ti, &req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("clear-log");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static const struct mctp_cci_cmd logs_cmds[] = {
    { "supported", "Get Supported Logs (0400h)",                  do_get_supported },
    { "get",       "Get Log (0401h) --uuid <hex> [--offset N] [--length N] [--hex]", do_get_log },
    { "clear",     "Clear Log (0402h) <uuid>",                     do_clear_log },
};

const struct mctp_cci_top logs_top = {
    .name = "logs",
    .help = "LOGS (0x04): supported, get, clear",
    .cmds = logs_cmds,
    .n_cmds = sizeof(logs_cmds) / sizeof(logs_cmds[0]),
};
