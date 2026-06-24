// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

#define PASSPHRASE_LEN 0x20

static int do_get_state(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                        int argc, char **argv)
{
    struct cxlmi_cmd_memdev_get_security_state_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_memdev_get_security_state(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-security-state");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    printf("state: 0x%x\n", rsp.security_state);
    return 0;
}

static int do_set_passphrase(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                             int argc, char **argv)
{
    struct cxlmi_cmd_memdev_set_passphrase_req req = {0};
    int rc;
    size_t old_len, new_len;

    if (argc < 2) {
        fprintf(stderr, "usage: security set-pass <old> <new>\n"); return 2;
    }
    old_len = strlen(argv[0]);
    new_len = strlen(argv[1]);
    if (old_len > PASSPHRASE_LEN || new_len > PASSPHRASE_LEN) {
        fprintf(stderr, "passphrase exceeds %d bytes\n", PASSPHRASE_LEN);
        return 2;
    }
    memcpy(req.current_passphrase, argv[0], old_len);
    memcpy(req.new_passphrase, argv[1], new_len);
    rc = cxlmi_cmd_memdev_set_passphrase(ep, ti, &req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("set-passphrase");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static int do_unlock(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                     int argc, char **argv)
{
    struct cxlmi_cmd_memdev_unlock_req req = {0};
    int rc;
    size_t len;

    if (argc < 1) {
        fprintf(stderr, "usage: security unlock <passphrase>\n"); return 2;
    }
    len = strlen(argv[0]);
    if (len > PASSPHRASE_LEN) {
        fprintf(stderr, "passphrase exceeds %d bytes\n", PASSPHRASE_LEN);
        return 2;
    }
    memcpy(req.current_passphrase, argv[0], len);
    rc = cxlmi_cmd_memdev_unlock(ep, ti, &req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("unlock");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static int do_freeze(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                     int argc, char **argv)
{
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_memdev_freeze_security_state(ep, ti);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("freeze");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static const struct mctp_cci_cmd security_cmds[] = {
    { "state",       "Get Security State (4600h)",          do_get_state },
    { "set-pass",    "Set Passphrase (4601h) <old> <new>",  do_set_passphrase },
    { "unlock",      "Unlock (4602h) <passphrase>",         do_unlock },
    { "freeze",      "Freeze Security State (4604h)",       do_freeze },
};

const struct mctp_cci_top security_top = {
    .name = "security",
    .help = "SECURITY (0x46): state, set-pass, unlock, freeze",
    .cmds = security_cmds,
    .n_cmds = sizeof(security_cmds) / sizeof(security_cmds[0]),
};
