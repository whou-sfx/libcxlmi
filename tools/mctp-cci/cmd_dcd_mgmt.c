// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <string.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static int do_get_dcd_info(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                           int argc, char **argv)
{
    struct cxlmi_cmd_fmapi_get_dcd_info_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_fmapi_get_dcd_info(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-dcd-info");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }

    printf("num_hosts:                       %u\n", rsp.num_hosts);
    printf("num_supported_dc_regions:        %u\n", rsp.num_supported_dc_regions);
    printf("capacity_selection_policies:     0x%04x\n", rsp.capacity_selection_policies);
    printf("capacity_removal_policies:       0x%04x\n", rsp.capacity_removal_policies);
    printf("sanitize_on_release_config_mask: 0x%02x\n", rsp.sanitize_on_release_config_mask);
    printf("total_dynamic_capacity:          %llu (multiple of 256MB)\n",
           (unsigned long long)rsp.total_dynamic_capacity);
    {
        uint64_t masks[8];
        unsigned int r;

        memcpy(masks, &rsp.region_0_supported_blk_sz_mask, sizeof(masks));
        for (r = 0; r < rsp.num_supported_dc_regions && r < 8; r++)
            printf("region_%u_supported_blk_sz_mask:  0x%016llx\n",
                   r, (unsigned long long)masks[r]);
    }
    return 0;
}

static const struct mctp_cci_cmd dcd_mgmt_cmds[] = {
    { "info", "Get DCD Info (5600h)", do_get_dcd_info },
};

const struct mctp_cci_top dcd_mgmt_top = {
    .name = "dcd-mgmt",
    .help = "DCD_MANAGEMENT (0x56): info",
    .cmds = dcd_mgmt_cmds,
    .n_cmds = sizeof(dcd_mgmt_cmds) / sizeof(dcd_mgmt_cmds[0]),
};
