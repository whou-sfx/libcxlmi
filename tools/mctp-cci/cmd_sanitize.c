// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static int do_sanitize(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                       int argc, char **argv)
{
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_memdev_sanitize(ep, ti);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("sanitize");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static int do_secure_erase(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                           int argc, char **argv)
{
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_memdev_secure_erase(ep, ti);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("secure-erase");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static const struct mctp_cci_cmd sanitize_cmds[] = {
    { "sanitize",     "Sanitize (4400h)", do_sanitize },
    { "secure-erase", "Secure Erase (4401h)", do_secure_erase },
};

const struct mctp_cci_top sanitize_top = {
    .name = "sanitize",
    .help = "SANITIZE (0x44): sanitize, secure-erase",
    .cmds = sanitize_cmds,
    .n_cmds = sizeof(sanitize_cmds) / sizeof(sanitize_cmds[0]),
};
