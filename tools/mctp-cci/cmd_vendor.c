// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

/* ScaleFlux VU mailbox layout (see docs/vu_handler_def.h, mbcci-sfx cmd_vu_evtadd.c). */
#define OPCODE_VU  0xCC53
#define VUUNLOCK   0x0U
#define VULOCK     0x1U
#define EVTADD     0x0129U

struct vu_simple_req {
    uint32_t vuCmdId;
    uint32_t status;
    uint32_t in_sz;
    uint32_t out_sz;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
    uint32_t arg4;
};

struct vu_evtadd_req {
    uint32_t vuCmdId;
    uint32_t status;
    uint32_t in_sz;
    uint32_t out_sz;
    uint32_t loglvl;
    uint32_t intfmask;
    uint32_t count;
    uint32_t arg4;
};

static const char * const loglvl_names[] = {
    "Info", "Warn", "Fail", "Fatal",
};

static int vu_unlock(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti)
{
    struct vu_simple_req req = { .vuCmdId = VUUNLOCK };
    int rc = cxlmi_cmd_vendor_specific(ep, ti, OPCODE_VU,
                                       &req, sizeof(req), NULL, 0);

    if (rc < 0)
        return mctp_cci_report_libcxlmi_error("vu-unlock");
    if (rc > 0) {
        fprintf(stderr, "vu-unlock: %s\n", cxlmi_cmd_retcode_tostr(rc));
        return 1;
    }
    return 0;
}

static int vu_lock(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti)
{
    struct vu_simple_req req = { .vuCmdId = VULOCK };
    int rc = cxlmi_cmd_vendor_specific(ep, ti, OPCODE_VU,
                                       &req, sizeof(req), NULL, 0);

    if (rc < 0)
        return mctp_cci_report_libcxlmi_error("vu-lock");
    if (rc > 0) {
        fprintf(stderr, "vu-lock: %s\n", cxlmi_cmd_retcode_tostr(rc));
        return 1;
    }
    return 0;
}

static int do_vu_inject_event(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                              int argc, char **argv)
{
    struct vu_evtadd_req req = {
        .vuCmdId = EVTADD,
    };
    int has_loglvl = 0, has_intfmask = 0, has_count = 0;
    int i, rc, lock_rc;

    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--loglvl")) {
            unsigned long v;

            if (i + 1 >= argc) {
                fprintf(stderr, "--loglvl expects a value (0-3)\n");
                return 2;
            }
            v = strtoul(argv[++i], NULL, 0);
            if (v > 3) {
                fprintf(stderr, "--loglvl out of range (0-3)\n");
                return 2;
            }
            req.loglvl = (uint32_t)v;
            has_loglvl = 1;
        } else if (!strcmp(argv[i], "--intfmask")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--intfmask expects a value\n");
                return 2;
            }
            req.intfmask = (uint32_t)strtoul(argv[++i], NULL, 0);
            has_intfmask = 1;
        } else if (!strcmp(argv[i], "--count")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--count expects a value\n");
                return 2;
            }
            req.count = (uint32_t)strtoul(argv[++i], NULL, 0);
            has_count = 1;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
        }
    }

    if (!has_loglvl || !has_intfmask || !has_count) {
        fprintf(stderr,
                "usage: vendor inject-event --loglvl <0-3> --intfmask <hex> --count <n>\n"
                "  --loglvl   0=Info 1=Warn 2=Fail 3=Fatal\n"
                "  --intfmask interrupt/interface mask (hex)\n"
                "  --count    number of events to inject (must be > 0)\n");
        return 2;
    }

    if (req.count == 0) {
        fprintf(stderr, "inject-event: --count must be > 0\n");
        return 2;
    }

    rc = vu_unlock(ep, ti);
    if (rc)
        return rc;

    rc = cxlmi_cmd_vendor_specific(ep, ti, OPCODE_VU,
                                   &req, sizeof(req), NULL, 0);
    if (rc < 0)
        rc = mctp_cci_report_libcxlmi_error("vu-inject-event");
    else if (rc > 0) {
        fprintf(stderr, "vu-inject-event: %s\n", cxlmi_cmd_retcode_tostr(rc));
    }

    lock_rc = vu_lock(ep, ti);
    if (rc == 0)
        rc = lock_rc;

    if (rc == 0) {
        printf("vu-inject-event OK: loglvl=%u (%s) intfmask=0x%x count=%u\n",
               req.loglvl, loglvl_names[req.loglvl], req.intfmask, req.count);
    }
    return rc;
}

static const struct mctp_cci_cmd vendor_cmds[] = {
    { "inject-event", "VU Inject Event (0xCC53/0x0129) --loglvl <0-3> --intfmask <hex> --count <n>",
      do_vu_inject_event },
};

const struct mctp_cci_top vendor_top = {
    .name = "vendor",
    .help = "Vendor-specific (e.g. 0xCC53 VU Inject Event)",
    .cmds = vendor_cmds,
    .n_cmds = sizeof(vendor_cmds) / sizeof(vendor_cmds[0]),
};
