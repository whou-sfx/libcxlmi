// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static int do_get_ld_info(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                          int argc, char **argv)
{
    struct cxlmi_cmd_fmapi_get_ld_info_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_fmapi_get_ld_info(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-ld-info");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    printf("memory_size: 0x%llx\n", (unsigned long long)rsp.memory_size);
    printf("ld_count: %u\n", rsp.ld_count);
    return 0;
}

static int do_get_ld_alloc(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                           int argc, char **argv)
{
    struct cxlmi_cmd_fmapi_get_ld_allocations_req req = {
        .start_ld_id = 0,
        .ld_allocation_list_limit = 32,
    };
    struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *rsp;
    int rc;
    size_t buf_sz;
    (void)argc; (void)argv;

    /* The response has a flexible array. Allocate conservatively. */
    buf_sz = sizeof(*rsp) + 32 * sizeof(struct cxlmi_cmd_fmapi_ld_allocations_list);
    rsp = calloc(1, buf_sz);
    if (!rsp) { perror("calloc"); return -1; }

    rc = cxlmi_cmd_fmapi_get_ld_allocations(ep, ti, &req, rsp);
    if (rc < 0) { free(rsp); return mctp_cci_report_libcxlmi_error("get-ld-allocations"); }
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); free(rsp); return 1; }
    printf("number_ld: %u\n", rsp->number_ld);
    printf("memory_granularity: %u\n", rsp->memory_granularity);
    printf("start_ld_id: %u\n", rsp->start_ld_id);
    free(rsp);
    return 0;
}

static int do_get_multiheaded(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                              int argc, char **argv)
{
    struct cxlmi_cmd_fmapi_get_multiheaded_info_req req = {
        .start_ld_id = 0,
        .ld_map_list_limit = 32,
    };
    struct cxlmi_cmd_fmapi_get_multiheaded_info_rsp *rsp;
    int rc;
    size_t buf_sz;
    (void)argc; (void)argv;

    /* The response has a flexible array. Allocate conservatively. */
    buf_sz = sizeof(*rsp) + 32 * sizeof(rsp->ld_map[0]);
    rsp = calloc(1, buf_sz);
    if (!rsp) { perror("calloc"); return -1; }

    rc = cxlmi_cmd_fmapi_get_multiheaded_info(ep, ti, &req, rsp);
    if (rc < 0) { free(rsp); return mctp_cci_report_libcxlmi_error("get-multiheaded-info"); }
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); free(rsp); return 1; }
    printf("num_lds: %u\n", rsp->num_lds);
    printf("num_heads: %u\n", rsp->num_heads);
    free(rsp);
    return 0;
}

static const struct mctp_cci_cmd mld_cmds[] = {
    { "ld-info",         "Get LD Info (5400h)",            do_get_ld_info },
    { "ld-allocations",  "Get LD Allocations (5401h)",     do_get_ld_alloc },
    { "multiheaded",     "Get Multi-Headed Info (5500h)",  do_get_multiheaded },
};

const struct mctp_cci_top mld_top = {
    .name = "mld",
    .help = "MLD (0x53/0x54/0x55): ld-info, ld-allocations, multiheaded",
    .cmds = mld_cmds,
    .n_cmds = sizeof(mld_cmds) / sizeof(mld_cmds[0]),
};
