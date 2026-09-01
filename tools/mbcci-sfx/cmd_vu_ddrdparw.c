// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: VU DDRDPARW command (OPCODE_VU=0xCC53, vuCmdId=DDRDPARW=0x0131).
 *
 * DPA 64B media R/W via mailbox. Matches FW vu_handler_ddrdparw mailbox path:
 *   arg1=op (0=wr 1=rd) arg2=dpa_l arg3=dpa_h arg4=pat; out=data[64]
 * Write fills the 64B line with repeating 32-bit pat (default 0xdeadbeef),
 * then always reads back and returns the 64B line.
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
#include <strings.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define DDRDPARW_DEFAULT_PAT 0xdeadbeefU

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
	if (!strcasecmp(s, "wr") || !strcmp(s, "0")) {
		*op_out = 0;
		return 0;
	}
	if (!strcasecmp(s, "rd") || !strcmp(s, "1")) {
		*op_out = 1;
		return 0;
	}

	fprintf(stderr,
		"vu-ddrdparw: unknown --op '%s' (valid: wr|0, rd|1)\n", s);
	return -1;
}

static void dump_line(const u8 *data)
{
	unsigned i;

	for (i = 0; i < 16U; i++) {
		u32 w;

		memcpy(&w, data + (i * 4U), sizeof(w));
		printf("  [%u]=0x%08x\n", i, w);
	}
}

int cmd_vu_ddrdparw(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	ddrdparw req = {
		.vuCmdId = DDRDPARW,
		.status  = 0,
		.in_sz   = 0,
		.out_sz  = (u32)sizeof(ddrdparw_output),
		.arg1    = 0, /* op */
		.arg2    = 0, /* dpa[31:0] */
		.arg3    = 0, /* dpa[63:32] */
		.arg4    = DDRDPARW_DEFAULT_PAT,
	};
	ddrdparw_output out;
	uint64_t dpa = 0;
	int has_op = 0, has_dpa = 0;
	int rc;

	memset(&out, 0, sizeof(out));

	for (int a = 1; a < argc; a++) {
		if (strcmp(argv[a], "--op") == 0 && a + 1 < argc) {
			if (parse_op(argv[++a], &req.arg1))
				return -1;
			has_op = 1;
		} else if (strcmp(argv[a], "--dpa") == 0 && a + 1 < argc) {
			char *end = NULL;
			unsigned long long v = strtoull(argv[++a], &end, 0);

			if (!argv[a][0] || (end && *end)) {
				fprintf(stderr, "vu-ddrdparw: invalid --dpa\n");
				return -1;
			}
			dpa = (uint64_t)v;
			has_dpa = 1;
		} else if (strcmp(argv[a], "--pat") == 0 && a + 1 < argc) {
			char *end = NULL;
			unsigned long v = strtoul(argv[++a], &end, 0);

			if (!argv[a][0] || (end && *end)) {
				fprintf(stderr, "vu-ddrdparw: invalid --pat\n");
				return -1;
			}
			req.arg4 = (u32)v;
		} else {
			fprintf(stderr,
				"Usage: vu-ddrdparw --op <wr|rd> --dpa <hex> [--pat <hex>]\n");
			return -1;
		}
	}

	if (!has_op || !has_dpa) {
		fprintf(stderr,
			"Usage: vu-ddrdparw --op <wr|rd> --dpa <hex> [--pat <hex>]\n"
			"  --op   wr|0  rd|1\n"
			"  --dpa  device physical address (hex; aligned down to 64B)\n"
			"  --pat  32-bit write fill pattern (default 0xdeadbeef; wr only)\n");
		return -1;
	}

	dpa &= ~0x3FULL;
	req.arg2 = (u32)(dpa & 0xffffffffULL);
	req.arg3 = (u32)(dpa >> 32);

	rc = vu_unlock(ep);
	if (rc)
		return rc;

	rc = cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
				       &req, sizeof(req),
				       &out, sizeof(out));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "vu-ddrdparw failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "vu-ddrdparw ioctl failed\n");
	}

	int lock_rc = vu_lock(ep);
	if (rc == 0)
		rc = lock_rc;

	if (rc == 0) {
		printf("vu-ddrdparw OK: op=%u (%s) dpa=0x%llx pat=0x%08x\n",
		       req.arg1, req.arg1 ? "rd" : "wr",
		       (unsigned long long)dpa, req.arg4);
		dump_line(out.data);
	}
	return rc;
}
