// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <time.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static int do_get_ts(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                     int argc, char **argv)
{
    struct cxlmi_cmd_get_timestamp_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_get_timestamp(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-timestamp");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    printf("timestamp: %llu\n", (unsigned long long)rsp.timestamp);
    return 0;
}

static int do_set_ts(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                     int argc, char **argv)
{
    struct cxlmi_cmd_set_timestamp_req req = {0};
    int rc;

    if (argc < 1) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        req.timestamp = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    } else {
        uint64_t v;
        if (parse_hex_u64(argv[0], &v) < 0) {
            fprintf(stderr, "bad timestamp value\n");
            return 2;
        }
        req.timestamp = v;
    }
    rc = cxlmi_cmd_set_timestamp(ep, ti, &req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("set-timestamp");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static const struct mctp_cci_cmd ts_cmds[] = {
    { "get", "Get Timestamp (0300h)", do_get_ts },
    { "set", "Set Timestamp (0301h) [<ns>]", do_set_ts },
};

const struct mctp_cci_top ts_top = {
    .name = "ts",
    .help = "TIMESTAMP (0x03): get, set",
    .cmds = ts_cmds,
    .n_cmds = sizeof(ts_cmds) / sizeof(ts_cmds[0]),
};
