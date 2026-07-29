// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: VU ECCRMWINJ command (OPCODE_VU=0xCC53, vuCmdId=ECCRMWINJ=0x011b).
 *
 * Same host pattern as cmd_vu_evtadd.c: unlock -> fill param -> vendor_specific
 * -> lock. Device vu_handler_eccrmwinj should mirror evtadd's dual input:
 *
 *   if (pargs) { ... shell ... }
 *   else {                      // mailbox path (pargs == NULL)
 *       op   = param->arg1;     // 0=clear 1=1bit 2=cecc 3=uecc
 *       dpa  = ((u64)param->arg3 << 32) | param->arg2;
 *   }
 *
 * Device then: align dpa, map port, clear, inject (drv_ddr_*).
 */

/*
 * Compatibility shims required before including vu_handler_def.h in plain C:
 *   - u32 is not a standard C type
 *   - __packed is a Linux kernel macro
 *   - MBCCI_SFX_BUILD gates out C++ function declarations in the header
 */
#include <stdint.h>
typedef uint32_t u32;
#define __packed __attribute__((packed))
#define MBCCI_SFX_BUILD
#include "../../docs/vu_handler_def.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

static const char * const op_names[] = {
	"clear", "1bit", "cecc", "uecc",
};

static int vu_unlock(struct cxlmi_endpoint *ep)
{
	vuunlock req = { .vuCmdId = VUUNLOCK, 0 };
	int rc = cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
					   &req, sizeof(req), NULL, 0);
	if (rc)
		fprintf(stderr, "vu-unlock failed (rc=%d)\n", rc);
	return rc;
}

static int vu_lock(struct cxlmi_endpoint *ep)
{
	vulock req = { .vuCmdId = VULOCK, 0 };
	int rc = cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
					   &req, sizeof(req), NULL, 0);
	if (rc)
		fprintf(stderr, "vu-lock failed (rc=%d)\n", rc);
	return rc;
}

static int parse_op(const char *s, u32 *op_out)
{
	if (!strcasecmp(s, "clear") || !strcmp(s, "0")) {
		*op_out = 0;
		return 0;
	}
	if (!strcasecmp(s, "1bit") || !strcmp(s, "1")) {
		*op_out = 1;
		return 0;
	}
	if (!strcasecmp(s, "cecc") || !strcmp(s, "2")) {
		*op_out = 2;
		return 0;
	}
	if (!strcasecmp(s, "uecc") || !strcmp(s, "3")) {
		*op_out = 3;
		return 0;
	}

	fprintf(stderr,
		"vu-eccrmwinj: unknown --op '%s' "
		"(valid: 1bit|1, cecc|2, uecc|3, clear|0)\n", s);
	return -1;
}

int cmd_vu_eccrmwinj(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	/* Mailbox param layout — FW else-branch should read these like evtadd */
	eccrmwinj req = {
		.vuCmdId = ECCRMWINJ,
		.status  = 0,
		.in_sz   = 0,
		.out_sz  = 0,
		.arg1    = 0, /* op */
		.arg2    = 0, /* dpa[31:0] */
		.arg3    = 0, /* dpa[63:32] */
		.arg4    = 0,
	};
	uint64_t dpa = 0;
	int has_op = 0, has_dpa = 0;
	int rc;

	for (int a = 1; a < argc; a++) {
		if (strcmp(argv[a], "--op") == 0 && a + 1 < argc) {
			if (parse_op(argv[++a], &req.arg1))
				return -1;
			has_op = 1;
		} else if (strcmp(argv[a], "--dpa") == 0 && a + 1 < argc) {
			char *end = NULL;
			unsigned long long v = strtoull(argv[++a], &end, 0);

			if (!argv[a][0] || (end && *end)) {
				fprintf(stderr, "vu-eccrmwinj: invalid --dpa\n");
				return -1;
			}
			dpa = (uint64_t)v;
			has_dpa = 1;
		} else {
			fprintf(stderr,
				"Usage: vu-eccrmwinj --op <1bit|cecc|uecc|clear> --dpa <hex>\n");
			return -1;
		}
	}

	if (!has_op || !has_dpa) {
		fprintf(stderr,
			"Usage: vu-eccrmwinj --op <1bit|cecc|uecc|clear> --dpa <hex>\n"
			"  --op   1bit|1  cecc|2  uecc|3  clear|0\n"
			"  --dpa  device physical address (hex; aligned down to 64B)\n");
		return -1;
	}

	/* Same 64B align as device vu_handler_eccrmwinj */
	dpa &= ~0x3FULL;
	req.arg2 = (u32)(dpa & 0xffffffffULL);
	req.arg3 = (u32)(dpa >> 32);

	rc = vu_unlock(ep);
	if (rc)
		return rc;

	rc = cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
				       &req, sizeof(req),
				       NULL, 0);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "vu-eccrmwinj failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "vu-eccrmwinj ioctl failed\n");
	}

	/* always lock, even on inject failure */
	int lock_rc = vu_lock(ep);
	if (rc == 0)
		rc = lock_rc;

	if (rc == 0)
		printf("vu-eccrmwinj OK: op=%u (%s) dpa=0x%llx\n",
		       req.arg1, op_names[req.arg1],
		       (unsigned long long)dpa);
	return rc;
}
