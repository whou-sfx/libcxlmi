// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static int do_get_dc_config(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                            int argc, char **argv)
{
    struct cxlmi_cmd_memdev_get_dc_config_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_memdev_get_dc_config(ep, ti, NULL, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-dc-config");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }

    printf("num_regions:           %u\n", rsp.num_regions);
    printf("regions_returned:      %u\n", rsp.regions_returned);
    printf("num_extents_supported: %u\n", rsp.num_extents_supported);
    printf("num_extents_available: %u\n", rsp.num_extents_available);
    printf("num_tags_supported:    %u\n", rsp.num_tags_supported);
    printf("num_tags_available:    %u\n", rsp.num_tags_available);
    return 0;
}

static const struct mctp_cci_cmd dcd_cmds[] = {
    { "config", "Get DC Config (4800h)", do_get_dc_config },
};

const struct mctp_cci_top dcd_top = {
    .name = "dcd",
    .help = "DCD_CONFIG (0x48): config",
    .cmds = dcd_cmds,
    .n_cmds = sizeof(dcd_cmds) / sizeof(dcd_cmds[0]),
};
