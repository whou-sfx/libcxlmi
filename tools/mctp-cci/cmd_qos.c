// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static int do_get_ctrl(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                       int argc, char **argv)
{
    struct cxlmi_cmd_memdev_get_sld_qos_control_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_memdev_get_sld_qos_control(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-sld-qos-control");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    printf("qos_telemetry_control:        0x%x\n", rsp.qos_telemetry_control);
    printf("egress_moderate_percentage:   0x%x\n", rsp.egress_moderate_percentage);
    printf("egress_severe_percentage:     0x%x\n", rsp.egress_severe_percentage);
    printf("backpressure_sample_interval: 0x%x\n", rsp.backpressure_sample_interval);
    return 0;
}

static int do_set_ctrl(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                       int argc, char **argv)
{
    struct cxlmi_cmd_memdev_set_sld_qos_control_req req = {0};
    int rc;

    if (argc < 1) { fprintf(stderr, "usage: qos set-ctrl <control>\n"); return 2; }
    req.qos_telemetry_control = strtoul(argv[0], NULL, 0);
    rc = cxlmi_cmd_memdev_set_sld_qos_control(ep, ti, &req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("set-sld-qos-control");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static int do_get_status(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                         int argc, char **argv)
{
    struct cxlmi_cmd_memdev_get_sld_qos_status_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_memdev_get_sld_qos_status(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-sld-qos-status");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    printf("backpressure_avg_percentage: 0x%x\n", rsp.backpressure_avg_percentage);
    return 0;
}

static const struct mctp_cci_cmd qos_cmds[] = {
    { "get-ctrl",   "Get SLD QoS Control (4700h)",   do_get_ctrl },
    { "set-ctrl",   "Set SLD QoS Control (4701h) <qos_telemetry_control>", do_set_ctrl },
    { "get-status", "Get SLD QoS Status (4702h)",    do_get_status },
};

const struct mctp_cci_top qos_top = {
    .name = "qos",
    .help = "SLD_QOS_TELEMETRY (0x47): get/set control, get status",
    .cmds = qos_cmds,
    .n_cmds = sizeof(qos_cmds) / sizeof(qos_cmds[0]),
};
