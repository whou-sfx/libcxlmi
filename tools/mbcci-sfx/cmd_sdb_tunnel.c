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

static void dump_hex(const char *label, const void *buf, size_t len)
{
#if  0
	const uint8_t *p = buf;
	size_t i;

	fprintf(stderr, "%s [%zu bytes]:\n", label, len);
	for (i = 0; i < len; i++) {
		fprintf(stderr, " %02x", p[i]);
		if ((i & 0xf) == 0xf || i == len - 1)
			fputc('\n', stderr);
	}
#endif
}

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
/* Port name → ID mapping                                             */
/* ------------------------------------------------------------------ */

static const struct {
	const char *name;
	uint8_t     id;
} port_map[] = {
	{ "vmd0", 0 },
	{ "vmd1", 1 },
	{ "i3c",  2 },
};

/* Returns port id on success, -1 and prints error on unknown name. */
static int parse_port_id(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof(port_map) / sizeof(port_map[0]); i++) {
		if (strcmp(name, port_map[i].name) == 0)
			return port_map[i].id;
	}
	fprintf(stderr, "sdb-tunnel: unknown --port '%s' (valid: vmd0, vmd1, i3c)\n",
		name);
	return -1;
}

/* CXL r3.1 §8.2.9.3 / §8.2.9.4 — not yet in public api-types.h */
struct sdb_get_resp_msg_limit_rsp { uint8_t limit; } __attribute__((packed));
struct sdb_set_resp_msg_limit_req { uint8_t limit; } __attribute__((packed));
struct sdb_set_resp_msg_limit_rsp { uint8_t limit; } __attribute__((packed));

/* ------------------------------------------------------------------ */
/* sdb-tunnel identify (inner opcode 0x0001)                          */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_identify(struct cxlmi_endpoint *ep, int argc, char **argv)
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

	uint8_t port_id = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			fprintf(stderr,
				"Usage: sdb-tunnel identify [--port <vmd0|vmd1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg); /* inner CCI msg, no payload */

	/* Inner MCTP-CCI request for Generic Component Identify (0x0001). */
	req.msg.command     = 0x01; /* IS_IDENTIFY */
	req.msg.command_set = 0x00; /* INFOSTAT   */
	/* category=0 (CXL_MCTP_CATEGORY_REQ), tag=0, pl_length=0 already zero */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

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

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr, "sdb-tunnel identify: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	printf("Vendor ID:          0x%04x\n", rsp.rsp.vendor_id);
	printf("Device ID:          0x%04x\n", rsp.rsp.device_id);
	printf("Subsys Vendor ID:   0x%04x\n", rsp.rsp.subsys_vendor_id);
	printf("Subsys ID:          0x%04x\n", rsp.rsp.subsys_id);
	{
		char sn[sizeof(rsp.rsp.serial_num) + 1];
		size_t j;

		memcpy(sn, &rsp.rsp.serial_num, sizeof(rsp.rsp.serial_num));
		sn[sizeof(rsp.rsp.serial_num)] = '\0';
		for (j = 0; j < sizeof(rsp.rsp.serial_num); j++) {
			if ((unsigned char)sn[j] < 0x20 || (unsigned char)sn[j] > 0x7e)
				sn[j] = '.';
		}
		printf("Serial Number:      %s\n", sn);
	}
	printf("Max Msg Size:       %u (2^%u bytes)\n",
	       rsp.rsp.max_msg_size, rsp.rsp.max_msg_size);
	printf("Component Type:     0x%02x\n", rsp.rsp.component_type);

	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-resp-msg-limit (inner opcode 0x0003)                */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_get_resp_msg_limit(struct cxlmi_endpoint *ep,
					 int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr         hdr;
		struct cxlmi_cci_msg              msg;
		struct sdb_get_resp_msg_limit_rsp rsp;
	} __attribute__((packed)) rsp;

	uint8_t port_id = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			fprintf(stderr,
				"Usage: sdb-tunnel get-resp-msg-limit [--port <vmd0|vmd1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x03; /* GET_RESPONSE_MSG_LIMIT */
	req.msg.command_set = 0x00; /* INFOSTAT */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel get-resp-msg-limit failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel get-resp-msg-limit ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel get-resp-msg-limit: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	printf("Resp Msg Limit: %u\n", rsp.rsp.limit);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel set-resp-msg-limit (inner opcode 0x0004)                */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_set_resp_msg_limit(struct cxlmi_endpoint *ep,
					 int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr         hdr;
		struct cxlmi_cci_msg              msg;
		struct sdb_set_resp_msg_limit_req payload;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr         hdr;
		struct cxlmi_cci_msg              msg;
		struct sdb_set_resp_msg_limit_rsp rsp;
	} __attribute__((packed)) rsp;

	uint8_t port_id = 0;
	int has_limit = 0, rc, i;
	unsigned long limit_val = 0;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
			limit_val = strtoul(argv[++i], NULL, 0);
			if (limit_val > 255) {
				fprintf(stderr,
					"sdb-tunnel set-resp-msg-limit: --limit must be 0-255\n");
				return -1;
			}
			has_limit = 1;
		} else {
			fprintf(stderr,
				"Usage: sdb-tunnel set-resp-msg-limit"
				" [--port <vmd0|vmd1|i3c>] --limit <0-255>\n");
			return -1;
		}
	}

	if (!has_limit) {
		fprintf(stderr,
			"Usage: sdb-tunnel set-resp-msg-limit"
			" [--port <vmd0|vmd1|i3c>] --limit <0-255>\n");
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg) + sizeof(req.payload);

	req.msg.command     = 0x04; /* SET_RESPONSE_MSG_LIMIT */
	req.msg.command_set = 0x00; /* INFOSTAT */
	req.msg.pl_length[0] = sizeof(req.payload); /* 1 byte, fits in pl_length[0] */

	req.payload.limit = (uint8_t)limit_val;

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel set-resp-msg-limit failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel set-resp-msg-limit ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel set-resp-msg-limit: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	printf("Resp Msg Limit set: %u\n", rsp.rsp.limit);
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
			"  identify          [--port <vmd0|vmd1|i3c>]             Generic Component Identify (0x0001)\n"
			"  get-resp-msg-limit[--port <vmd0|vmd1|i3c>]             Get Response Message Limit (0x0003)\n"
			"  set-resp-msg-limit[--port <vmd0|vmd1|i3c>] --limit <n> Set Response Message Limit (0x0004)\n");
		return -1;
	}

	if (strcmp(argv[1], "identify") == 0)
		return sdb_tunnel_identify(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-resp-msg-limit") == 0)
		return sdb_tunnel_get_resp_msg_limit(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "set-resp-msg-limit") == 0)
		return sdb_tunnel_set_resp_msg_limit(ep, argc - 2, argv + 2);

	fprintf(stderr, "sdb-tunnel: unknown cci-cmd '%s'\n", argv[1]);
	fprintf(stderr, "  supported: identify, get-resp-msg-limit, set-resp-msg-limit\n");
	return -1;
}
