// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: VU DLCFG command (OPCODE_VU=0xCC53, vuCmdId=DLCFG=0x07).
 *
 * Downloads a configuration file to the device in chunked transfers via
 * the vendor-specific mailbox CCI path.
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

#define DLCFG_CHUNK_DEFAULT 1024
#define DLCFG_ALIGN         4

#define DLCFG_CFG_DEV       1
#define DLCFG_CFG_DDR       2

#define DLCFG_ACTION_FULL     0x0
#define DLCFG_ACTION_INIT     0x1
#define DLCFG_ACTION_CONTINUE 0x2
#define DLCFG_ACTION_END      0x3

#define VU_DLCFG_USAGE \
	"vu-dlcfg --input <file> --cfg-type <DEV|DDR> [--chunk-size <n>]"

static int parse_cfg_type(const char *name, uint32_t *cfg_type_out)
{
	if (strcmp(name, "DEV") == 0) {
		*cfg_type_out = DLCFG_CFG_DEV;
		return 0;
	}
	if (strcmp(name, "DDR") == 0) {
		*cfg_type_out = DLCFG_CFG_DDR;
		return 0;
	}
	fprintf(stderr,
		"vu-dlcfg: unknown --cfg-type '%s' (valid: DEV, DDR)\n", name);
	return -1;
}

static const char *cfg_type_name(uint32_t cfg_type)
{
	switch (cfg_type) {
	case DLCFG_CFG_DEV: return "DEV";
	case DLCFG_CFG_DDR: return "DDR";
	default:            return "?";
	}
}

static const char *dlcfg_action_name(uint32_t action)
{
	switch (action) {
	case DLCFG_ACTION_FULL:     return "FULL";
	case DLCFG_ACTION_INIT:     return "INIT";
	case DLCFG_ACTION_CONTINUE: return "CONTINUE";
	case DLCFG_ACTION_END:      return "END";
	default:                    return "?";
	}
}

static uint32_t dlcfg_pick_action(unsigned chunk_idx, unsigned num_chunks)
{
	if (num_chunks == 1)
		return DLCFG_ACTION_FULL;
	if (chunk_idx == 0)
		return DLCFG_ACTION_INIT;
	if (chunk_idx == num_chunks - 1)
		return DLCFG_ACTION_END;
	return DLCFG_ACTION_CONTINUE;
}

static size_t dlcfg_pad_len(size_t data_sz)
{
	return (data_sz + DLCFG_ALIGN - 1) & ~(size_t)(DLCFG_ALIGN - 1);
}

int vu_mb_unlock(struct cxlmi_endpoint *ep)
{
	vuunlock req = { .vuCmdId = VUUNLOCK, 0 };
	int rc = cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
					   &req, sizeof(req), NULL, 0);

	if (rc)
		fprintf(stderr, "vu-unlock failed (rc=%d)\n", rc);
	return rc;
}

int vu_mb_lock(struct cxlmi_endpoint *ep)
{
	vulock req = { .vuCmdId = VULOCK, 0 };
	int rc = cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
					   &req, sizeof(req), NULL, 0);

	if (rc)
		fprintf(stderr, "vu-lock failed (rc=%d)\n", rc);
	return rc;
}

int parse_vu_dlcfg_req(int argc, char **argv, struct vu_dlcfg_params *params)
{
	int has_input = 0, has_cfg_type = 0;
	int i;

	memset(params, 0, sizeof(*params));
	params->chunk_size = DLCFG_CHUNK_DEFAULT;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
			params->input_file = argv[++i];
			has_input = 1;
		} else if (strcmp(argv[i], "--cfg-type") == 0 && i + 1 < argc) {
			if (parse_cfg_type(argv[++i], &params->cfg_type) != 0)
				return -1;
			has_cfg_type = 1;
		} else if (strcmp(argv[i], "--chunk-size") == 0 && i + 1 < argc) {
			unsigned long chunk = strtoul(argv[++i], NULL, 0);

			if (chunk < DLCFG_ALIGN || chunk % DLCFG_ALIGN != 0) {
				fprintf(stderr,
					"vu-dlcfg: --chunk-size must be >= %d"
					" and a multiple of %d\n",
					DLCFG_ALIGN, DLCFG_ALIGN);
				return -1;
			}
			params->chunk_size = (uint32_t)chunk;
		} else {
			fprintf(stderr, "Usage: %s\n", VU_DLCFG_USAGE);
			return -1;
		}
	}

	if (!has_input || !has_cfg_type) {
		fprintf(stderr, "Usage: %s\n", VU_DLCFG_USAGE);
		fprintf(stderr,
			"  --input      configuration file (binary)\n"
			"  --cfg-type   DEV or DDR\n"
			"  --chunk-size chunk size (default %u, must be 4-byte aligned)\n",
			DLCFG_CHUNK_DEFAULT);
		return -1;
	}

	return 0;
}

int vu_dlcfg_file(struct cxlmi_endpoint *ep,
		  const struct vu_dlcfg_params *params,
		  vu_dlcfg_send_fn send_fn, void *send_ctx)
{
	dlcfg *req;
	FILE *fp;
	long file_size;
	unsigned num_chunks, chunk_idx;
	size_t req_buf_sz;
	int rc = -1;

	fp = fopen(params->input_file, "rb");
	if (!fp) {
		perror("vu-dlcfg: fopen");
		return -1;
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		perror("vu-dlcfg: fseek");
		goto out_fclose;
	}
	file_size = ftell(fp);
	if (file_size < 0) {
		perror("vu-dlcfg: ftell");
		goto out_fclose;
	}
	if (file_size == 0) {
		fprintf(stderr, "vu-dlcfg: input file is empty\n");
		goto out_fclose;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		perror("vu-dlcfg: fseek");
		goto out_fclose;
	}

	num_chunks = (unsigned)((file_size + params->chunk_size - 1) /
				params->chunk_size);
	req_buf_sz = offsetof(dlcfg, payload) + params->chunk_size;
	req = calloc(1, req_buf_sz);
	if (!req) {
		fprintf(stderr, "vu-dlcfg: out of memory\n");
		goto out_fclose;
	}

	fprintf(stderr,
		"vu-dlcfg: file=%s cfg-type=%s size=%ld chunk=%u chunks=%u\n",
		params->input_file, cfg_type_name(params->cfg_type), file_size,
		params->chunk_size, num_chunks);

	for (chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
		uint32_t byte_offset = chunk_idx * params->chunk_size;
		size_t data_sz, padded_sz, req_sz;
		uint32_t action;

		data_sz = (size_t)file_size - byte_offset;
		if (data_sz > params->chunk_size)
			data_sz = params->chunk_size;
		padded_sz = dlcfg_pad_len(data_sz);
		req_sz = offsetof(dlcfg, payload) + padded_sz;

		memset(req, 0, req_buf_sz);
		action = dlcfg_pick_action(chunk_idx, num_chunks);
		req->vuCmdId = DLCFG;
		req->status = 0;
		req->in_sz = (u32)req_sz;
		req->out_sz = 0;
		req->action = action;
		req->cfg_type = params->cfg_type;
		req->offset = byte_offset;
		req->arg4 = 0;

		if (fread(req->payload, 1, data_sz, fp) != data_sz) {
			fprintf(stderr, "vu-dlcfg: fread failed at offset %u\n",
				byte_offset);
			goto out_free;
		}

		fprintf(stderr,
			"vu-dlcfg: chunk %u/%u offset=%u bytes=%zu padded=%zu action=%s\n",
			chunk_idx + 1, num_chunks, byte_offset, data_sz, padded_sz,
			dlcfg_action_name(action));

		rc = send_fn(ep, send_ctx, req, req_sz);
		if (rc) {
			if (rc > 0)
				fprintf(stderr, "vu-dlcfg failed: %s\n",
					cxlmi_cmd_retcode_tostr(rc));
			else
				fprintf(stderr, "vu-dlcfg ioctl failed\n");
			goto out_free;
		}
	}

	printf("vu-dlcfg OK\n");
	printf("  File:        %s\n", params->input_file);
	printf("  Cfg Type:    %s\n", cfg_type_name(params->cfg_type));
	printf("  Total Bytes: %ld\n", file_size);
	printf("  Chunks:      %u (chunk-size=%u)\n",
	       num_chunks, params->chunk_size);

	rc = 0;

out_free:
	free(req);
out_fclose:
	fclose(fp);
	return rc;
}

static int vu_dlcfg_send_direct(struct cxlmi_endpoint *ep, void *ctx,
				void *req, size_t req_sz)
{
	(void)ctx;
	return cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
					 req, req_sz, NULL, 0);
}

int cmd_vu_dlcfg(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct vu_dlcfg_params params;
	int rc, lock_rc;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s\n", VU_DLCFG_USAGE);
		return -1;
	}

	rc = parse_vu_dlcfg_req(argc - 1, argv + 1, &params);
	if (rc)
		return rc;

	rc = vu_mb_unlock(ep);
	if (rc)
		return rc;

	rc = vu_dlcfg_file(ep, &params, vu_dlcfg_send_direct, NULL);

	lock_rc = vu_mb_lock(ep);
	if (rc == 0)
		rc = lock_rc;

	return rc;
}
