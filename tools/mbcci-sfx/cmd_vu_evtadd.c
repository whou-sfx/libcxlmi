// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: VU EVTADD command (OPCODE_VU=0xCC53, vuCmdId=EVTADD=0x0129).
 *
 * Injects a synthetic CXL event into the device's event log via the
 * vendor-specific mailbox CCI path.
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

#include <libcxlmi.h>

#include "mbcci-sfx.h"

static const char * const loglvl_names[] = {
	"Info", "Warn", "Fail", "Fatal",
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

int cmd_vu_evtadd(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	evtadd req = {
		.vuCmdId  = EVTADD,
		.status   = 0,
		.in_sz    = 0,
		.out_sz   = 0,
		.loglvl   = 0,
		.intfmask = 0,
		.count    = 0,
		.arg4     = 0,
	};
	int has_loglvl = 0, has_intfmask = 0, has_count = 0;
	int rc;

	for (int a = 1; a < argc; a++) {
		if (strcmp(argv[a], "--loglvl") == 0 && a + 1 < argc) {
			req.loglvl = (u32)strtoul(argv[++a], NULL, 0);
			has_loglvl = 1;
		} else if (strcmp(argv[a], "--intfmask") == 0 && a + 1 < argc) {
			req.intfmask = (u32)strtoul(argv[++a], NULL, 0);
			has_intfmask = 1;
		} else if (strcmp(argv[a], "--count") == 0 && a + 1 < argc) {
			req.count = (u32)strtoul(argv[++a], NULL, 0);
			has_count = 1;
		} else {
			fprintf(stderr,
				"Usage: vu-evtadd --loglvl <0-3> --intfmask <hex> --count <n>\n");
			return -1;
		}
	}

	if (!has_loglvl || !has_intfmask || !has_count) {
		fprintf(stderr,
			"Usage: vu-evtadd --loglvl <0-3> --intfmask <hex> --count <n>\n"
			"  --loglvl   0=Info 1=Warn 2=Fail 3=Fatal\n"
			"  --intfmask interrupt/interface mask (hex)\n"
			"  --count    number of events to inject\n");
		return -1;
	}

	if (req.loglvl > 3) {
		fprintf(stderr, "vu-evtadd: --loglvl must be 0-3 (got %u)\n",
			req.loglvl);
		return -1;
	}

	if (req.count == 0) {
		fprintf(stderr, "vu-evtadd: --count must be > 0\n");
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
			fprintf(stderr, "vu-evtadd failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "vu-evtadd ioctl failed\n");
	}

	/* always lock, even on evtadd failure */
	int lock_rc = vu_lock(ep);
	if (rc == 0)
		rc = lock_rc;

	if (rc == 0)
		printf("vu-evtadd OK: loglvl=%u (%s) intfmask=0x%x count=%u\n",
		       req.loglvl, loglvl_names[req.loglvl], req.intfmask, req.count);
	return rc;
}
