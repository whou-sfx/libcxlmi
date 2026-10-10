// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: VU DIMMINFO command (OPCODE_VU=0xCC53, vuCmdId=DIMMINFO=0x024F).
 *
 * SPD identity for one DIMM. Matches vu_handler_dimminfo mailbox path:
 *   arg1=dimm, output dimminfo_output (100 bytes).
 */

#include <stdint.h>
typedef uint32_t u32;
#define __packed __attribute__((packed))
#define MBCCI_SFX_BUILD
#include "../../docs/vu_handler_def.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

_Static_assert(sizeof(dimminfo_output) == 100,
	       "dimminfo VU output ABI changed");

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

static void print_field(const char *label, const char *src, size_t n)
{
	size_t len = 0;

	while (len < n && src[len] != '\0')
		len++;
	printf("%-12s %.*s\n", label, (int)len, src);
}

int cmd_vu_dimminfo(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	dimminfo req = {
		.vuCmdId = DIMMINFO,
		.status  = 0,
		.in_sz   = 0,
		.out_sz  = (u32)sizeof(dimminfo_output),
		.dimm    = 0,
		.reserved1 = 0,
		.reserved2 = 0,
		.reserved3 = 0,
	};
	dimminfo_output out;
	int has_dimm = 0;
	int rc;

	memset(&out, 0, sizeof(out));

	for (int a = 1; a < argc; a++) {
		if (strcmp(argv[a], "--dimm") == 0 && a + 1 < argc) {
			char *end = NULL;
			unsigned long v = strtoul(argv[++a], &end, 0);

			if (!argv[a][0] || (end && *end)) {
				fprintf(stderr, "vu-dimminfo: invalid --dimm\n");
				return -1;
			}
			req.dimm = (u32)v;
			has_dimm = 1;
		} else {
			fprintf(stderr, "Usage: vu-dimminfo --dimm <n>\n");
			return -1;
		}
	}

	if (!has_dimm) {
		fprintf(stderr,
			"Usage: vu-dimminfo --dimm <n>\n"
			"  --dimm  DIMM index\n");
		return -1;
	}

	rc = vu_unlock(ep);
	if (rc)
		return rc;

	rc = cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
				       &req, sizeof(req),
				       &out, sizeof(out));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "vu-dimminfo failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "vu-dimminfo ioctl failed\n");
	}

	int lock_rc = vu_lock(ep);
	if (rc == 0)
		rc = lock_rc;

	if (rc == 0) {
		printf("vu-dimminfo OK: dimm=%u\n", req.dimm);
		print_field("TYPE:", out.module, sizeof(out.module));
		printf("%-12s %u GB\n", "Capacity:", out.capacity_gb);
		printf("%-12s %u MT/s\n", "Speed:", out.speed_mts);
		print_field("Module MFR:", out.module_mfr, sizeof(out.module_mfr));
		print_field("DRAM MFR:", out.dram_mfr, sizeof(out.dram_mfr));
		print_field("Part Number:", out.part_number, sizeof(out.part_number));
	}
	return rc;
}
