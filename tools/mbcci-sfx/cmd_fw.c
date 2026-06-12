// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Firmware Update commands (Opcodes 0200h / 0201h).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define CXL_FW_OFFSET_ALIGN  128
#define CXL_FW_CHUNK_DEFAULT 1024

#define CXL_FW_ACTION_FULL     0x0
#define CXL_FW_ACTION_INIT     0x1
#define CXL_FW_ACTION_CONTINUE 0x2
#define CXL_FW_ACTION_END      0x3

#define TRANSFER_FW_USAGE \
	"transfer-fw --input <file> --slot <n> [--chunk-size <n>]"

static void print_fw_rev(const char *label, const char *rev)
{
	char buf[0x10 + 1];
	size_t i;

	memcpy(buf, rev, 0x10);
	buf[0x10] = '\0';
	for (i = 0; i < 0x10; i++) {
		if ((unsigned char)buf[i] < 0x20 || (unsigned char)buf[i] > 0x7e)
			buf[i] = '.';
	}
	printf("%-20s %s\n", label, buf);
}

void print_get_fw_info(const struct cxlmi_cmd_get_fw_info_rsp *fw)
{
	uint8_t active_slot = fw->slot_info & 0x7;
	uint8_t staged_slot = (fw->slot_info >> 3) & 0x7;

	printf("Slots Supported:    %u\n", fw->slots_supported);
	printf("Slot Info:          0x%02x\n", fw->slot_info);
	printf("  Active Slot:      %u\n", active_slot);
	printf("  Staged Slot:      %u\n", staged_slot);
	printf("Capabilities:       0x%02x\n", fw->caps);

	if (fw->slots_supported >= 1)
		print_fw_rev("FW Rev Slot 1:", fw->fw_rev1);
	if (fw->slots_supported >= 2)
		print_fw_rev("FW Rev Slot 2:", fw->fw_rev2);
	if (fw->slots_supported >= 3)
		print_fw_rev("FW Rev Slot 3:", fw->fw_rev3);
	if (fw->slots_supported >= 4)
		print_fw_rev("FW Rev Slot 4:", fw->fw_rev4);
}

int cmd_get_fw_info(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_get_fw_info_rsp fw;
	int rc;

	(void)argc;
	(void)argv;

	memset(&fw, 0, sizeof(fw));
	rc = cxlmi_cmd_get_fw_info(ep, NULL, &fw);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get fw info failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get fw info ioctl failed\n");
		return rc;
	}

	print_get_fw_info(&fw);
	return 0;
}

static bool xfer_fw_ok(int rc)
{
	return rc == 0 || rc == CXLMI_RET_BACKGROUND;
}

static const char *xfer_action_name(uint8_t action)
{
	switch (action) {
	case CXL_FW_ACTION_FULL:     return "FULL";
	case CXL_FW_ACTION_INIT:     return "INIT";
	case CXL_FW_ACTION_CONTINUE: return "CONTINUE";
	case CXL_FW_ACTION_END:      return "END";
	default:                     return "?";
	}
}

static uint8_t xfer_pick_action(unsigned chunk_idx, unsigned num_chunks)
{
	if (num_chunks == 1)
		return CXL_FW_ACTION_FULL;
	if (chunk_idx == 0)
		return CXL_FW_ACTION_INIT;
	if (chunk_idx == num_chunks - 1)
		return CXL_FW_ACTION_END;
	return CXL_FW_ACTION_CONTINUE;
}

int parse_transfer_fw_req(int argc, char **argv,
			  struct transfer_fw_params *params)
{
	int has_input = 0, has_slot = 0;
	int i;

	memset(params, 0, sizeof(*params));
	params->chunk_size = CXL_FW_CHUNK_DEFAULT;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
			params->input_file = argv[++i];
			has_input = 1;
		} else if (strcmp(argv[i], "--slot") == 0 && i + 1 < argc) {
			unsigned long slot = strtoul(argv[++i], NULL, 0);

			if (slot < 1 || slot > 255) {
				fprintf(stderr,
					"transfer-fw: --slot must be 1-255\n");
				return -1;
			}
			params->slot = (uint8_t)slot;
			has_slot = 1;
		} else if (strcmp(argv[i], "--chunk-size") == 0 && i + 1 < argc) {
			unsigned long chunk = strtoul(argv[++i], NULL, 0);

			if (chunk < CXL_FW_OFFSET_ALIGN ||
			    chunk % CXL_FW_OFFSET_ALIGN != 0) {
				fprintf(stderr,
					"transfer-fw: --chunk-size must be >= %d"
					" and a multiple of %d\n",
					CXL_FW_OFFSET_ALIGN, CXL_FW_OFFSET_ALIGN);
				return -1;
			}
			params->chunk_size = (uint32_t)chunk;
		} else {
			fprintf(stderr, "Usage: %s\n", TRANSFER_FW_USAGE);
			return -1;
		}
	}

	if (!has_input || !has_slot) {
		fprintf(stderr, "Usage: %s\n", TRANSFER_FW_USAGE);
		return -1;
	}

	return 0;
}

int transfer_fw_file(struct cxlmi_endpoint *ep,
		     const struct transfer_fw_params *params,
		     transfer_fw_send_fn send_fn, void *send_ctx)
{
	struct cxlmi_cmd_transfer_fw_req *req;
	FILE *fp;
	long file_size;
	unsigned num_chunks, chunk_idx;
	size_t req_buf_sz;
	int rc = -1, bg_started = 0;

	fp = fopen(params->input_file, "rb");
	if (!fp) {
		perror("transfer-fw: fopen");
		return -1;
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		perror("transfer-fw: fseek");
		goto out_fclose;
	}
	file_size = ftell(fp);
	if (file_size < 0) {
		perror("transfer-fw: ftell");
		goto out_fclose;
	}
	if (file_size == 0) {
		fprintf(stderr, "transfer-fw: input file is empty\n");
		goto out_fclose;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		perror("transfer-fw: fseek");
		goto out_fclose;
	}

	num_chunks = (unsigned)((file_size + params->chunk_size - 1) /
				params->chunk_size);
	req_buf_sz = sizeof(*req) + params->chunk_size;
	req = calloc(1, req_buf_sz);
	if (!req) {
		fprintf(stderr, "transfer-fw: out of memory\n");
		goto out_fclose;
	}

	fprintf(stderr,
		"transfer-fw: file=%s slot=%u size=%ld chunk=%u chunks=%u\n",
		params->input_file, params->slot, file_size,
		params->chunk_size, num_chunks);

	for (chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
		uint32_t byte_offset = chunk_idx * params->chunk_size;
		size_t data_sz;
		uint8_t action;

		data_sz = (size_t)file_size - byte_offset;
		if (data_sz > params->chunk_size)
			data_sz = params->chunk_size;

		memset(req, 0, req_buf_sz);
		action = xfer_pick_action(chunk_idx, num_chunks);
		req->action = action;
		req->slot = params->slot;
		req->offset = byte_offset / CXL_FW_OFFSET_ALIGN;

		if (fread(req->data, 1, data_sz, fp) != data_sz) {
			fprintf(stderr, "transfer-fw: fread failed at offset %u\n",
				byte_offset);
			goto out_free;
		}

		fprintf(stderr,
			"transfer-fw: chunk %u/%u offset=%u bytes=%zu action=%s\n",
			chunk_idx + 1, num_chunks, byte_offset, data_sz,
			xfer_action_name(action));

		rc = send_fn(ep, send_ctx, req, data_sz);
		if (!xfer_fw_ok(rc)) {
			if (rc > 0)
				fprintf(stderr, "transfer-fw failed: %s\n",
					cxlmi_cmd_retcode_tostr(rc));
			else
				fprintf(stderr, "transfer-fw ioctl failed\n");
			goto out_free;
		}
		if (rc == CXLMI_RET_BACKGROUND)
			bg_started = 1;
	}

	printf("Transfer FW OK\n");
	printf("  File:        %s\n", params->input_file);
	printf("  Slot:        %u\n", params->slot);
	printf("  Total Bytes: %ld\n", file_size);
	printf("  Chunks:      %u (chunk-size=%u)\n",
	       num_chunks, params->chunk_size);
	if (bg_started)
		printf("  Note: background operation started on device"
		       " (use activate-fw separately)\n");

	rc = 0;

out_free:
	free(req);
out_fclose:
	fclose(fp);
	return rc;
}

static int xfer_send_direct(struct cxlmi_endpoint *ep, void *ctx,
			    struct cxlmi_cmd_transfer_fw_req *req,
			    size_t data_sz)
{
	(void)ctx;
	return cxlmi_cmd_transfer_fw(ep, NULL, req, data_sz);
}

int cmd_transfer_fw(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct transfer_fw_params params;
	int rc;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s\n", TRANSFER_FW_USAGE);
		return -1;
	}

	rc = parse_transfer_fw_req(argc - 1, argv + 1, &params);
	if (rc)
		return rc;

	return transfer_fw_file(ep, &params, xfer_send_direct, NULL);
}

#define CXL_FW_ACTIVATE_ONLINE  0
#define CXL_FW_ACTIVATE_OFFLINE 1

#define ACTIVATE_FW_USAGE \
	"activate-fw --slot <n> [--action online|offline]"

static bool activate_fw_ok(int rc)
{
	return rc == 0 || rc == CXLMI_RET_BACKGROUND;
}

static const char *activate_action_name(uint8_t action)
{
	switch (action) {
	case CXL_FW_ACTIVATE_ONLINE:  return "online";
	case CXL_FW_ACTIVATE_OFFLINE: return "offline";
	default:                      return "custom";
	}
}

void print_activate_fw_result(const struct cxlmi_cmd_activate_fw_req *req,
			      int rc)
{
	printf("Activate FW OK\n");
	printf("  Slot:   %u\n", req->slot);
	printf("  Action: %s (%u)\n", activate_action_name(req->action),
	       req->action);
	if (rc == CXLMI_RET_BACKGROUND)
		printf("  Note: background operation started on device\n");
}

int parse_activate_fw_req(int argc, char **argv,
			  struct cxlmi_cmd_activate_fw_req *req)
{
	int has_slot = 0, has_action = 0;
	int i;

	memset(req, 0, sizeof(*req));
	req->action = CXL_FW_ACTIVATE_OFFLINE;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--slot") == 0 && i + 1 < argc) {
			unsigned long slot = strtoul(argv[++i], NULL, 0);

			if (slot < 1 || slot > 255) {
				fprintf(stderr,
					"activate-fw: --slot must be 1-255\n");
				return -1;
			}
			req->slot = (uint8_t)slot;
			has_slot = 1;
		} else if (strcmp(argv[i], "--action") == 0 && i + 1 < argc) {
			const char *action = argv[++i];

			if (strcmp(action, "online") == 0) {
				req->action = CXL_FW_ACTIVATE_ONLINE;
			} else if (strcmp(action, "offline") == 0) {
				req->action = CXL_FW_ACTIVATE_OFFLINE;
			} else {
				unsigned long val = strtoul(action, NULL, 0);

				if (val > 255) {
					fprintf(stderr,
						"activate-fw: --action must be"
						" online, offline, or 0-255\n");
					return -1;
				}
				req->action = (uint8_t)val;
			}
			has_action = 1;
		} else {
			fprintf(stderr, "Usage: %s\n", ACTIVATE_FW_USAGE);
			return -1;
		}
	}

	if (!has_slot) {
		fprintf(stderr, "Usage: %s\n", ACTIVATE_FW_USAGE);
		return -1;
	}

	(void)has_action;
	return 0;
}

int cmd_activate_fw(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_activate_fw_req req;
	int rc;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s\n", ACTIVATE_FW_USAGE);
		return -1;
	}

	rc = parse_activate_fw_req(argc - 1, argv + 1, &req);
	if (rc)
		return rc;

	rc = cxlmi_cmd_activate_fw(ep, NULL, &req);
	if (!activate_fw_ok(rc)) {
		if (rc > 0)
			fprintf(stderr, "activate fw failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "activate fw ioctl failed\n");
		return rc;
	}

	print_activate_fw_result(&req, rc);
	return 0;
}
