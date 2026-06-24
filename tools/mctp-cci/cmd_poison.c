// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static int do_get_poison_list(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                              int argc, char **argv)
{
    struct cxlmi_cmd_memdev_get_poison_list_req req = {0};
    struct cxlmi_cmd_memdev_get_poison_list_rsp *rsp;
    int rc, i;
    size_t buf_sz;

    if (argc >= 1) req.get_poison_list_phy_addr = strtoull(argv[0], NULL, 0);
    if (argc >= 2) req.get_poison_list_phy_addr_len = strtoull(argv[1], NULL, 0);

    /* Flexible array of media error records — allocate for a generous count */
    buf_sz = sizeof(*rsp) + 256 * sizeof(struct cxlmi_memdev_media_err_record);
    rsp = calloc(1, buf_sz);
    if (!rsp) { perror("calloc"); return 2; }

    rc = cxlmi_cmd_get_poison_list(ep, ti, &req, rsp);
    if (rc < 0) { free(rsp); return mctp_cci_report_libcxlmi_error("get-poison-list"); }
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); free(rsp); return 1; }

    printf("poison_list_flags: 0x%02x\n", rsp->poison_list_flags);
    printf("overflow_timestamp: 0x%016lx\n", (unsigned long)rsp->overflow_timestamp);
    printf("more_err_media_record_cnt: %u\n", rsp->more_err_media_record_cnt);
    for (i = 0; i < rsp->more_err_media_record_cnt; i++) {
        printf("  [%d] media_err_addr=0x%016lx media_err_len=0x%08x\n", i,
               (unsigned long)rsp->records[i].media_err_addr,
               rsp->records[i].media_err_len);
    }
    free(rsp);
    return 0;
}

static int do_inject_poison(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                            int argc, char **argv)
{
    struct cxlmi_cmd_memdev_inject_poison_req req = {0};
    int rc;

    if (argc < 1) { fprintf(stderr, "usage: poison inject <phys_addr>\n"); return 2; }
    req.inject_poison_phy_addr = strtoull(argv[0], NULL, 0);
    rc = cxlmi_cmd_memdev_inject_poison(ep, ti, &req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("inject-poison");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static int do_clear_poison(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                           int argc, char **argv)
{
    struct cxlmi_cmd_memdev_clear_poison_req req = {0};
    int rc;

    if (argc < 1) { fprintf(stderr, "usage: poison clear <phys_addr> [<write_data_hex>]\n"); return 2; }
    req.clear_poison_phy_addr = strtoull(argv[0], NULL, 0);
    if (argc >= 2) {
        const char *hex = argv[1];
        size_t hex_len = strlen(hex);
        size_t out_len = sizeof(req.clear_poison_write_data);
        unsigned char *dst = req.clear_poison_write_data;

        /* Optional write data: hex string, low nibble first byte. Pad/truncate to 64. */
        if (hex_len > out_len * 2) hex_len = out_len * 2;
        memset(dst, 0, out_len);
        for (size_t i = 0; i + 1 < hex_len; i += 2) {
            unsigned int byte;
            if (sscanf(&hex[i], "%2x", &byte) != 1) {
                fprintf(stderr, "invalid hex in <write_data_hex>\n");
                return 2;
            }
            dst[i / 2] = (unsigned char)byte;
        }
    }
    rc = cxlmi_cmd_memdev_clear_poison(ep, ti, &req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("clear-poison");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static const struct mctp_cci_cmd poison_cmds[] = {
    { "list",   "Get Poison List (4300h) [<phys_addr> [<len>]]",                  do_get_poison_list },
    { "inject", "Inject Poison (4301h) <phys_addr>",                              do_inject_poison },
    { "clear",  "Clear Poison (4302h) <phys_addr> [<write_data_hex>]",            do_clear_poison },
};

const struct mctp_cci_top poison_top = {
    .name = "poison",
    .help = "MEDIA_AND_POISON (0x43): list, inject, clear",
    .cmds = poison_cmds,
    .n_cmds = sizeof(poison_cmds) / sizeof(poison_cmds[0]),
};
