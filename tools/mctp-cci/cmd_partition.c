// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static int do_get_partition(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                            int argc, char **argv)
{
    struct cxlmi_cmd_memdev_get_partition_info_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_memdev_get_partition_info(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-partition");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }

    printf("Active Volatile     : 0x%016llx\n", (unsigned long long)rsp.active_vmem);
    printf("Active Persistent   : 0x%016llx\n", (unsigned long long)rsp.active_pmem);
    printf("Next Volatile       : 0x%016llx\n", (unsigned long long)rsp.next_vmem);
    printf("Next Persistent     : 0x%016llx\n", (unsigned long long)rsp.next_pmem);
    return 0;
}

static int do_set_partition(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                            int argc, char **argv)
{
    struct cxlmi_cmd_memdev_set_partition_info_req req = {0};
    int i, rc;

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--next-volatile") == 0 && i + 1 < argc) {
            uint64_t v;
            if (parse_size_with_unit(argv[++i], &v) < 0) {
                fprintf(stderr, "bad --next-volatile\n"); return 2;
            }
            memcpy(&req.volatile_capacity, &v, sizeof(v));
        } else if (strcmp(argv[i], "--flags") == 0 && i + 1 < argc) {
            req.flags = strtoul(argv[++i], NULL, 0);
        }
    }
    rc = cxlmi_cmd_memdev_set_partition_info(ep, ti, &req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("set-partition");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static const struct mctp_cci_cmd partition_cmds[] = {
    { "get", "Get Partition Info (4100h)", do_get_partition },
    { "set", "Set Partition Info (4101h) --next-volatile <size> [--flags <n>]", do_set_partition },
};

const struct mctp_cci_top partition_top = {
    .name = "partition",
    .help = "CCLS (0x41): get/set partition info",
    .cmds = partition_cmds,
    .n_cmds = sizeof(partition_cmds) / sizeof(partition_cmds[0]),
};
