// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: sdb-tunnel command group (Vendor-specific opcode 0xCCCC).
 *
 * Sends a tunnel-wrapped MCTP-CCI-format packet to the device via the
 * mailbox ioctl path.  The outer CCI command uses Vendor-specific opcode
 * 0xCCCC; the payload carries a tunnel header followed by an inner
 * cxlmi_cci_msg that the device's sideband-cci-handle task processes.
 *
 * Tunnel request payload layout:
 *   [ sdb_tunnel_req_hdr (4B) ]  id, target_type, command_size
 *   [ cxlmi_cci_msg      (12B)]  inner CCI request header + payload
 *
 * Tunnel response payload layout:
 *   [ sdb_tunnel_rsp_hdr (4B) ]  length, resv
 *   [ cxlmi_cci_msg      (12B)]  inner CCI response header + payload
 *
 * Currently supported inner commands:
 *   identify   Generic Component Identify (opcode 0x0001)
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define SDB_TUNNEL_OPCODE 0xCCCC

/*
 * Local mirror of cxlmi_cmd_fmapi_tunnel_command_req header fields.
 * The full request is this header immediately followed by a cxlmi_cci_msg.
 */
struct sdb_tunnel_req_hdr {
	uint8_t  id;           /* Port or LD ID — 0 for direct sideband target */
	uint8_t  target_type;  /* 0 = port/LD */
	uint16_t command_size; /* size of the embedded cxlmi_cci_msg (header + payload) */
} __attribute__((packed));

/*
 * Local mirror of cxlmi_cmd_fmapi_tunnel_command_rsp header fields.
 * The full response is this header immediately followed by a cxlmi_cci_msg.
 */
struct sdb_tunnel_rsp_hdr {
	uint16_t length; /* total byte count of the embedded message */
	uint16_t resv;
} __attribute__((packed));

/* ------------------------------------------------------------------ */
/* sdb-tunnel identify (inner opcode 0x0001)                          */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_identify(struct cxlmi_endpoint *ep)
{
	/*
	 * Request: tunnel header + cxlmi_cci_msg with no payload.
	 * Response: tunnel header + cxlmi_cci_msg + cxlmi_cmd_identify_rsp.
	 */
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_identify_rsp rsp;
	} __attribute__((packed)) rsp;

	int rc;

	memset(&req, 0, sizeof(req));
	req.hdr.id           = 0;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg); /* inner CCI msg, no payload */

	/* Inner MCTP-CCI request for Generic Component Identify (0x0001). */
	req.msg.command     = 0x01; /* IS_IDENTIFY */
	req.msg.command_set = 0x00; /* INFOSTAT   */
	/* category=0 (CXL_MCTP_CATEGORY_REQ), tag=0, pl_length=0 already zero */

	memset(&rsp, 0, sizeof(rsp));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel identify failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel identify ioctl failed\n");
		return rc;
	}

	if (rsp.msg.return_code != 0) {
		fprintf(stderr, "sdb-tunnel identify: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	printf("Vendor ID:          0x%04x\n", rsp.rsp.vendor_id);
	printf("Device ID:          0x%04x\n", rsp.rsp.device_id);
	printf("Subsys Vendor ID:   0x%04x\n", rsp.rsp.subsys_vendor_id);
	printf("Subsys ID:          0x%04x\n", rsp.rsp.subsys_id);
	printf("Serial Number:      0x%016llx\n",
	       (unsigned long long)rsp.rsp.serial_num);
	printf("Max Msg Size:       %u (2^%u bytes)\n",
	       rsp.rsp.max_msg_size, rsp.rsp.max_msg_size);
	printf("Component Type:     0x%02x\n", rsp.rsp.component_type);

	return 0;
}

/* ------------------------------------------------------------------ */
/* Dispatcher                                                          */
/* ------------------------------------------------------------------ */

int cmd_sdb_tunnel(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr,
			"Usage: sdb-tunnel <cci-cmd> [args...]\n"
			"  identify   Generic Component Identify (0x0001)\n");
		return -1;
	}

	if (strcmp(argv[1], "identify") == 0)
		return sdb_tunnel_identify(ep);

	fprintf(stderr, "sdb-tunnel: unknown cci-cmd '%s'\n", argv[1]);
	fprintf(stderr, "  supported: identify\n");
	return -1;
}
