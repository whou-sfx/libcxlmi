// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: VU DDRDBT command (OPCODE_VU=0xCC53, vuCmdId=DDRDBT=0x0132).
 *
 * Sticky MBIST skip-WI (PR-only) flag for next DBT/MBIST. Matches FW
 * vu_handler_ddrdbt mailbox path: arg1=0|1 (1=skip WI).
 */

#include <stdint.h>
typedef uint32_t u32;
typedef uint8_t u8;
#define __packed __attribute__((packed))
#define MBCCI_SFX_BUILD
#include "../../docs/vu_handler_def.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

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

int cmd_vu_ddrdbt(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	ddrdbt req = {
		.vuCmdId = DDRDBT,
		.status  = 0,
		.in_sz   = 0,
		.out_sz  = 0,
		.arg1    = 0, /* skip_wi */
		.arg2    = 0,
		.arg3    = 0,
		.arg4    = 0,
	};
	int has_enable = 0;
	int rc;

	for (int a = 1; a < argc; a++) {
		if (strcmp(argv[a], "--enable") == 0 && a + 1 < argc) {
			char *end = NULL;
			unsigned long v = strtoul(argv[++a], &end, 0);

			if (!argv[a][0] || (end && *end) || (v != 0 && v != 1)) {
				fprintf(stderr,
					"vu-ddrdbt: --enable must be 0 or 1\n");
				return -1;
			}
			req.arg1 = (u32)v;
			has_enable = 1;
		} else {
			fprintf(stderr, "Usage: vu-ddrdbt --enable <0|1>\n");
			return -1;
		}
	}

	if (!has_enable) {
		fprintf(stderr,
			"Usage: vu-ddrdbt --enable <0|1>\n"
			"  --enable  1=next DBT/MBIST skips WI (PR-only); 0=normal\n");
		return -1;
	}

	rc = vu_unlock(ep);
	if (rc)
		return rc;

	rc = cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
				       &req, sizeof(req),
				       NULL, 0);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "vu-ddrdbt failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "vu-ddrdbt ioctl failed\n");
	}

	int lock_rc = vu_lock(ep);
	if (rc == 0)
		rc = lock_rc;

	if (rc == 0)
		printf("vu-ddrdbt OK: skip_wi=%u\n", req.arg1);
	return rc;
}
