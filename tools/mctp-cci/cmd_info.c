// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static const char *component_type_name(uint8_t type)
{
    switch (type) {
    case 0x00: return "CXL Switch";
    case 0x03: return "CXL Type3 Device";
    default:   return "Unknown";
    }
}

static void print_component_identify(const struct cxlmi_cmd_identify_rsp *rsp)
{
    char sn[sizeof(rsp->serial_num) + 1];
    size_t i;

    memcpy(sn, &rsp->serial_num, sizeof(rsp->serial_num));
    sn[sizeof(rsp->serial_num)] = '\0';
    for (i = 0; i < sizeof(rsp->serial_num); i++) {
        if ((unsigned char)sn[i] < 0x20 || (unsigned char)sn[i] > 0x7e)
            sn[i] = '.';
    }

    printf("Vendor ID             : 0x%04x\n", rsp->vendor_id);
    printf("Device ID             : 0x%04x\n", rsp->device_id);
    printf("Subsys Vendor ID      : 0x%04x\n", rsp->subsys_vendor_id);
    printf("Subsys ID             : 0x%04x\n", rsp->subsys_id);
    printf("Serial Number         : %s (0x%016" PRIx64 ")\n",
           sn, (uint64_t)rsp->serial_num);
    printf("Max Msg Size          : %u (2^%u = %u bytes)\n",
           rsp->max_msg_size, rsp->max_msg_size, 1u << rsp->max_msg_size);
    printf("Component Type        : 0x%02x (%s)\n",
           rsp->component_type, component_type_name(rsp->component_type));
}

static int do_identify(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                       int argc, char **argv)
{
    struct cxlmi_cmd_identify_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_identify(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("identify");
    if (rc > 0) { fprintf(stderr, "identify: %s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }

    print_component_identify(&rsp);
    return 0;
}

static int do_bg_op_status(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                           int argc, char **argv)
{
    struct cxlmi_cmd_bg_op_status_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_bg_op_status(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("bg-op-status");
    if (rc > 0) { fprintf(stderr, "bg-op-status: %s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    printf("Status: 0x%x\n", rsp.status);
    return 0;
}

static int do_get_resp_limit(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                             int argc, char **argv)
{
    struct cxlmi_cmd_get_response_msg_limit_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_get_response_msg_limit(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-response-msg-limit");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    printf("limit: %u\n", rsp.limit);
    return 0;
}

static int do_abort(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                    int argc, char **argv)
{
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_request_bg_op_abort(ep, ti);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("abort");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static const struct mctp_cci_cmd info_cmds[] = {
    { "identify",            "Component Identify (0001h)", do_identify },
    { "bg-op-status",        "Background Op Status (0002h)", do_bg_op_status },
    { "get-resp-msg-limit",  "Get Response Msg Limit (0003h)", do_get_resp_limit },
    { "abort",               "Request Abort Background Op (0004h)", do_abort },
};

const struct mctp_cci_top info_top = {
    .name = "info",
    .help = "INFOSTAT (0x00): identify, bg-op-status, response-msg-limit, abort",
    .cmds = info_cmds,
    .n_cmds = sizeof(info_cmds) / sizeof(info_cmds[0]),
};
