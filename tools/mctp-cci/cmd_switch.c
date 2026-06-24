// SPDX-License-Identifier: LGPL-2.1-or-later
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static int do_identify_sw(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                          int argc, char **argv)
{
    struct cxlmi_cmd_fmapi_identify_sw_device_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_fmapi_identify_sw_device(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("identify-sw-device");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    printf("ingress_port_id: %u\n", rsp.ingress_port_id);
    printf("num_physical_ports: %u\n", rsp.num_physical_ports);
    printf("num_vcs: %u\n", rsp.num_vcs);
    printf("num_total_vppb: %u\n", rsp.num_total_vppb);
    printf("num_active_vppb: %u\n", rsp.num_active_vppb);
    printf("num_hdm_decoder_per_usp: %u\n", rsp.num_hdm_decoder_per_usp);
    return 0;
}

static int do_phys_port_state(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                              int argc, char **argv)
{
    struct cxlmi_cmd_fmapi_get_phys_port_state_rsp *rsp;
    size_t buf_sz;
    int rc;

    if (argc < 1) { fprintf(stderr, "usage: switch port-state <port_id>\n"); return 2; }

    /* Allocate space for flexible-array ports[] in the response */
    buf_sz = sizeof(*rsp) + 8 * sizeof(rsp->ports[0]);
    rsp = calloc(1, buf_sz);
    if (!rsp) { perror("calloc"); return 2; }

    /* Validate port_id */
    {
        unsigned long port_id;
        char *end;

        errno = 0;
        port_id = strtoul(argv[0], &end, 0);
        if (errno || *end != '\0' || port_id > 255) {
            fprintf(stderr, "invalid <port_id>: %s\n", argv[0]);
            return 2;
        }

        /* Build request with one port_id */
        uint8_t req_buf[sizeof(struct cxlmi_cmd_fmapi_get_phys_port_state_req) + 1] = {0};
        struct cxlmi_cmd_fmapi_get_phys_port_state_req *req_pl =
            (struct cxlmi_cmd_fmapi_get_phys_port_state_req *)req_buf;
        req_pl->num_ports = 1;
        req_pl->ports[0] = (uint8_t)port_id;

        rc = cxlmi_cmd_fmapi_get_phys_port_state(ep, ti, req_pl, rsp);
    }
    if (rc < 0) { free(rsp); return mctp_cci_report_libcxlmi_error("get-phys-port-state"); }
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); free(rsp); return 1; }

    printf("num_ports: %u\n", rsp->num_ports);
    free(rsp);
    return 0;
}

static int do_vcs_info(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                       int argc, char **argv)
{
    struct cxlmi_cmd_fmapi_get_vcs_info_rsp *rsp;
    size_t buf_sz;
    int rc;
    (void)argc; (void)argv;

    /* Build request with vppb limit and num_vcs matching the response allocation */
    {
        uint8_t req_buf[sizeof(struct cxlmi_cmd_fmapi_get_vcs_info_req) + 16] = {0};
        struct cxlmi_cmd_fmapi_get_vcs_info_req *req_pl =
            (struct cxlmi_cmd_fmapi_get_vcs_info_req *)req_buf;

        req_pl->start_vppb = 0;
        req_pl->vppb_list_limit = 32;
        req_pl->num_vcs = 16;
        for (int i = 0; i < 16; i++)
            req_pl->vcs_id_list[i] = (uint8_t)i;

        buf_sz = sizeof(*rsp) + 16 * (sizeof(struct cxlmi_cmd_fmapi_vcs_info_block) +
                                      32 * sizeof(struct cxlmi_cmd_fmapi_vppb_info));
        rsp = calloc(1, buf_sz);
        if (!rsp) { perror("calloc"); return 2; }

        rc = cxlmi_cmd_fmapi_get_vcs_info(ep, ti, req_pl, rsp);
    }

    if (rc < 0) { free(rsp); return mctp_cci_report_libcxlmi_error("get-vcs-info"); }
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); free(rsp); return 1; }

    printf("num_vcs: %u\n", rsp->num_vcs);
    free(rsp);
    return 0;
}

static const struct mctp_cci_cmd switch_cmds[] = {
    { "identify",    "Identify Switch Device (5100h)",           do_identify_sw },
    { "port-state",  "Get Physical Port State (5101h) <port>",   do_phys_port_state },
    { "vcs-info",    "Get VCS Info (5200h)",                     do_vcs_info },
};

const struct mctp_cci_top switch_top = {
    .name = "switch",
    .help = "PHYSICAL_SWITCH (0x51) + VIRTUAL_SWITCH (0x52)",
    .cmds = switch_cmds,
    .n_cmds = sizeof(switch_cmds) / sizeof(switch_cmds[0]),
};
