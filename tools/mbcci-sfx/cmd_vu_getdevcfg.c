// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: VU GETCFG command (OPCODE_VU=0xCC53, vuCmdId=GETCFG=0x08).
 *
 * Reads device DEV configuration from the device in chunked transfers and
 * writes it to an output file via the vendor-specific mailbox CCI path.
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

#define GETCFG_CHUNK VU_GETCFG_OUT_PAYLOAD

#define VU_GETDEVCFG_USAGE \
	"vu-getdevcfg --output <file>"

int parse_vu_getdevcfg_req(int argc, char **argv,
			   struct vu_getdevcfg_params *params)
{
	int has_output = 0;
	int i;

	memset(params, 0, sizeof(*params));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
			params->output_file = argv[++i];
			has_output = 1;
		} else {
			fprintf(stderr, "Usage: %s\n", VU_GETDEVCFG_USAGE);
			return -1;
		}
	}

	if (!has_output) {
		fprintf(stderr, "Usage: %s\n", VU_GETDEVCFG_USAGE);
		fprintf(stderr,
			"  --output  configuration file to write (binary)\n");
		return -1;
	}

	return 0;
}

int vu_getdevcfg_fetch(struct cxlmi_endpoint *ep,
		       const struct vu_getdevcfg_params *params,
		       vu_getcfg_send_fn send_fn, void *send_ctx)
{
	getcfg req;
	getcfg_output out;
	FILE *fp = NULL;
	uint32_t offset = 0;
	uint32_t total_bytes = 0;
	unsigned round = 0;
	int rc = -1;

	fp = fopen(params->output_file, "wb");
	if (!fp) {
		perror("vu-getdevcfg: fopen");
		return -1;
	}

	fprintf(stderr, "vu-getdevcfg: output=%s chunk=%u\n",
		params->output_file, GETCFG_CHUNK);

	do {
		memset(&req, 0, sizeof(req));
		memset(&out, 0, sizeof(out));

		req.vuCmdId = GETCFG;
		req.in_sz = (u32)sizeof(req);
		req.out_sz = (u32)sizeof(out);
		req.offset = offset;
		req.length = GETCFG_CHUNK;

		rc = send_fn(ep, send_ctx, &req, &out);
		if (rc) {
			if (rc > 0)
				fprintf(stderr, "vu-getdevcfg failed: %s\n",
					cxlmi_cmd_retcode_tostr(rc));
			else
				fprintf(stderr, "vu-getdevcfg ioctl failed\n");
			goto out_fclose;
		}

		if (out.respLen > GETCFG_CHUNK) {
			fprintf(stderr,
				"vu-getdevcfg: invalid respLen %u (max %u)\n",
				out.respLen, GETCFG_CHUNK);
			rc = -1;
			goto out_fclose;
		}

		if (out.respLen > 0) {
			if (fwrite(out.payload, 1, out.respLen, fp) != out.respLen) {
				perror("vu-getdevcfg: fwrite");
				rc = -1;
				goto out_fclose;
			}
			total_bytes += out.respLen;
		}

		round++;
		fprintf(stderr,
			"vu-getdevcfg: round %u offset=%u respLen=%u more=%u\n",
			round, offset, out.respLen, out.more);

		offset += out.respLen;
	} while (out.more);

	printf("vu-getdevcfg OK\n");
	printf("  Output:      %s\n", params->output_file);
	printf("  Total Bytes: %u\n", total_bytes);
	printf("  Rounds:      %u\n", round);

	rc = 0;

out_fclose:
	fclose(fp);
	return rc;
}

static int vu_getcfg_send_direct(struct cxlmi_endpoint *ep, void *ctx,
				 void *req, void *out)
{
	(void)ctx;
	return cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
				       req, sizeof(getcfg),
				       out, sizeof(getcfg_output));
}

int cmd_vu_getdevcfg(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct vu_getdevcfg_params params;
	int rc, lock_rc;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s\n", VU_GETDEVCFG_USAGE);
		return -1;
	}

	rc = parse_vu_getdevcfg_req(argc - 1, argv + 1, &params);
	if (rc)
		return rc;

	rc = vu_mb_unlock(ep);
	if (rc)
		return rc;

	rc = vu_getdevcfg_fetch(ep, &params, vu_getcfg_send_direct, NULL);

	lock_rc = vu_mb_lock(ep);
	if (rc == 0)
		rc = lock_rc;

	return rc;
}
