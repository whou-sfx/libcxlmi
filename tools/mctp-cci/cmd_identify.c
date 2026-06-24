// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <string.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static void print_identify(const struct cxlmi_cmd_memdev_identify_rsp *rsp)
{
    char fwrev[sizeof(rsp->fw_revision) + 1];

    memcpy(fwrev, rsp->fw_revision, sizeof(rsp->fw_revision));
    fwrev[sizeof(rsp->fw_revision)] = '\0';

    printf("FW Revision     : %s\n", fwrev);
    printf("Total Capacity  : 0x%016llx\n", (unsigned long long)rsp->total_capacity);
    printf("Volatile Only   : 0x%016llx\n", (unsigned long long)rsp->volatile_capacity);
    printf("Persistent Only : 0x%016llx\n", (unsigned long long)rsp->persistent_capacity);
    printf("Partition Align : 0x%016llx\n", (unsigned long long)rsp->partition_align);
    printf("Info Events     : 0x%x\n", rsp->info_event_log_size);
}

static int do_memdev_identify(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                              int argc, char **argv)
{
    struct cxlmi_cmd_memdev_identify_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_memdev_identify(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("memdev-identify");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    print_identify(&rsp);
    return 0;
}

static const struct mctp_cci_cmd identify_cmds[] = {
    { "memdev", "Memory Device Identify (4000h)", do_memdev_identify },
};

const struct mctp_cci_top identify_top = {
    .name = "identify",
    .help = "Memory Device Identify (0x4000)",
    .cmds = identify_cmds,
    .n_cmds = sizeof(identify_cmds) / sizeof(identify_cmds[0]),
};
