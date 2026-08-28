// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: VU Download FW Union command
 * (OPCODE_VU=0xCC53, vuCmdId=FWDOWNLOAD=0x0241).
 *
 * Session-based, chunked firmware image download that mirrors
 * fw_download_union() in cxl_test_tool's cxl_vu_download.cpp:
 *
 *   BEGIN(action=0|5)  -> device allocates a session and reports the
 *                         (state, image, offset, length) it needs next.
 *   DATA(action=1)     -> host supplies the requested chunk, device
 *                         accepts it and advances accepted_sequence.
 *   QUERY(action=2)    -> host polls while device is PREPARING/WRITING.
 *   FINISH(action=3)   -> host commits once device reports
 *                         READY_TO_FINISH.
 *   ABORT(action=4)    -> host cancels an in-flight session on error.
 *
 * image=1 (SOC_RT_BL2) targets the inactive partition resolved from the
 * device's own partition table; image=255 (UNION) requires an explicit
 * --partition A|B.
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
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define FW_DLUNION_VERSION          1u
#define FW_DLUNION_CHUNK_SIZE       2016u
#define FW_DLUNION_BEGIN_RETRY_MS   35000u
#define FW_DLUNION_OP_TIMEOUT_MS    300000u
#define FW_DLUNION_MIN_IMAGE_SIZE   116

enum fwdownload_action {
	FWDOWNLOAD_ACTION_BEGIN = 0,
	FWDOWNLOAD_ACTION_DATA,
	FWDOWNLOAD_ACTION_QUERY,
	FWDOWNLOAD_ACTION_FINISH,
	FWDOWNLOAD_ACTION_ABORT,
	FWDOWNLOAD_ACTION_BEGIN_SOC_RT,
};

enum fwdownload_state {
	FWDOWNLOAD_STATE_IDLE = 0,
	FWDOWNLOAD_STATE_NEED_DATA,
	FWDOWNLOAD_STATE_PREPARING,
	FWDOWNLOAD_STATE_WRITING,
	FWDOWNLOAD_STATE_READY_TO_FINISH,
	FWDOWNLOAD_STATE_COMPLETE,
	FWDOWNLOAD_STATE_FAILED,
	FWDOWNLOAD_STATE_ABORTING,
	FWDOWNLOAD_STATE_ABORTED,
};

enum fwdownload_result {
	FWDOWNLOAD_RESULT_OK = 0,
	FWDOWNLOAD_RESULT_INVALID_ACTION,
	FWDOWNLOAD_RESULT_BUSY,
};

#define VU_DLUNION_USAGE \
	"vu-dlunion --input <file> [--image <union|bl2>] [--partition <A|B>]"

struct fwdownload_req_buf {
	fwdownload hdr;
	unsigned char payload[FW_DLUNION_CHUNK_SIZE];
};

static const char *dlunion_image_name(uint32_t image)
{
	switch (image) {
	case VU_DLUNION_IMAGE_SOC_RT_BL2: return "SOC_RT_BL2";
	case VU_DLUNION_IMAGE_UNION:      return "UNION";
	default:                          return "?";
	}
}

static const char *dlunion_partition_name(uint32_t partition)
{
	switch (partition) {
	case VU_DLUNION_PARTITION_A: return "A";
	case VU_DLUNION_PARTITION_B: return "B";
	default:                     return "-";
	}
}

static const char *dlunion_state_name(uint32_t state)
{
	switch (state) {
	case FWDOWNLOAD_STATE_IDLE:            return "IDLE";
	case FWDOWNLOAD_STATE_NEED_DATA:       return "NEED_DATA";
	case FWDOWNLOAD_STATE_PREPARING:       return "PREPARING";
	case FWDOWNLOAD_STATE_WRITING:         return "WRITING";
	case FWDOWNLOAD_STATE_READY_TO_FINISH: return "READY_TO_FINISH";
	case FWDOWNLOAD_STATE_COMPLETE:        return "COMPLETE";
	case FWDOWNLOAD_STATE_FAILED:          return "FAILED";
	case FWDOWNLOAD_STATE_ABORTING:        return "ABORTING";
	case FWDOWNLOAD_STATE_ABORTED:         return "ABORTED";
	default:                               return "UNKNOWN";
	}
}

static const char *dlunion_fw_image_name(uint32_t image_index)
{
	switch (image_index) {
	case UINT32_MAX: return "METADATA";
	case 0u:         return "CALIPTRA_FMC_RT";
	case 1u:         return "SOC_MANIFEST";
	case 2u:         return "MCU_RT";
	case 3u:         return "DDR_CONFIG";
	case 4u:         return "SOC_RT";
	default:         return "UNKNOWN";
	}
}

static int dlunion_now_ms(uint64_t *now_ms)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return -1;
	*now_ms = (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
	return 0;
}

/*
 * Sends one FWDOWNLOAD action, unlocking/locking the VU mailbox around the
 * ioctl (matching cxl_test_tool's fw_download_send(), which re-does
 * vuUnlock/vuLock per call).
 *
 * Returns 0 on success, a positive fwdownload_result value if the device
 * rejected the action at the application level (e.g. BUSY), or -1 on a
 * lower-level (ioctl/CCI/protocol) failure.
 */
static int fwdownload_send(struct cxlmi_endpoint *ep, uint32_t action,
			   uint32_t session_or_partition,
			   uint32_t package_offset, uint32_t sequence,
			   const unsigned char *payload, uint32_t payload_sz,
			   fwdownload_output *rsp)
{
	struct fwdownload_req_buf req_buf;
	size_t req_sz;
	int rc, lock_rc;
	int is_begin = action == FWDOWNLOAD_ACTION_BEGIN ||
		       action == FWDOWNLOAD_ACTION_BEGIN_SOC_RT;

	memset(rsp, 0, sizeof(*rsp));

	if (payload_sz > FW_DLUNION_CHUNK_SIZE || (payload_sz && !payload))
		return -1;

	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.hdr.vuCmdId = FWDOWNLOAD;
	req_buf.hdr.in_sz = (u32)payload_sz;
	req_buf.hdr.out_sz = (u32)sizeof(*rsp);
	req_buf.hdr.action = action;
	req_buf.hdr.session_or_partition = session_or_partition;
	req_buf.hdr.package_offset = package_offset;
	req_buf.hdr.sequence = sequence;
	if (payload_sz)
		memcpy(req_buf.payload, payload, payload_sz);
	req_sz = offsetof(struct fwdownload_req_buf, payload) + payload_sz;

	rc = vu_mb_unlock(ep);
	if (rc)
		return -1;

	rc = cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
				       &req_buf, req_sz, rsp, sizeof(*rsp));

	lock_rc = vu_mb_lock(ep);

	if (rc) {
		if (rc > 0)
			fprintf(stderr, "vu-dlunion: action=%u failed: %s\n",
				action, cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "vu-dlunion: action=%u ioctl failed\n",
				action);
		return -1;
	}
	if (lock_rc)
		return -1;

	if (rsp->version != FW_DLUNION_VERSION) {
		fprintf(stderr,
			"vu-dlunion: protocol version=%u expected=%u\n",
			rsp->version, FW_DLUNION_VERSION);
		return -1;
	}
	if (!is_begin && rsp->session_id != session_or_partition) {
		fprintf(stderr,
			"vu-dlunion: response session=%u expected=%u\n",
			rsp->session_id, session_or_partition);
		return -1;
	}
	if (rsp->result != FWDOWNLOAD_RESULT_OK) {
		if (!is_begin || rsp->result != FWDOWNLOAD_RESULT_BUSY)
			fprintf(stderr,
				"vu-dlunion: state=%u result=%u session=%u\n",
				rsp->state, rsp->result, rsp->session_id);
		return (int)rsp->result;
	}

	return 0;
}

int vu_dlunion_download(struct cxlmi_endpoint *ep,
			const struct vu_dlunion_params *params)
{
	FILE *fp = NULL;
	long file_size;
	fwdownload_output rsp;
	unsigned char chunk[FW_DLUNION_CHUNK_SIZE];
	uint32_t session_id = 0;
	uint32_t last_state = UINT32_MAX;
	uint32_t last_image = UINT32_MAX;
	uint32_t busy_state = UINT32_MAX;
	uint64_t busy_since_ms = 0;
	uint32_t last_data_image = UINT32_MAX;
	uint32_t last_data_offset = UINT32_MAX;
	uint32_t last_data_length = 0;
	uint64_t begin_start_ms = 0;
	int have_last_data_range = 0;
	int begin_wait_reported = 0;
	int abort_needed = 0;
	uint32_t begin_action, begin_partition;
	int rc = -1;

	begin_action = params->image == VU_DLUNION_IMAGE_SOC_RT_BL2 ?
		FWDOWNLOAD_ACTION_BEGIN_SOC_RT : FWDOWNLOAD_ACTION_BEGIN;
	begin_partition = params->image == VU_DLUNION_IMAGE_UNION ?
		params->partition : VU_DLUNION_PARTITION_NONE;

	fp = fopen(params->input_file, "rb");
	if (!fp) {
		perror("vu-dlunion: fopen");
		return -1;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		perror("vu-dlunion: fseek");
		goto out;
	}
	file_size = ftell(fp);
	if (file_size < FW_DLUNION_MIN_IMAGE_SIZE) {
		fprintf(stderr, "vu-dlunion: invalid image size=%ld\n",
			file_size);
		goto out;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		perror("vu-dlunion: fseek");
		goto out;
	}

	if (dlunion_now_ms(&begin_start_ms) != 0) {
		perror("vu-dlunion: clock_gettime");
		goto out;
	}

	for (;;) {
		int begin_rc = fwdownload_send(ep, begin_action,
					       begin_partition,
					       (uint32_t)file_size,
					       FW_DLUNION_CHUNK_SIZE,
					       NULL, 0, &rsp);
		uint64_t now_ms;

		if (begin_rc == 0)
			break;
		if (begin_rc != FWDOWNLOAD_RESULT_BUSY)
			goto out;

		if (dlunion_now_ms(&now_ms) != 0) {
			perror("vu-dlunion: clock_gettime");
			goto out;
		}
		if (now_ms - begin_start_ms >= FW_DLUNION_BEGIN_RETRY_MS) {
			fprintf(stderr,
				"vu-dlunion: device remained busy for %u ms\n",
				FW_DLUNION_BEGIN_RETRY_MS);
			goto out;
		}
		if (!begin_wait_reported) {
			printf("vu-dlunion: device busy, waiting for stale session recovery\n");
			begin_wait_reported = 1;
		}
		usleep(1000u * 1000u);
	}

	if (rsp.session_id == 0 || rsp.state != FWDOWNLOAD_STATE_NEED_DATA ||
	    rsp.total_bytes != (uint32_t)file_size) {
		fprintf(stderr,
			"vu-dlunion: invalid BEGIN response session=%u state=%u total=%u\n",
			rsp.session_id, rsp.state, rsp.total_bytes);
		goto out;
	}
	session_id = rsp.session_id;
	abort_needed = 1;

	printf("vu-dlunion: begin session=%u image=%s(%u) partition=%s file=%s size=%ld chunk=%u\n",
	       session_id, dlunion_image_name(params->image), params->image,
	       dlunion_partition_name(begin_partition), params->input_file,
	       file_size, FW_DLUNION_CHUNK_SIZE);

	for (;;) {
		if (rsp.total_bytes != (uint32_t)file_size ||
		    rsp.received_bytes > rsp.total_bytes) {
			fprintf(stderr,
				"vu-dlunion: invalid progress received=%u total=%u expected=%ld\n",
				rsp.received_bytes, rsp.total_bytes, file_size);
			goto out;
		}
		if (rsp.state != last_state || rsp.image_index != last_image) {
			printf("vu-dlunion: state=%s(%u) image=%s(%u) received=%u/%u next=0x%x+0x%x\n",
			       dlunion_state_name(rsp.state), rsp.state,
			       dlunion_fw_image_name(rsp.image_index),
			       rsp.image_index, rsp.received_bytes,
			       rsp.total_bytes, rsp.next_offset,
			       rsp.next_length);
			last_state = rsp.state;
			last_image = rsp.image_index;
		}

		if (rsp.state == FWDOWNLOAD_STATE_PREPARING ||
		    rsp.state == FWDOWNLOAD_STATE_WRITING ||
		    rsp.state == FWDOWNLOAD_STATE_ABORTING) {
			uint64_t now_ms;

			if (dlunion_now_ms(&now_ms) != 0) {
				perror("vu-dlunion: clock_gettime");
				goto out;
			}
			if (busy_state != rsp.state) {
				busy_state = rsp.state;
				busy_since_ms = now_ms;
			} else if (now_ms - busy_since_ms >=
				   FW_DLUNION_OP_TIMEOUT_MS) {
				fprintf(stderr,
					"vu-dlunion: state=%u timed out after %u ms\n",
					rsp.state, FW_DLUNION_OP_TIMEOUT_MS);
				goto out;
			}
		} else {
			busy_state = UINT32_MAX;
			busy_since_ms = 0;
		}

		switch (rsp.state) {
		case FWDOWNLOAD_STATE_NEED_DATA: {
			uint32_t sequence = rsp.accepted_sequence + 1u;
			uint32_t req_image = rsp.image_index;
			uint32_t req_offset = rsp.next_offset;
			uint32_t req_length = rsp.next_length;

			if (have_last_data_range &&
			    req_image == last_data_image &&
			    req_offset == last_data_offset &&
			    req_length == last_data_length) {
				fprintf(stderr,
					"vu-dlunion: device made no progress image=%u range=0x%x+0x%x\n",
					req_image, req_offset, req_length);
				goto out;
			}
			if (req_length == 0 || req_length > sizeof(chunk) ||
			    req_offset > (uint32_t)file_size ||
			    req_length > (uint32_t)file_size - req_offset) {
				fprintf(stderr,
					"vu-dlunion: invalid requested range=0x%x+0x%x\n",
					req_offset, req_length);
				goto out;
			}
			if (fseek(fp, req_offset, SEEK_SET) != 0 ||
			    fread(chunk, 1, req_length, fp) != req_length) {
				fprintf(stderr,
					"vu-dlunion: read failed at offset %u\n",
					req_offset);
				goto out;
			}
			if (fwdownload_send(ep, FWDOWNLOAD_ACTION_DATA,
					    session_id, req_offset, sequence,
					    chunk, req_length, &rsp) != 0)
				goto out;
			if (rsp.accepted_sequence != sequence) {
				fprintf(stderr,
					"vu-dlunion: sequence sent=%u accepted=%u\n",
					sequence, rsp.accepted_sequence);
				goto out;
			}
			last_data_image = req_image;
			last_data_offset = req_offset;
			last_data_length = req_length;
			have_last_data_range = 1;
			break;
		}

		case FWDOWNLOAD_STATE_PREPARING:
		case FWDOWNLOAD_STATE_WRITING:
		case FWDOWNLOAD_STATE_ABORTING: {
			uint32_t retry_ms = rsp.retry_after_ms == 0 ?
				10u : rsp.retry_after_ms;

			if (retry_ms > 1000u)
				retry_ms = 1000u;
			usleep(retry_ms * 1000u);
			if (fwdownload_send(ep, FWDOWNLOAD_ACTION_QUERY,
					    session_id, 0, 0, NULL, 0,
					    &rsp) != 0)
				goto out;
			break;
		}

		case FWDOWNLOAD_STATE_READY_TO_FINISH:
			if (fwdownload_send(ep, FWDOWNLOAD_ACTION_FINISH,
					    session_id, 0, 0, NULL, 0,
					    &rsp) != 0)
				goto out;
			break;

		case FWDOWNLOAD_STATE_COMPLETE:
			abort_needed = 0;
			rc = 0;
			printf("vu-dlunion OK: session=%u image=%s partition=%s transferred=%u bytes\n",
			       session_id, dlunion_image_name(params->image),
			       dlunion_partition_name(begin_partition),
			       rsp.received_bytes);
			goto out;

		case FWDOWNLOAD_STATE_FAILED:
		case FWDOWNLOAD_STATE_ABORTED:
			fprintf(stderr,
				"vu-dlunion: terminal state=%u result=%u\n",
				rsp.state, rsp.result);
			abort_needed = 0;
			goto out;

		default:
			fprintf(stderr, "vu-dlunion: unexpected state=%u\n",
				rsp.state);
			goto out;
		}
	}

out:
	if (rsp.state == FWDOWNLOAD_STATE_COMPLETE ||
	    rsp.state == FWDOWNLOAD_STATE_FAILED ||
	    rsp.state == FWDOWNLOAD_STATE_ABORTED)
		abort_needed = 0;
	if (abort_needed && session_id != 0) {
		fwdownload_output abort_rsp;

		(void)fwdownload_send(ep, FWDOWNLOAD_ACTION_ABORT, session_id,
				      0, 0, NULL, 0, &abort_rsp);
	}
	if (fp)
		fclose(fp);
	return rc;
}

int parse_vu_dlunion_req(int argc, char **argv,
			 struct vu_dlunion_params *params)
{
	int has_input = 0, has_image = 0, has_partition = 0;
	int i;

	memset(params, 0, sizeof(*params));
	params->image = VU_DLUNION_IMAGE_UNION;
	params->partition = VU_DLUNION_PARTITION_NONE;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
			params->input_file = argv[++i];
			has_input = 1;
		} else if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
			const char *v = argv[++i];

			if (!strcasecmp(v, "union") || !strcmp(v, "255"))
				params->image = VU_DLUNION_IMAGE_UNION;
			else if (!strcasecmp(v, "bl2") || !strcmp(v, "1"))
				params->image = VU_DLUNION_IMAGE_SOC_RT_BL2;
			else {
				fprintf(stderr,
					"vu-dlunion: unknown --image '%s' (valid: union, bl2)\n",
					v);
				return -1;
			}
			has_image = 1;
		} else if (strcmp(argv[i], "--partition") == 0 &&
			   i + 1 < argc) {
			const char *v = argv[++i];

			if (!strcasecmp(v, "A") || !strcmp(v, "1"))
				params->partition = VU_DLUNION_PARTITION_A;
			else if (!strcasecmp(v, "B") || !strcmp(v, "2"))
				params->partition = VU_DLUNION_PARTITION_B;
			else {
				fprintf(stderr,
					"vu-dlunion: unknown --partition '%s' (valid: A, B)\n",
					v);
				return -1;
			}
			has_partition = 1;
		} else {
			fprintf(stderr, "Usage: %s\n", VU_DLUNION_USAGE);
			return -1;
		}
	}

	if (!has_input) {
		fprintf(stderr, "Usage: %s\n", VU_DLUNION_USAGE);
		fprintf(stderr,
			"  --input      firmware/manifest image (binary)\n"
			"  --image      union (default, requires --partition) or bl2\n"
			"  --partition  A or B (image=union only)\n");
		return -1;
	}
	(void)has_image;

	if (params->image == VU_DLUNION_IMAGE_UNION && !has_partition) {
		fprintf(stderr,
			"vu-dlunion: --image union requires --partition A|B\n");
		return -1;
	}
	if (params->image == VU_DLUNION_IMAGE_SOC_RT_BL2 && has_partition) {
		fprintf(stderr,
			"vu-dlunion: --image bl2 must not use --partition\n");
		return -1;
	}

	return 0;
}

int cmd_vu_dlunion(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct vu_dlunion_params params;
	int rc;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s\n", VU_DLUNION_USAGE);
		return -1;
	}

	rc = parse_vu_dlunion_req(argc - 1, argv + 1, &params);
	if (rc)
		return rc;

	return vu_dlunion_download(ep, &params);
}
