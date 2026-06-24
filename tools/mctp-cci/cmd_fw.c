// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static int do_get_fw_info(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                          int argc, char **argv)
{
    struct cxlmi_cmd_get_fw_info_rsp rsp;
    char rev[sizeof(rsp.fw_rev1) + 1];
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_get_fw_info(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-fw-info");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }

    printf("Slots supported: %u\n", rsp.slots_supported);
    printf("Slot info: 0x%x\n", rsp.slot_info);
    printf("Capabilities: 0x%x\n", rsp.caps);

    memcpy(rev, rsp.fw_rev1, sizeof(rsp.fw_rev1));
    rev[sizeof(rsp.fw_rev1)] = '\0';
    printf("FW Rev 1: %s\n", rev);
    memcpy(rev, rsp.fw_rev2, sizeof(rsp.fw_rev2));
    rev[sizeof(rsp.fw_rev2)] = '\0';
    printf("FW Rev 2: %s\n", rev);
    memcpy(rev, rsp.fw_rev3, sizeof(rsp.fw_rev3));
    rev[sizeof(rsp.fw_rev3)] = '\0';
    printf("FW Rev 3: %s\n", rev);
    memcpy(rev, rsp.fw_rev4, sizeof(rsp.fw_rev4));
    rev[sizeof(rsp.fw_rev4)] = '\0';
    printf("FW Rev 4: %s\n", rev);
    return 0;
}

static int do_transfer_fw(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                          int argc, char **argv)
{
    struct cxlmi_cmd_transfer_fw_req *req = NULL;
    void *file_buf = NULL;
    size_t file_sz = 0;
    const char *in_path = NULL;
    int i, rc;

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            in_path = argv[++i];
        }
    }
    if (!in_path) { fprintf(stderr, "missing --input\n"); return 2; }
    if (read_file_to_buffer(in_path, &file_buf, &file_sz) < 0) {
        fprintf(stderr, "cannot read %s\n", in_path);
        return 2;
    }

    req = calloc(1, sizeof(*req) + file_sz);
    if (!req) { free(file_buf); return 2; }

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--slot") == 0 && i + 1 < argc) {
            req->slot = strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--action") == 0 && i + 1 < argc) {
            req->action = strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc) {
            uint64_t v;
            if (parse_hex_u64(argv[++i], &v) < 0) {
                fprintf(stderr, "bad --offset\n");
                free(req); free(file_buf);
                return 2;
            }
            memcpy(&req->offset, &v, sizeof(v));
        }
    }
    if (file_sz > 0)
        memcpy(req->data, file_buf, file_sz);
    free(file_buf);

    rc = cxlmi_cmd_transfer_fw(ep, ti, req, file_sz);
    free(req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("transfer-fw");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static int do_activate_fw(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                          int argc, char **argv)
{
    struct cxlmi_cmd_activate_fw_req req = {0};
    int rc;

    if (argc >= 1) req.slot = strtoul(argv[0], NULL, 0);
    rc = cxlmi_cmd_activate_fw(ep, ti, &req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("activate-fw");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static const struct mctp_cci_cmd fw_cmds[] = {
    { "info",     "Get FW Info (0200h)",                                       do_get_fw_info },
    { "transfer", "Transfer FW (0201h) --input <file> --slot <n> [--action <n>] [--offset <n>]", do_transfer_fw },
    { "activate", "Activate FW (0202h) <slot>",                                 do_activate_fw },
};

const struct mctp_cci_top fw_top = {
    .name = "fw",
    .help = "FIRMWARE_UPDATE (0x02): get-fw-info, transfer-fw, activate-fw",
    .cmds = fw_cmds,
    .n_cmds = sizeof(fw_cmds) / sizeof(fw_cmds[0]),
};
