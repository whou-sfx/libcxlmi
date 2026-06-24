// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static int log_from_name(const char *s)
{
    if (!strcmp(s, "info"))    return 0;
    if (!strcmp(s, "warn"))    return 1;
    if (!strcmp(s, "failure")) return 2;
    if (!strcmp(s, "fatal"))   return 3;
    if (!strcmp(s, "dcd"))     return 4;
    return -1;
}

static int do_get_events(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                         int argc, char **argv)
{
    struct cxlmi_cmd_get_event_records_req req = {0};
    struct cxlmi_cmd_get_event_records_rsp rsp;
    int rc;

    if (argc < 1 || log_from_name(argv[0]) < 0) {
        fprintf(stderr, "usage: events get <info|warn|failure|fatal|dcd>\n");
        return 2;
    }
    req.event_log = log_from_name(argv[0]);
    rc = cxlmi_cmd_get_event_records(ep, ti, &req, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-event-records");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    printf("records: %u\n", rsp.record_count);
    return 0;
}

static int do_clear_events(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                           int argc, char **argv)
{
    struct cxlmi_cmd_clear_event_records_req req = {0};
    int rc;

    if (argc < 1 || log_from_name(argv[0]) < 0) {
        fprintf(stderr, "usage: events clear <log> [--all | --handle <h>...]\n");
        return 2;
    }
    req.event_log = log_from_name(argv[0]);
    req.clear_flags = 1;
    rc = cxlmi_cmd_clear_event_records(ep, ti, &req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("clear-event-records");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static int do_get_intr_policy(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                              int argc, char **argv)
{
    struct cxlmi_cmd_get_event_interrupt_policy_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_get_event_interrupt_policy(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-event-interrupt-policy");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    printf("informational: 0x%x\n", rsp.informational_settings);
    printf("warning:       0x%x\n", rsp.warning_settings);
    printf("failure:       0x%x\n", rsp.failure_settings);
    printf("fatal:         0x%x\n", rsp.fatal_settings);
    return 0;
}

static int do_set_intr_policy(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                              int argc, char **argv)
{
    struct cxlmi_cmd_set_event_interrupt_policy_req req = {0};
    int i, rc;

    for (i = 0; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--info"))         req.informational_settings = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--warn"))    req.warning_settings       = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--failure")) req.failure_settings       = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--fatal"))   req.fatal_settings         = strtoul(argv[++i], NULL, 0);
    }
    rc = cxlmi_cmd_set_event_interrupt_policy(ep, ti, &req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("set-event-interrupt-policy");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static const struct mctp_cci_cmd events_cmds[] = {
    { "get",        "Get Event Records (0100h) <log>",                   do_get_events },
    { "clear",      "Clear Event Records (0101h) <log> [--all]",         do_clear_events },
    { "get-policy", "Get Event Interrupt Policy (0102h)",                do_get_intr_policy },
    { "set-policy", "Set Event Interrupt Policy (0103h) [--info N] ...", do_set_intr_policy },
};

const struct mctp_cci_top events_top = {
    .name = "events",
    .help = "EVENTS (0x01): get/clear records, interrupt policy",
    .cmds = events_cmds,
    .n_cmds = sizeof(events_cmds) / sizeof(events_cmds[0]),
};