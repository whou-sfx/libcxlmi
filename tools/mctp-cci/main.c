// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static const struct mctp_cci_top *const tops[] = {
    &info_top,     &events_top,   &fw_top,        &ts_top,
    &logs_top,     &features_top, &identify_top,  &partition_top,
    &health_top,   &poison_top,   &sanitize_top,  &pmem_top,
    &security_top, &qos_top,      &dcd_top,       &switch_top,
    &mld_top,      &dcd_mgmt_top, &vendor_top,
};
static const size_t ntops = sizeof(tops) / sizeof(tops[0]);

static void print_top(FILE *out, const struct mctp_cci_top *t)
{
    size_t i;
    fprintf(out, "  %-12s %s\n", t->name, t->help);
    for (i = 0; i < t->n_cmds; i++)
        fprintf(out, "    %-12s %s\n", t->cmds[i].name, t->cmds[i].help);
}

static void usage(FILE *out, const char *prog)
{
    size_t i;
    fprintf(out, "Usage: %s <nid> <eid> <top> [sub] [args...]\n", prog);
    fprintf(out, "       %s <nid> <eid> --help\n", prog);
    fprintf(out, "       %s -h | --help\n\n", prog);
    fprintf(out, "Tunnel options (apply to any <top> <sub>):\n");
    fprintf(out, "  --tunnel-port N         tunnel via CXL Switch downstream port N\n");
    fprintf(out, "  --tunnel-ld M           tunnel to LD M within an MLD\n");
    fprintf(out, "  --port-and-ld N,M       tunnel through switch port N to LD M\n");
    fprintf(out, "  --tunnel-mhd            tunnel to LD Pool CCI in a MHD\n\n");
    fprintf(out, "Top-level subcommands:\n");
    for (i = 0; i < ntops; i++)
        print_top(out, tops[i]);
}

static const struct mctp_cci_top *find_top(const char *name)
{
    size_t i;
    for (i = 0; i < ntops; i++)
        if (strcmp(tops[i]->name, name) == 0)
            return tops[i];
    return NULL;
}

static const struct mctp_cci_cmd *find_cmd(const struct mctp_cci_top *t,
                                           const char *name)
{
    size_t i;
    for (i = 0; i < t->n_cmds; i++)
        if (strcmp(t->cmds[i].name, name) == 0)
            return &t->cmds[i];
    return NULL;
}

int main(int argc, char **argv)
{
    const char *prog = argv[0] ? argv[0] : "mctp-cci";
    struct cxlmi_ctx *ctx;
    struct cxlmi_endpoint *ep;
    struct cxlmi_tunnel_info ti = { .port = -1, .ld = -1, .level = 0, .mhd = false };
    const struct mctp_cci_top *t;
    const struct mctp_cci_cmd *c;
    unsigned int net;
    unsigned int eid;
    int sub_argc, rc;

    if (argc >= 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(stdout, prog);
        return EXIT_SUCCESS;
    }

    if (argc < 4) {
        usage(stderr, prog);
        return EXIT_FAILURE;
    }

    if (parse_hex_u64(argv[1], (uint64_t *)&net) < 0 || net > 0xff) {
        fprintf(stderr, "invalid <nid>: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    if (parse_hex_u64(argv[2], (uint64_t *)&eid) < 0 || eid > 0xff) {
        fprintf(stderr, "invalid <eid>: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    t = find_top(argv[3]);
    if (!t) {
        fprintf(stderr, "Unknown top-level subcommand: %s\n\n", argv[3]);
        usage(stderr, prog);
        return EXIT_FAILURE;
    }

    /* Show top-level help without opening the MCTP endpoint. */
    if (argc >= 5 && strcmp(argv[4], "--help") == 0) {
        print_top(stdout, t);
        return EXIT_SUCCESS;
    }

    ctx = cxlmi_new_ctx(stderr, LOG_WARNING);
    if (!ctx) {
        fprintf(stderr, "cannot create libcxlmi context\n");
        return EXIT_FAILURE;
    }
    ep = cxlmi_open_mctp(ctx, net, (uint8_t)eid);
    if (!ep) {
        fprintf(stderr, "cannot open MCTP endpoint %u:%u\n", net, eid);
        cxlmi_free_ctx(ctx);
        return EXIT_FAILURE;
    }

    sub_argc = parse_tunnel_args(argc - 4, argv + 4, &ti);
    if (sub_argc < 0) {
        cxlmi_close(ep);
        cxlmi_free_ctx(ctx);
        return EXIT_FAILURE;
    }

    if (sub_argc < 1) {
        print_top(stdout, t);
        cxlmi_close(ep);
        cxlmi_free_ctx(ctx);
        return EXIT_SUCCESS;
    }

    c = find_cmd(t, argv[4]);
    if (!c) {
        fprintf(stderr, "Unknown subcommand '%s %s'\n\n", t->name, argv[4]);
        print_top(stderr, t);
        cxlmi_close(ep);
        cxlmi_free_ctx(ctx);
        return EXIT_FAILURE;
    }

    rc = c->fn(ep, &ti, sub_argc - 1, argv + 5);
    cxlmi_close(ep);
    cxlmi_free_ctx(ctx);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
