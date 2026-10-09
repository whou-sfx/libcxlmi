// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: VU ECCRMWINJ command (OPCODE_VU=0xCC53, vuCmdId=ECCRMWINJ=0x011b).
 *
 * unlock -> fill param -> vendor_specific -> lock.
 * Mailbox layout matches vu_handler_eccrmwinj (pargs == NULL):
 *
 *   DPA:  arg1=op  arg2=dpa_l  arg3=dpa_h
 *   gran: arg1=0x10|op|(gran<<8)  arg2=off_l  arg3=off_h
 *         fru  gran=1 arg4=fru
 *         rank gran=2 arg4=rank
 *         dram gran=3 arg4=rank  arg2=row|codeword  arg3=0
 *
 * op: 0=clear 1=1bit 2=cecc 3=uecc. DPA and byte offsets are 64B-aligned.
 * Row and codeword share gran 3; firmware ECS mode picks which one arg2 is.
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

/* arg1 bit4 selects gran mode; gran is arg1[10:8]. Matches firmware. */
#define ECCRMW_GRAN_SEL  0x10U
#define ECCRMW_GRAN_FRU  1U
#define ECCRMW_GRAN_RANK 2U
#define ECCRMW_GRAN_DRAM 3U

enum {
	ADDR_NONE = 0,
	ADDR_DPA,
	ADDR_FRU,
	ADDR_RANK,
	ADDR_ROW,
	ADDR_CW,
};

static void usage(void)
{
	fprintf(stderr,
		"Usage: vu-eccrmwinj --op <1bit|cecc|uecc|clear> --dpa <hex>\n"
		"       vu-eccrmwinj --op <1bit|cecc|uecc|clear> --fru <0-255> --off <hex>\n"
		"       vu-eccrmwinj --op <1bit|cecc|uecc|clear> --rank <0-255> --off <hex>\n"
		"       vu-eccrmwinj --op <1bit|cecc|uecc|clear> --rank <0-255> --row <n>\n"
		"       vu-eccrmwinj --op <1bit|cecc|uecc|clear> --rank <0-255> --codeword <n>\n"
		"  --op         1bit|1  cecc|2  uecc|3  clear|0\n"
		"  --dpa        device physical address (aligned down to 64B)\n"
		"  --fru        FRU id; --off is a byte offset in that FRU\n"
		"  --rank       global rank\n"
		"  --off        byte offset used with --fru or --rank (aligned down to 64B)\n"
		"  --row        DRAM row index; requires ECS per-DRAM and row mode\n"
		"  --codeword   DRAM codeword index; requires ECS per-DRAM and codeword mode\n");
}

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

static int parse_u64(const char *s, uint64_t *out)
{
	char *end = NULL;
	unsigned long long v;

	if (!s || !s[0])
		return -1;
	v = strtoull(s, &end, 0);
	if (end == s || (end && *end))
		return -1;
	*out = (uint64_t)v;
	return 0;
}

/* Firmware rejects arg4 above 0xFF for fru and rank. */
static int parse_unit(const char *s, u32 *out)
{
	uint64_t v;

	if (parse_u64(s, &v) || v > 0xFFULL)
		return -1;
	*out = (u32)v;
	return 0;
}

static int parse_u32(const char *s, u32 *out)
{
	uint64_t v;

	if (parse_u64(s, &v) || v > 0xFFFFFFFFULL)
		return -1;
	*out = (u32)v;
	return 0;
}

static int conflict(void)
{
	fprintf(stderr,
		"vu-eccrmwinj: choose one of --dpa, --fru --off, "
		"--rank --off, --rank --row, --rank --codeword\n");
	return -1;
}

int cmd_vu_eccrmwinj(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	eccrmwinj req = {
		.vuCmdId = ECCRMWINJ,
		.status  = 0,
		.in_sz   = 0,
		.out_sz  = 0,
		.arg1    = 0,
		.arg2    = 0,
		.arg3    = 0,
		.arg4    = 0,
	};
	u32 op = 0;
	u32 unit = 0;
	u32 sel = 0;
	uint64_t addr = 0;
	int has_op = 0, has_rank = 0, has_off = 0, has_sel = 0;
	int mode = ADDR_NONE;
	int rc;

	for (int a = 1; a < argc; a++) {
		if (strcmp(argv[a], "--op") == 0 && a + 1 < argc) {
			if (parse_op(argv[++a], &op))
				return -1;
			has_op = 1;
		} else if (strcmp(argv[a], "--dpa") == 0 && a + 1 < argc) {
			if (mode != ADDR_NONE || has_rank || has_off || has_sel)
				return conflict();
			mode = ADDR_DPA;
			if (parse_u64(argv[++a], &addr)) {
				fprintf(stderr, "vu-eccrmwinj: invalid --dpa\n");
				return -1;
			}
		} else if (strcmp(argv[a], "--fru") == 0 && a + 1 < argc) {
			if (mode != ADDR_NONE || has_rank || has_sel)
				return conflict();
			mode = ADDR_FRU;
			if (parse_unit(argv[++a], &unit)) {
				fprintf(stderr,
					"vu-eccrmwinj: invalid --fru (0-255)\n");
				return -1;
			}
		} else if (strcmp(argv[a], "--rank") == 0 && a + 1 < argc) {
			if (has_rank || mode == ADDR_DPA || mode == ADDR_FRU)
				return conflict();
			has_rank = 1;
			if (parse_unit(argv[++a], &unit)) {
				fprintf(stderr,
					"vu-eccrmwinj: invalid --rank (0-255)\n");
				return -1;
			}
		} else if (strcmp(argv[a], "--off") == 0 && a + 1 < argc) {
			if (mode == ADDR_DPA || has_off || has_sel)
				return conflict();
			has_off = 1;
			if (parse_u64(argv[++a], &addr)) {
				fprintf(stderr, "vu-eccrmwinj: invalid --off\n");
				return -1;
			}
		} else if ((strcmp(argv[a], "--row") == 0 ||
			    strcmp(argv[a], "--codeword") == 0) &&
			   a + 1 < argc) {
			int kind = (argv[a][2] == 'r') ? ADDR_ROW : ADDR_CW;

			if (mode != ADDR_NONE || has_off || has_sel)
				return conflict();
			has_sel = 1;
			mode = kind;
			if (parse_u32(argv[++a], &sel)) {
				fprintf(stderr, "vu-eccrmwinj: invalid %s\n",
					kind == ADDR_ROW ? "--row" : "--codeword");
				return -1;
			}
		} else {
			usage();
			return -1;
		}
	}

	if (has_op && has_rank && has_off && !has_sel && mode == ADDR_NONE)
		mode = ADDR_RANK;

	if (!has_op ||
	    (mode == ADDR_DPA && (has_rank || has_off || has_sel)) ||
	    (mode == ADDR_FRU && (!has_off || has_rank || has_sel)) ||
	    (mode != ADDR_DPA && mode != ADDR_FRU && mode != ADDR_RANK &&
	     mode != ADDR_ROW && mode != ADDR_CW) ||
	    ((mode == ADDR_ROW || mode == ADDR_CW) && (!has_rank || has_off))) {
		usage();
		return -1;
	}

	if (mode == ADDR_ROW || mode == ADDR_CW) {
		/* arg3 must stay 0; firmware rejects a high half on dram. */
		req.arg1 = ECCRMW_GRAN_SEL | op | (ECCRMW_GRAN_DRAM << 8);
		req.arg2 = sel;
		req.arg4 = unit;
	} else {
		/* Same 64B align as device vu_handler_eccrmwinj */
		addr &= ~0x3FULL;
		req.arg2 = (u32)(addr & 0xffffffffULL);
		req.arg3 = (u32)(addr >> 32);
		if (mode == ADDR_DPA) {
			req.arg1 = op;
		} else {
			u32 gran = (mode == ADDR_FRU) ? ECCRMW_GRAN_FRU
						      : ECCRMW_GRAN_RANK;

			req.arg1 = ECCRMW_GRAN_SEL | op | (gran << 8);
			req.arg4 = unit;
		}
	}

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

	if (rc == 0) {
		if (mode == ADDR_DPA)
			printf("vu-eccrmwinj OK: op=%u (%s) dpa=0x%llx\n",
			       op, op_names[op], (unsigned long long)addr);
		else if (mode == ADDR_ROW || mode == ADDR_CW)
			printf("vu-eccrmwinj OK: op=%u (%s) rank=%u %s=%u\n",
			       op, op_names[op], unit,
			       mode == ADDR_ROW ? "row" : "codeword", sel);
		else
			printf("vu-eccrmwinj OK: op=%u (%s) %s=%u off=0x%llx\n",
			       op, op_names[op],
			       mode == ADDR_FRU ? "fru" : "rank",
			       unit, (unsigned long long)addr);
	}
	return rc;
}
