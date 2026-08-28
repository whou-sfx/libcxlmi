// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: VU PT Update command (OPCODE_VU=0xCC53, vuCmdId=PTUPDATE=0x0242).
 *
 * Uploads a fixed-size (256 byte) partition-table image to the device in a
 * single vendor-specific mailbox transfer. Mirrors fw_pt_update() in
 * cxl_test_tool's cxl_vu_download.cpp: the device writes the new table to
 * the backup slot first and then the primary slot.
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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define VU_DLPT_USAGE \
	"vu-dlpt --input <file>"

struct ptupdate_req_buf {
	ptupdate hdr;
	unsigned char pt[VU_DLPT_SIZE];
};

int parse_vu_dlpt_req(int argc, char **argv, struct vu_dlpt_params *params)
{
	int has_input = 0;
	int i;

	memset(params, 0, sizeof(*params));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
			params->input_file = argv[++i];
			has_input = 1;
		} else {
			fprintf(stderr, "Usage: %s\n", VU_DLPT_USAGE);
			return -1;
		}
	}

	if (!has_input) {
		fprintf(stderr, "Usage: %s\n", VU_DLPT_USAGE);
		fprintf(stderr,
			"  --input  partition table image (binary, exactly %u bytes)\n",
			VU_DLPT_SIZE);
		return -1;
	}

	return 0;
}

static int ptupdate_send(struct cxlmi_endpoint *ep,
			 const unsigned char *pt, size_t pt_len)
{
	struct ptupdate_req_buf req_buf;
	size_t req_sz;

	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.hdr.vuCmdId = PTUPDATE;
	req_buf.hdr.in_sz = (u32)pt_len;
	req_buf.hdr.out_sz = 0;
	memcpy(req_buf.pt, pt, pt_len);
	req_sz = offsetof(struct ptupdate_req_buf, pt) + pt_len;

	return cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
					 &req_buf, req_sz, NULL, 0);
}

int cmd_vu_dlpt(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct vu_dlpt_params params;
	unsigned char pt[VU_DLPT_SIZE];
	FILE *fp;
	long file_size;
	int rc, lock_rc;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s\n", VU_DLPT_USAGE);
		return -1;
	}

	rc = parse_vu_dlpt_req(argc - 1, argv + 1, &params);
	if (rc)
		return rc;

	fp = fopen(params.input_file, "rb");
	if (!fp) {
		perror("vu-dlpt: fopen");
		return -1;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		perror("vu-dlpt: fseek");
		fclose(fp);
		return -1;
	}
	file_size = ftell(fp);
	if (file_size != VU_DLPT_SIZE) {
		fprintf(stderr,
			"vu-dlpt: PT image must be exactly %u bytes, actual=%ld\n",
			VU_DLPT_SIZE, file_size);
		fclose(fp);
		return -1;
	}
	if (fseek(fp, 0, SEEK_SET) != 0 ||
	    fread(pt, 1, VU_DLPT_SIZE, fp) != VU_DLPT_SIZE) {
		fprintf(stderr, "vu-dlpt: read PT image failed\n");
		fclose(fp);
		return -1;
	}
	fclose(fp);

	rc = vu_mb_unlock(ep);
	if (rc)
		return rc;

	rc = ptupdate_send(ep, pt, VU_DLPT_SIZE);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "vu-dlpt failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "vu-dlpt ioctl failed\n");
	}

	lock_rc = vu_mb_lock(ep);
	if (rc == 0)
		rc = lock_rc;

	if (rc == 0)
		printf("vu-dlpt OK: file=%s order=backup->primary\n",
		       params.input_file);

	return rc;
}
