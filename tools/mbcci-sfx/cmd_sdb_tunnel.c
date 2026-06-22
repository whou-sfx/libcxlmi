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
 *   identify          Generic Component Identify (opcode 0x0001)
 *   identify_memdev   Identify Memory Device (opcode 0x4000)
 *   get-partition     Get Partition Info (opcode 0x4100)
 *   set-partition     Set Partition Info (opcode 0x4101)
 *   get-fw-info       Get FW Info (opcode 0x0200)
 *   transfer-fw       Transfer FW (opcode 0x0201)
 *   activate-fw       Activate FW (opcode 0x0202)
 *   get-health-info   Get Health Info (opcode 0x4200)
 *   get-alert-config  Get Alert Configuration (opcode 0x4201)
 *   set-alert-config  Set Alert Configuration (opcode 0x4202)
 *   get-sld-qos-ctrl  Get SLD QoS Control (opcode 0x4700)
 *   set-sld-qos-ctrl  Set SLD QoS Control (opcode 0x4701)
 *   get-sld-qos-status Get SLD QoS Status (opcode 0x4702)
 *   fm-get-ld-info    FM Get LD Info (opcode 0x5400)
 *   fm-get-ld-alloc   FM Get LD Allocations (opcode 0x5401)
 *   get-supported-logs Get Supported Logs (opcode 0x0400)
 *   get-log           Get Log (opcode 0x0401)
 *   get-supported-feat Get Supported Features (opcode 0x0500)
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ccan/endian/endian.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define SDB_TUNNEL_OPCODE     0xCCCC
#define SDB_MAX_EVENT_RECORDS 20       /* matches CXLMI_MAX_SUPPORTED_EVENT_RECORDS */
#define SDB_RSP_FLAG_OVERFLOW    (1 << 0)
#define SDB_RSP_FLAG_MORE_EVENTS (1 << 1)

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
	{ "vdm0", 0 },
	{ "vdm1", 1 },
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
	fprintf(stderr, "sdb-tunnel: unknown --port '%s' (valid: vdm0, vdm1, i3c)\n",
		name);
	return -1;
}

/* ------------------------------------------------------------------ */
/* Event log name → value mapping                                     */
/* ------------------------------------------------------------------ */

static const struct {
	const char *name;
	uint8_t     value;
} event_log_map[] = {
	{ "info",    0 },
	{ "warn",    1 },
	{ "failure", 2 },
	{ "fatal",   3 },
	{ "dcd",     4 },
};

/* Returns log value on success, -1 and prints error on unknown name. */
static int parse_event_log_local(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof(event_log_map) / sizeof(event_log_map[0]); i++) {
		if (strcmp(name, event_log_map[i].name) == 0)
			return event_log_map[i].value;
	}
	fprintf(stderr,
		"sdb-tunnel: unknown --log '%s' (valid: info, warn, failure, fatal, dcd)\n",
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
				"Usage: sdb-tunnel identify [--port <vdm0|vdm1|i3c>]\n");
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
/* sdb-tunnel identify_memdev (inner opcode 0x4000)                   */
/* ------------------------------------------------------------------ */

static void sdb_parse_memdev_identify_rsp(
	const struct cxlmi_cmd_memdev_identify_rsp *wire,
	struct cxlmi_cmd_memdev_identify_rsp *host)
{
	memset(host, 0, sizeof(*host));
	memcpy(host->fw_revision, wire->fw_revision, sizeof(wire->fw_revision));
	host->total_capacity = le64_to_cpu(wire->total_capacity);
	host->volatile_capacity = le64_to_cpu(wire->volatile_capacity);
	host->persistent_capacity = le64_to_cpu(wire->persistent_capacity);
	host->partition_align = le64_to_cpu(wire->partition_align);
	host->info_event_log_size = le16_to_cpu(wire->info_event_log_size);
	host->warning_event_log_size = le16_to_cpu(wire->warning_event_log_size);
	host->failure_event_log_size = le16_to_cpu(wire->failure_event_log_size);
	host->fatal_event_log_size = le16_to_cpu(wire->fatal_event_log_size);
	host->lsa_size = le32_to_cpu(wire->lsa_size);
	memcpy(host->poison_list_max_mer, wire->poison_list_max_mer,
	       sizeof(wire->poison_list_max_mer));
	host->inject_poison_limit = le16_to_cpu(wire->inject_poison_limit);
	host->poison_caps = wire->poison_caps;
	host->qos_telemetry_caps = wire->qos_telemetry_caps;
#ifndef SUPPORT_CXL_2_0
	host->dc_event_log_size = le16_to_cpu(wire->dc_event_log_size);
#endif
}

static int sdb_tunnel_identify_memdev(struct cxlmi_endpoint *ep,
				      int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_memdev_identify_rsp rsp;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_memdev_identify_rsp id;
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
				"Usage: sdb-tunnel identify_memdev [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x00; /* MEMORY_DEVICE */
	req.msg.command_set = 0x40; /* IDENTIFY       */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel identify_memdev failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel identify_memdev ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel identify_memdev: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	sdb_parse_memdev_identify_rsp(&rsp.rsp, &id);
	print_memdev_identify(&id);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-partition (inner opcode 0x4100)                     */
/* ------------------------------------------------------------------ */

static void sdb_parse_memdev_get_partition_rsp(
	const struct cxlmi_cmd_memdev_get_partition_info_rsp *wire,
	struct cxlmi_cmd_memdev_get_partition_info_rsp *host)
{
	memset(host, 0, sizeof(*host));
	host->active_vmem = le64_to_cpu(wire->active_vmem);
	host->active_pmem = le64_to_cpu(wire->active_pmem);
	host->next_vmem = le64_to_cpu(wire->next_vmem);
	host->next_pmem = le64_to_cpu(wire->next_pmem);
}

static int sdb_tunnel_get_partition(struct cxlmi_endpoint *ep,
				    int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_memdev_get_partition_info_rsp rsp;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_memdev_get_partition_info_rsp pi;
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
				"Usage: sdb-tunnel get-partition [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x00; /* GET_PARTITION_INFO */
	req.msg.command_set = 0x41; /* CCLS               */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel get-partition failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel get-partition ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel get-partition: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	sdb_parse_memdev_get_partition_rsp(&rsp.rsp, &pi);
	print_memdev_partition_info(&pi);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel set-partition (inner opcode 0x4101)                     */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_set_partition(struct cxlmi_endpoint *ep,
				    int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr                         hdr;
		struct cxlmi_cci_msg                              msg;
		struct cxlmi_cmd_memdev_set_partition_info_req    payload;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_memdev_set_partition_info_req pi;
	char *part_argv[16];
	int part_argc = 0;
	uint8_t port_id = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (part_argc >= (int)(sizeof(part_argv) / sizeof(part_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel set-partition: too many arguments\n");
				return -1;
			}
			part_argv[part_argc++] = argv[i];
		}
	}

	rc = parse_set_partition_req(part_argc, part_argv, &pi);
	if (rc)
		return rc;

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.payload));

	req.msg.command     = 0x01; /* SET_PARTITION_INFO */
	req.msg.command_set = 0x41; /* CCLS               */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] = (uint8_t)((sizeof(req.payload) >> 8) & 0xff);

	req.payload.volatile_capacity = cpu_to_le64(pi.volatile_capacity);
	req.payload.flags = pi.flags;

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel set-partition failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel set-partition ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel set-partition: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	print_set_partition_result(&pi);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-fw-info (inner opcode 0x0200)                       */
/* ------------------------------------------------------------------ */

static void sdb_parse_get_fw_info_rsp(const struct cxlmi_cmd_get_fw_info_rsp *wire,
				      struct cxlmi_cmd_get_fw_info_rsp *host)
{
	memset(host, 0, sizeof(*host));
	host->slots_supported = wire->slots_supported;
	host->slot_info = wire->slot_info;
	host->caps = wire->caps;
	memcpy(host->fw_rev1, wire->fw_rev1, sizeof(wire->fw_rev1));
	memcpy(host->fw_rev2, wire->fw_rev2, sizeof(wire->fw_rev2));
	memcpy(host->fw_rev3, wire->fw_rev3, sizeof(wire->fw_rev3));
	memcpy(host->fw_rev4, wire->fw_rev4, sizeof(wire->fw_rev4));
}

static int sdb_tunnel_get_fw_info(struct cxlmi_endpoint *ep,
				  int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_get_fw_info_rsp rsp;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_get_fw_info_rsp fw;
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
				"Usage: sdb-tunnel get-fw-info [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x00; /* GET_INFO         */
	req.msg.command_set = 0x02; /* FIRMWARE_UPDATE */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel get-fw-info failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel get-fw-info ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel get-fw-info: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	sdb_parse_get_fw_info_rsp(&rsp.rsp, &fw);
	print_get_fw_info(&fw);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel transfer-fw (inner opcode 0x0201)                       */
/* ------------------------------------------------------------------ */

struct sdb_xfer_fw_ctx {
	uint8_t port_id;
};

static bool sdb_xfer_fw_inner_ok(uint16_t return_code)
{
	return return_code == 0 || return_code == CXLMI_RET_BACKGROUND;
}

static int sdb_xfer_fw_send(struct cxlmi_endpoint *ep, void *ctx,
			    struct cxlmi_cmd_transfer_fw_req *req,
			    size_t data_sz)
{
	struct sdb_xfer_fw_ctx *sctx = ctx;
	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) rsp;
	size_t req_payload_sz = CXL_FW_XFER_FIXED + data_sz;
	size_t full_req_sz = sizeof(struct sdb_tunnel_req_hdr) +
			     sizeof(struct cxlmi_cci_msg) + req_payload_sz;
	uint8_t *req_buf = NULL;
	struct sdb_tunnel_req_hdr *req_hdr;
	struct cxlmi_cci_msg *req_msg;
	uint8_t *req_pl;
	int rc;

	req_buf = calloc(1, full_req_sz);
	if (!req_buf) {
		fprintf(stderr, "sdb-tunnel transfer-fw: out of memory\n");
		return -1;
	}

	req_hdr = (struct sdb_tunnel_req_hdr *)req_buf;
	req_msg = (struct cxlmi_cci_msg *)(req_buf + sizeof(*req_hdr));
	req_pl  = req_buf + sizeof(*req_hdr) + sizeof(*req_msg);

	req_hdr->id           = sctx->port_id;
	req_hdr->target_type  = 0;
	req_hdr->command_size = (uint16_t)(sizeof(*req_msg) + req_payload_sz);

	req_msg->command     = 0x01; /* TRANSFER */
	req_msg->command_set = 0x02; /* FIRMWARE_UPDATE */
	req_msg->pl_length[0] = (uint8_t)(req_payload_sz & 0xff);
	req_msg->pl_length[1] = (uint8_t)((req_payload_sz >> 8) & 0xff);

	req_pl[0] = req->action;
	req_pl[1] = req->slot;
	memset(req_pl + 2, 0, 2);
	*(leint32_t *)(req_pl + 4) = cpu_to_le32(req->offset);
	memset(req_pl + 8, 0, CXL_FW_XFER_FIXED - 8);
	memcpy(req_pl + CXL_FW_XFER_FIXED, req->data, data_sz);

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", req_buf, full_req_sz);

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       req_buf, full_req_sz,
				       &rsp, sizeof(rsp));
	free(req_buf);

	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel transfer-fw failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel transfer-fw ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (!sdb_xfer_fw_inner_ok(rsp.msg.return_code)) {
		fprintf(stderr,
			"sdb-tunnel transfer-fw: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	return (int)rsp.msg.return_code;
}

static int sdb_tunnel_transfer_fw(struct cxlmi_endpoint *ep,
				 int argc, char **argv)
{
	struct transfer_fw_params params;
	struct sdb_xfer_fw_ctx ctx = { 0 };
	char *part_argv[16];
	int part_argc = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			ctx.port_id = (uint8_t)rc;
		} else {
			if (part_argc >= (int)(sizeof(part_argv) / sizeof(part_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel transfer-fw: too many arguments\n");
				return -1;
			}
			part_argv[part_argc++] = argv[i];
		}
	}

	rc = parse_transfer_fw_req(part_argc, part_argv, &params);
	if (rc)
		return rc;

	return transfer_fw_file(ep, &params, sdb_xfer_fw_send, &ctx);
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel activate-fw (inner opcode 0x0202)                       */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_activate_fw(struct cxlmi_endpoint *ep,
				  int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr      hdr;
		struct cxlmi_cci_msg           msg;
		struct cxlmi_cmd_activate_fw_req payload;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_activate_fw_req act;
	char *part_argv[16];
	int part_argc = 0;
	uint8_t port_id = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (part_argc >= (int)(sizeof(part_argv) / sizeof(part_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel activate-fw: too many arguments\n");
				return -1;
			}
			part_argv[part_argc++] = argv[i];
		}
	}

	rc = parse_activate_fw_req(part_argc, part_argv, &act);
	if (rc)
		return rc;

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.payload));

	req.msg.command     = 0x02; /* ACTIVATE */
	req.msg.command_set = 0x02; /* FIRMWARE_UPDATE */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] = (uint8_t)((sizeof(req.payload) >> 8) & 0xff);

	req.payload.action = act.action;
	req.payload.slot = act.slot;

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel activate-fw failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel activate-fw ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (!sdb_xfer_fw_inner_ok(rsp.msg.return_code)) {
		fprintf(stderr,
			"sdb-tunnel activate-fw: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	print_activate_fw_result(&act, (int)rsp.msg.return_code);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-health-info (inner opcode 0x4200)                 */
/* ------------------------------------------------------------------ */

static void sdb_parse_memdev_health_info_rsp(
	const struct cxlmi_cmd_memdev_get_health_info_rsp *wire,
	struct cxlmi_cmd_memdev_get_health_info_rsp *host)
{
	memset(host, 0, sizeof(*host));
	host->health_status = wire->health_status;
	host->media_status = wire->media_status;
	host->additional_status = wire->additional_status;
	host->life_used = wire->life_used;
	host->device_temperature = le16_to_cpu(wire->device_temperature);
	host->dirty_shutdown_count = le32_to_cpu(wire->dirty_shutdown_count);
	host->corrected_volatile_error_count =
		le32_to_cpu(wire->corrected_volatile_error_count);
	host->corrected_persistent_error_count =
		le32_to_cpu(wire->corrected_persistent_error_count);
}

static int sdb_tunnel_get_health_info(struct cxlmi_endpoint *ep,
				      int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_memdev_get_health_info_rsp rsp;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_memdev_get_health_info_rsp hi;
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
				"Usage: sdb-tunnel get-health-info [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x00; /* GET_HEALTH_INFO    */
	req.msg.command_set = 0x42; /* HEALTH_INFO_ALERTS */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel get-health-info failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel get-health-info ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel get-health-info: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	sdb_parse_memdev_health_info_rsp(&rsp.rsp, &hi);
	print_memdev_health_info(&hi);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-alert-config (inner opcode 0x4201)                */
/* ------------------------------------------------------------------ */

static void sdb_parse_memdev_get_alert_config_rsp(
	const struct cxlmi_cmd_memdev_get_alert_config_rsp *wire,
	struct cxlmi_cmd_memdev_get_alert_config_rsp *host)
{
	memset(host, 0, sizeof(*host));
	host->valid_alerts = wire->valid_alerts;
	host->programmable_alerts = wire->programmable_alerts;
	host->life_used_critical_alert_threshold =
		wire->life_used_critical_alert_threshold;
	host->life_used_programmable_warning_threshold =
		wire->life_used_programmable_warning_threshold;
	host->device_over_temperature_critical_alert_threshold =
		le16_to_cpu(wire->device_over_temperature_critical_alert_threshold);
	host->device_under_temperature_critical_alert_threshold =
		le16_to_cpu(wire->device_under_temperature_critical_alert_threshold);
	host->device_over_temperature_programmable_warning_threshold =
		le16_to_cpu(wire->device_over_temperature_programmable_warning_threshold);
	host->device_under_temperature_programmable_warning_threshold =
		le16_to_cpu(wire->device_under_temperature_programmable_warning_threshold);
	host->corrected_volatile_mem_error_programmable_warning_threshold =
		le16_to_cpu(wire->corrected_volatile_mem_error_programmable_warning_threshold);
	host->corrected_persistent_mem_error_programmable_warning_threshold =
		le16_to_cpu(wire->corrected_persistent_mem_error_programmable_warning_threshold);
}

static int sdb_tunnel_get_alert_config(struct cxlmi_endpoint *ep,
				       int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_memdev_get_alert_config_rsp rsp;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_memdev_get_alert_config_rsp ac;
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
				"Usage: sdb-tunnel get-alert-config [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x01; /* GET_ALERT_CONFIG   */
	req.msg.command_set = 0x42; /* HEALTH_INFO_ALERTS */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel get-alert-config failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel get-alert-config ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel get-alert-config: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	sdb_parse_memdev_get_alert_config_rsp(&rsp.rsp, &ac);
	print_memdev_alert_config(&ac);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel set-alert-config (inner opcode 0x4202)                */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_set_alert_config(struct cxlmi_endpoint *ep,
				       int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr                         hdr;
		struct cxlmi_cci_msg                              msg;
		struct cxlmi_cmd_memdev_set_alert_config_req      payload;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_memdev_set_alert_config_req ac;
	char *alert_argv[16];
	int alert_argc = 0;
	uint8_t port_id = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (alert_argc >= (int)(sizeof(alert_argv) / sizeof(alert_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel set-alert-config: too many arguments\n");
				return -1;
			}
			alert_argv[alert_argc++] = argv[i];
		}
	}

	rc = parse_set_alert_config_req(alert_argc, alert_argv, &ac);
	if (rc)
		return rc;

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.payload));

	req.msg.command     = 0x02; /* SET_ALERT_CONFIG   */
	req.msg.command_set = 0x42; /* HEALTH_INFO_ALERTS */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] = (uint8_t)((sizeof(req.payload) >> 8) & 0xff);

	req.payload.valid_alert_actions = ac.valid_alert_actions;
	req.payload.enable_alert_actions = ac.enable_alert_actions;
	req.payload.life_used_programmable_warning_threshold =
		ac.life_used_programmable_warning_threshold;
	req.payload.device_over_temperature_programmable_warning_threshold =
		cpu_to_le16(ac.device_over_temperature_programmable_warning_threshold);
	req.payload.device_under_temperature_programmable_warning_threshold =
		cpu_to_le16(ac.device_under_temperature_programmable_warning_threshold);
	req.payload.corrected_volatile_mem_error_programmable_warning_threshold =
		cpu_to_le16(ac.corrected_volatile_mem_error_programmable_warning_threshold);
	req.payload.corrected_persistent_mem_error_programmable_warning_threshold =
		cpu_to_le16(ac.corrected_persistent_mem_error_programmable_warning_threshold);

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel set-alert-config failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel set-alert-config ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel set-alert-config: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	print_set_alert_config_result(&ac);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-sld-qos-ctrl (inner opcode 0x4700)                */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_get_sld_qos_ctrl(struct cxlmi_endpoint *ep,
				       int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_memdev_get_sld_qos_control_rsp rsp;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_memdev_get_sld_qos_control_rsp qos;
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
				"Usage: sdb-tunnel get-sld-qos-ctrl [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x00; /* GET_SLD_QOS_CONTROL */
	req.msg.command_set = 0x47; /* SLD_QOS_TELEMETRY  */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel get-sld-qos-ctrl failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel get-sld-qos-ctrl ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel get-sld-qos-ctrl: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	memcpy(&qos, &rsp.rsp, sizeof(qos));
	print_sld_qos_control(&qos);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel set-sld-qos-ctrl (inner opcode 0x4701)                */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_set_sld_qos_ctrl(struct cxlmi_endpoint *ep,
				       int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr                         hdr;
		struct cxlmi_cci_msg                              msg;
		struct cxlmi_cmd_memdev_set_sld_qos_control_req   payload;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_memdev_set_sld_qos_control_req qos;
	char *qos_argv[16];
	int qos_argc = 0;
	uint8_t port_id = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (qos_argc >= (int)(sizeof(qos_argv) / sizeof(qos_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel set-sld-qos-ctrl: too many arguments\n");
				return -1;
			}
			qos_argv[qos_argc++] = argv[i];
		}
	}

	rc = parse_set_sld_qos_ctrl_req(qos_argc, qos_argv, &qos);
	if (rc)
		return rc;

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.payload));

	req.msg.command     = 0x01; /* SET_SLD_QOS_CONTROL */
	req.msg.command_set = 0x47; /* SLD_QOS_TELEMETRY  */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] = (uint8_t)((sizeof(req.payload) >> 8) & 0xff);

	memcpy(&req.payload, &qos, sizeof(req.payload));

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel set-sld-qos-ctrl failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel set-sld-qos-ctrl ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel set-sld-qos-ctrl: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	print_set_sld_qos_ctrl_result(&qos);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-sld-qos-status (inner opcode 0x4702)              */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_get_sld_qos_status(struct cxlmi_endpoint *ep,
					 int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_memdev_get_sld_qos_status_rsp rsp;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_memdev_get_sld_qos_status_rsp qos;
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
				"Usage: sdb-tunnel get-sld-qos-status [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x02; /* GET_SLD_QOS_STATUS  */
	req.msg.command_set = 0x47; /* SLD_QOS_TELEMETRY   */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel get-sld-qos-status failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel get-sld-qos-status ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel get-sld-qos-status: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	qos.backpressure_avg_percentage = rsp.rsp.backpressure_avg_percentage;
	print_sld_qos_status(&qos);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel bg-op-abort (inner opcode 0x0005)                       */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_bg_op_abort(struct cxlmi_endpoint *ep,
				  int argc, char **argv)
{
	/* No request payload, no response payload. */
	struct {
		struct sdb_tunnel_req_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
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
				"Usage: sdb-tunnel bg-op-abort [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x05; /* REQUEST_BG_OP_ABORT */
	req.msg.command_set = 0x00; /* INFOSTAT */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel bg-op-abort failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel bg-op-abort ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr, "sdb-tunnel bg-op-abort: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	printf("bg-op-abort OK\n");
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
				"Usage: sdb-tunnel get-resp-msg-limit [--port <vdm0|vdm1|i3c>]\n");
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
				" [--port <vdm0|vdm1|i3c>] --limit <0-255>\n");
			return -1;
		}
	}

	if (!has_limit) {
		fprintf(stderr,
			"Usage: sdb-tunnel set-resp-msg-limit"
			" [--port <vdm0|vdm1|i3c>] --limit <0-255>\n");
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
/* sdb-tunnel clear-event-records (inner opcode 0x0101)               */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_clear_event_records(struct cxlmi_endpoint *ep,
					  int argc, char **argv)
{
	/*
	 * req payload has a FAM (handles[]), so the full tunnel packet is
	 * heap-allocated.  Layout:
	 *   sdb_tunnel_req_hdr (4B) + cxlmi_cci_msg (12B) +
	 *   cxlmi_cmd_clear_event_records_req fixed (6B) + nr_recs*2B
	 */
	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) rsp;

	uint16_t handles[SDB_MAX_EVENT_RECORDS];
	uint8_t  port_id = 0, nr_recs = 0, clear_all = 0;
	const char *log_name = NULL;
	uint8_t  *req_buf = NULL;
	size_t    req_payload_sz, full_req_sz;
	struct sdb_tunnel_req_hdr             *req_hdr;
	struct cxlmi_cci_msg                  *req_msg;
	struct cxlmi_cmd_clear_event_records_req *req_pl;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
			log_name = argv[++i];
		} else if (strcmp(argv[i], "--all") == 0) {
			clear_all = 1;
		} else if (strcmp(argv[i], "--handle") == 0 && i + 1 < argc) {
			if (nr_recs >= SDB_MAX_EVENT_RECORDS) {
				fprintf(stderr,
					"sdb-tunnel clear-event-records: too many --handle values (max %d)\n",
					SDB_MAX_EVENT_RECORDS);
				return -1;
			}
			handles[nr_recs++] = (uint16_t)strtoul(argv[++i], NULL, 0);
		} else {
			fprintf(stderr,
				"Usage: sdb-tunnel clear-event-records"
				" [--port <vmd0|vmd1|i3c>] --log <info|warn|failure|fatal|dcd>"
				" [--all] [--handle <h>...]\n");
			return -1;
		}
	}

	if (!log_name) {
		fprintf(stderr,
			"Usage: sdb-tunnel clear-event-records"
			" [--port <vmd0|vmd1|i3c>] --log <info|warn|failure|fatal|dcd>"
			" [--all] [--handle <h>...]\n");
		return -1;
	}

	rc = parse_event_log_local(log_name);
	if (rc < 0)
		return -1;

	/* Build the tunnel packet dynamically to accommodate handles[]. */
	req_payload_sz = sizeof(*req_pl) + nr_recs * sizeof(uint16_t);
	full_req_sz    = sizeof(*req_hdr) + sizeof(*req_msg) + req_payload_sz;

	req_buf = calloc(1, full_req_sz);
	if (!req_buf) {
		fprintf(stderr, "sdb-tunnel clear-event-records: out of memory\n");
		return -1;
	}

	req_hdr = (struct sdb_tunnel_req_hdr *)req_buf;
	req_msg = (struct cxlmi_cci_msg *)(req_buf + sizeof(*req_hdr));
	req_pl  = (struct cxlmi_cmd_clear_event_records_req *)
		  (req_buf + sizeof(*req_hdr) + sizeof(*req_msg));

	req_hdr->id           = port_id;
	req_hdr->target_type  = 0;
	req_hdr->command_size = (uint16_t)(sizeof(*req_msg) + req_payload_sz);

	req_msg->command     = 0x01; /* CLEAR_RECORDS */
	req_msg->command_set = 0x01; /* EVENTS        */
	/* pl_length is 20-bit LE; fits in pl_length[0..1] for reasonable sizes */
	req_msg->pl_length[0] = (uint8_t)(req_payload_sz & 0xff);
	req_msg->pl_length[1] = (uint8_t)((req_payload_sz >> 8) & 0xff);

	req_pl->event_log   = (uint8_t)rc;
	if (clear_all) {
		req_pl->clear_flags = 0x1;
		req_pl->nr_recs     = 0;
	} else {
		req_pl->clear_flags = 0;
		req_pl->nr_recs     = nr_recs;
		memcpy(req_pl->handles, handles, nr_recs * sizeof(uint16_t));
	}

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", req_buf, full_req_sz);

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       req_buf, full_req_sz,
				       &rsp, sizeof(rsp));
	free(req_buf);

	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel clear-event-records failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel clear-event-records ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel clear-event-records: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	printf("Event records cleared (log=%s%s)\n",
	       log_name, clear_all ? ", all" : "");
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-event-records (inner opcode 0x0100)                 */
/* ------------------------------------------------------------------ */

/*
 * The response buffer must accommodate the tunnel wrapper, the inner
 * cxlmi_cci_msg header, the fixed part of cxlmi_cmd_get_event_records_rsp,
 * and up to SDB_MAX_EVENT_RECORDS variable-length records.
 */
typedef struct {
	struct sdb_tunnel_rsp_hdr              hdr;
	struct cxlmi_cci_msg                   msg;
	struct cxlmi_cmd_get_event_records_rsp rsp;
} __attribute__((packed)) sdb_get_event_rsp_t;

static int sdb_tunnel_get_event_records(struct cxlmi_endpoint *ep,
					int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr              hdr;
		struct cxlmi_cci_msg                   msg;
		struct cxlmi_cmd_get_event_records_req req_payload;
	} __attribute__((packed)) req;

	sdb_get_event_rsp_t *rsp_buf;
	size_t rsp_buf_sz;
	uint8_t port_id = 0;
	const char *log_name = NULL;
	uint32_t round = 0;
	int rc = 0, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
			log_name = argv[++i];
		} else {
			fprintf(stderr,
				"Usage: sdb-tunnel get-event-records"
				" [--port <vdm0|vdm1|i3c>] --log <info|warn|failure|fatal|dcd>\n");
			return -1;
		}
	}

	if (!log_name) {
		fprintf(stderr,
			"Usage: sdb-tunnel get-event-records"
			" [--port <vdm0|vdm1|i3c>] --log <info|warn|failure|fatal|dcd>\n");
		return -1;
	}

	rc = parse_event_log_local(log_name);
	if (rc < 0)
		return -1;

	rsp_buf_sz = sizeof(sdb_get_event_rsp_t) +
		     SDB_MAX_EVENT_RECORDS * sizeof(struct cxlmi_event_record);
	rsp_buf = calloc(1, rsp_buf_sz);
	if (!rsp_buf) {
		fprintf(stderr, "sdb-tunnel get-event-records: out of memory\n");
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg) + sizeof(req.req_payload);

	req.msg.command      = 0x00; /* GET_EVENT_RECORDS */
	req.msg.command_set  = 0x01; /* Event (0x01xx)    */
	req.msg.pl_length[0] = sizeof(req.req_payload); /* 1 byte */

	req.req_payload.event_log = (uint8_t)rc;

	printf("Log: %s (%u)\n", log_name, req.req_payload.event_log);

	do {
		uint16_t count;
		uint16_t k;

		memset(rsp_buf, 0, rsp_buf_sz);

		dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

		rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
					       &req, sizeof(req),
					       rsp_buf, rsp_buf_sz);
		if (rc) {
			if (rc > 0)
				fprintf(stderr,
					"sdb-tunnel get-event-records failed: %s\n",
					cxlmi_cmd_retcode_tostr(rc));
			else
				fprintf(stderr,
					"sdb-tunnel get-event-records ioctl failed\n");
			break;
		}

		dump_hex("sdb-tunnel RX", rsp_buf, rsp_buf_sz);

		if (rsp_buf->msg.return_code != 0) {
			fprintf(stderr,
				"sdb-tunnel get-event-records: inner CCI error 0x%04x\n",
				rsp_buf->msg.return_code);
			rc = (int)rsp_buf->msg.return_code;
			break;
		}

		printf("\n--- Round %u ---\n", round + 1);
		printf("Flags:                 0x%02x [%s%s]\n",
		       rsp_buf->rsp.flags,
		       (rsp_buf->rsp.flags & SDB_RSP_FLAG_OVERFLOW)    ? "OVERFLOW "   : "",
		       (rsp_buf->rsp.flags & SDB_RSP_FLAG_MORE_EVENTS) ? "MORE_EVENTS" : "");
		printf("Overflow Error Count:  %u\n",
		       rsp_buf->rsp.overflow_err_count);
		printf("First Overflow TS:     %llu\n",
		       (unsigned long long)rsp_buf->rsp.first_overflow_timestamp);
		printf("Last Overflow TS:      %llu\n",
		       (unsigned long long)rsp_buf->rsp.last_overflow_timestamp);

		count = rsp_buf->rsp.record_count;
		if (count > SDB_MAX_EVENT_RECORDS) {
			fprintf(stderr,
				"warning: record_count %u exceeds limit %u, clamping\n",
				count, SDB_MAX_EVENT_RECORDS);
			count = SDB_MAX_EVENT_RECORDS;
		}
		printf("Record Count:          %u\n", count);

		for (k = 0; k < count; k++) {
			const struct cxlmi_event_record *r =
				&rsp_buf->rsp.records[k];
			int j;

			printf("\n  [Record %u]\n", k);
			printf("    UUID:           ");
			for (j = 0; j < 16; j++)
				printf("%02x", r->uuid[j]);
			printf("\n");
			printf("    Handle:         0x%04x\n", r->handle);
			printf("    Related Handle: 0x%04x\n", r->related_handle);
			printf("    Timestamp:      %llu\n",
			       (unsigned long long)r->timestamp);
			printf("    Flags:          0x%02x 0x%02x 0x%02x\n",
			       r->flags[0], r->flags[1], r->flags[2]);
			printf("    Length:         %u\n", r->length);
			printf("    MaintOpClass:   0x%02x  SubClass: 0x%02x\n",
			       r->maint_op_class, r->maint_op_subclass);
			printf("    LD ID:          %u  Head ID: %u\n",
			       r->ld_id, r->head_id);
			printf("    Data:           ");
			for (j = 0; j < 0x50; j++) {
				printf("%02x", r->data[j]);
				if ((j + 1) % 16 == 0 && j + 1 < 0x50)
					printf("\n                    ");
			}
			printf("\n");
		}

		round++;
	} while (rsp_buf->rsp.flags & SDB_RSP_FLAG_MORE_EVENTS);

	free(rsp_buf);
	return rc;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-mctp-evt-int-policy (inner opcode 0x0104)           */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_get_mctp_evt_int_policy(struct cxlmi_endpoint *ep,
					      int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr                          hdr;
		struct cxlmi_cci_msg                               msg;
		struct cxlmi_cmd_get_mctp_event_interrupt_policy_rsp rsp;
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
				"Usage: sdb-tunnel get-mctp-evt-int-policy [--port <vmd0|vmd1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x04; /* GET_MCTP_EVENT_INTERRUPT_POLICY */
	req.msg.command_set = 0x01; /* EVENTS */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr,
				"sdb-tunnel get-mctp-evt-int-policy failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"sdb-tunnel get-mctp-evt-int-policy ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel get-mctp-evt-int-policy: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	{
		uint16_t s = rsp.rsp.event_interrupt_settings;

		printf("MCTP Event Interrupt Settings: 0x%04x\n", s);
		printf("  [0] Informational Event Log:       %s\n", (s >> 0) & 1 ? "enabled" : "disabled");
		printf("  [1] Warning Event Log:             %s\n", (s >> 1) & 1 ? "enabled" : "disabled");
		printf("  [2] Failure Event Log:             %s\n", (s >> 2) & 1 ? "enabled" : "disabled");
		printf("  [3] Fatal Event Log:               %s\n", (s >> 3) & 1 ? "enabled" : "disabled");
		printf("  [4] Dynamic Capacity Event Log:    %s\n", (s >> 4) & 1 ? "enabled" : "disabled");
		printf("  [15] Background Operation Done:    %s\n", (s >> 15) & 1 ? "enabled" : "disabled");
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel set-mctp-evt-int-policy (inner opcode 0x0105)           */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_set_mctp_evt_int_policy(struct cxlmi_endpoint *ep,
					      int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr                          hdr;
		struct cxlmi_cci_msg                               msg;
		struct cxlmi_cmd_set_mctp_event_interrupt_policy_req payload;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) rsp;

	uint8_t port_id = 0;
	int has_settings = 0, rc, i;
	unsigned long settings_val = 0;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else if (strcmp(argv[i], "--settings") == 0 && i + 1 < argc) {
			settings_val = strtoul(argv[++i], NULL, 0);
			if (settings_val > 0xffff) {
				fprintf(stderr,
					"sdb-tunnel set-mctp-evt-int-policy:"
					" --settings must be a 16-bit value (0x0000-0xffff)\n");
				return -1;
			}
			has_settings = 1;
		} else {
			fprintf(stderr,
				"Usage: sdb-tunnel set-mctp-evt-int-policy"
				" [--port <vmd0|vmd1|i3c>] --settings <hex>\n");
			return -1;
		}
	}

	if (!has_settings) {
		fprintf(stderr,
			"Usage: sdb-tunnel set-mctp-evt-int-policy"
			" [--port <vmd0|vmd1|i3c>] --settings <hex>\n");
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg) + sizeof(req.payload);

	req.msg.command      = 0x05; /* SET_MCTP_EVENT_INTERRUPT_POLICY */
	req.msg.command_set  = 0x01; /* EVENTS */
	req.msg.pl_length[0] = sizeof(req.payload) & 0xff;
	req.msg.pl_length[1] = (sizeof(req.payload) >> 8) & 0xff;

	req.payload.event_interrupt_settings = (uint16_t)settings_val;

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr,
				"sdb-tunnel set-mctp-evt-int-policy failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"sdb-tunnel set-mctp-evt-int-policy ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel set-mctp-evt-int-policy: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	printf("MCTP Event Interrupt Settings set: 0x%04lx\n", settings_val);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-timestamp (inner opcode 0x0300)                     */
/* ------------------------------------------------------------------ */

static void sdb_print_timestamp(uint64_t ns)
{
	time_t sec = (time_t)(ns / 1000000000ULL);
	uint32_t frac_ns = (uint32_t)(ns % 1000000000ULL);
	struct tm *tm;
	char buf[64];

	printf("Timestamp (raw):    %llu ns\n", (unsigned long long)ns);
	tm = localtime(&sec);
	if (tm && strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm))
		printf("Timestamp (local):  %s.%09u\n", buf, frac_ns);
	else
		printf("Timestamp (local):  (decode failed)\n");
}

static int sdb_tunnel_get_timestamp(struct cxlmi_endpoint *ep,
				    int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
		struct cxlmi_cmd_get_timestamp_rsp rsp;
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
				"Usage: sdb-tunnel get-timestamp [--port <vmd0|vmd1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x00; /* GET_TIMESTAMP */
	req.msg.command_set = 0x03; /* TIMESTAMP     */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel get-timestamp failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel get-timestamp ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr, "sdb-tunnel get-timestamp: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	sdb_print_timestamp(rsp.rsp.timestamp);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel set-timestamp (inner opcode 0x0301)                     */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_set_timestamp(struct cxlmi_endpoint *ep,
				    int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr hdr;
		struct cxlmi_cci_msg      msg;
		struct cxlmi_cmd_set_timestamp_req payload;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) rsp;

	uint8_t port_id = 0;
	int rc, i;
	struct timespec ts;
	uint64_t ts_ns;

	/* Default: current host time. */
	clock_gettime(CLOCK_REALTIME, &ts);
	ts_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else if (strcmp(argv[i], "--ts") == 0 && i + 1 < argc) {
			ts_ns = strtoull(argv[++i], NULL, 0);
		} else {
			fprintf(stderr,
				"Usage: sdb-tunnel set-timestamp [--port <vmd0|vmd1|i3c>] [--ts <ns>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg) + sizeof(req.payload);

	req.msg.command      = 0x01; /* SET_TIMESTAMP */
	req.msg.command_set  = 0x03; /* TIMESTAMP     */
	req.msg.pl_length[0] = sizeof(req.payload) & 0xff;
	req.msg.pl_length[1] = (sizeof(req.payload) >> 8) & 0xff;

	req.payload.timestamp = ts_ns;

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel set-timestamp failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel set-timestamp ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr, "sdb-tunnel set-timestamp: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	printf("Timestamp set to %llu ns\n", (unsigned long long)ts_ns);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel fm-get-ld-info (inner opcode 0x5400)                  */
/* ------------------------------------------------------------------ */

static void sdb_parse_fm_get_ld_info_rsp(
	const struct cxlmi_cmd_fmapi_get_ld_info_rsp *wire,
	struct cxlmi_cmd_fmapi_get_ld_info_rsp *host)
{
	memset(host, 0, sizeof(*host));
	host->memory_size = le64_to_cpu(wire->memory_size);
	host->ld_count = le16_to_cpu(wire->ld_count);
	host->qos_telemetry_capability = wire->qos_telemetry_capability;
}

static int sdb_tunnel_fm_get_ld_info(struct cxlmi_endpoint *ep,
				     int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_fmapi_get_ld_info_rsp rsp;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_fmapi_get_ld_info_rsp info;
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
				"Usage: sdb-tunnel fm-get-ld-info [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x00; /* GET_LD_INFO    */
	req.msg.command_set = 0x54; /* MLD_COMPONENTS */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel fm-get-ld-info failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel fm-get-ld-info ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel fm-get-ld-info: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	sdb_parse_fm_get_ld_info_rsp(&rsp.rsp, &info);
	print_fm_get_ld_info(&info);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel fm-get-ld-alloc (inner opcode 0x5401)                 */
/* ------------------------------------------------------------------ */

static void sdb_parse_fm_get_ld_alloc_rsp(
	const struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *wire,
	struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *host)
{
	unsigned int i;

	host->number_ld = wire->number_ld;
	host->memory_granularity = wire->memory_granularity;
	host->start_ld_id = wire->start_ld_id;
	host->ld_allocation_list_len = wire->ld_allocation_list_len;

	for (i = 0; i < host->ld_allocation_list_len; i++) {
		host->ld_allocation_list[i].range_1_allocation_mult =
			le64_to_cpu(wire->ld_allocation_list[i].range_1_allocation_mult);
		host->ld_allocation_list[i].range_2_allocation_mult =
			le64_to_cpu(wire->ld_allocation_list[i].range_2_allocation_mult);
	}
}

static int sdb_tunnel_fm_get_ld_alloc(struct cxlmi_endpoint *ep,
				      int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr                         hdr;
		struct cxlmi_cci_msg                              msg;
		struct cxlmi_cmd_fmapi_get_ld_allocations_req     payload;
	} __attribute__((packed)) req;

	struct cxlmi_cmd_fmapi_get_ld_allocations_req in;
	char *alloc_argv[16];
	int alloc_argc = 0;
	uint8_t *rsp_buf = NULL;
	uint8_t *host_buf = NULL;
	struct cxlmi_cci_msg *inner_rsp;
	struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *wire_rsp;
	struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *host_rsp;
	size_t list_bytes, rsp_buf_sz;
	uint8_t port_id = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (alloc_argc >= (int)(sizeof(alloc_argv) / sizeof(alloc_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel fm-get-ld-alloc: too many arguments\n");
				return -1;
			}
			alloc_argv[alloc_argc++] = argv[i];
		}
	}

	rc = parse_fm_get_ld_alloc_req(alloc_argc, alloc_argv, &in);
	if (rc)
		return rc;

	list_bytes = in.ld_allocation_list_limit *
		     sizeof(struct cxlmi_cmd_fmapi_ld_allocations_list);
	rsp_buf_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
		     sizeof(struct cxlmi_cci_msg) +
		     sizeof(struct cxlmi_cmd_fmapi_get_ld_allocations_rsp) +
		     list_bytes;

	rsp_buf = calloc(1, rsp_buf_sz);
	host_buf = calloc(1, sizeof(struct cxlmi_cmd_fmapi_get_ld_allocations_rsp) +
			       list_bytes);
	if (!rsp_buf || !host_buf) {
		fprintf(stderr, "sdb-tunnel fm-get-ld-alloc: out of memory\n");
		free(rsp_buf);
		free(host_buf);
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.payload));

	req.msg.command     = 0x01; /* GET_LD_ALLOCATIONS */
	req.msg.command_set = 0x54; /* MLD_COMPONENTS     */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] = (uint8_t)((sizeof(req.payload) >> 8) & 0xff);

	req.payload.start_ld_id = in.start_ld_id;
	req.payload.ld_allocation_list_limit = in.ld_allocation_list_limit;

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       rsp_buf, rsp_buf_sz);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel fm-get-ld-alloc failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel fm-get-ld-alloc ioctl failed\n");
		free(rsp_buf);
		free(host_buf);
		return rc;
	}

	dump_hex("sdb-tunnel RX", rsp_buf, rsp_buf_sz);

	inner_rsp = (struct cxlmi_cci_msg *)(rsp_buf + sizeof(struct sdb_tunnel_rsp_hdr));
	if (inner_rsp->return_code != 0) {
		uint16_t err = inner_rsp->return_code;

		fprintf(stderr,
			"sdb-tunnel fm-get-ld-alloc: inner CCI error 0x%04x\n",
			err);
		free(rsp_buf);
		free(host_buf);
		return (int)err;
	}

	wire_rsp = (struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *)inner_rsp->payload;
	host_rsp = (struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *)host_buf;
	sdb_parse_fm_get_ld_alloc_rsp(wire_rsp, host_rsp);
	print_fm_get_ld_alloc(host_rsp);

	free(rsp_buf);
	free(host_buf);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-supported-logs (inner opcode 0x0400)                */
/* ------------------------------------------------------------------ */

#define SDB_SUPPORTED_LOGS_RSP_SZ \
	(sizeof(struct cxlmi_cmd_get_supported_logs_rsp) + \
	 CXLMI_MAX_SUPPORTED_LOGS * sizeof(struct cxlmi_supported_log_entry))

static void sdb_parse_supported_logs_rsp(
	const struct cxlmi_cmd_get_supported_logs_rsp *wire,
	struct cxlmi_cmd_get_supported_logs_rsp *host)
{
	uint16_t n;
	int i;

	memset(host, 0, SDB_SUPPORTED_LOGS_RSP_SZ);
	n = le16_to_cpu(wire->num_supported_log_entries);
	host->num_supported_log_entries = n;

	for (i = 0; i < n; i++) {
		memcpy(host->entries[i].uuid, wire->entries[i].uuid, 16);
		host->entries[i].log_size =
			le32_to_cpu(wire->entries[i].log_size);
	}
}

static int sdb_tunnel_fetch_supported_logs(struct cxlmi_endpoint *ep,
					   uint8_t port_id,
					   struct cxlmi_cmd_get_supported_logs_rsp *host)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	uint8_t *rsp_buf;
	struct cxlmi_cci_msg *inner_rsp;
	struct cxlmi_cmd_get_supported_logs_rsp *wire_rsp;
	size_t rsp_buf_sz;
	int rc;

	rsp_buf_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
		     sizeof(struct cxlmi_cci_msg) +
		     SDB_SUPPORTED_LOGS_RSP_SZ;
	rsp_buf = calloc(1, rsp_buf_sz);
	if (!rsp_buf)
		return -1;

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x00; /* GET_SUPPORTED_LOGS */
	req.msg.command_set = 0x04; /* LOGS               */

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       rsp_buf, rsp_buf_sz);
	if (rc) {
		free(rsp_buf);
		return rc;
	}

	inner_rsp = (struct cxlmi_cci_msg *)(rsp_buf +
					    sizeof(struct sdb_tunnel_rsp_hdr));
	if (inner_rsp->return_code != 0) {
		rc = (int)inner_rsp->return_code;
		free(rsp_buf);
		return rc;
	}

	wire_rsp = (struct cxlmi_cmd_get_supported_logs_rsp *)inner_rsp->payload;
	sdb_parse_supported_logs_rsp(wire_rsp, host);
	free(rsp_buf);
	return 0;
}

static int sdb_tunnel_get_supported_logs(struct cxlmi_endpoint *ep,
				       int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct cxlmi_cmd_get_supported_logs_rsp *rsp;
	uint8_t port_id = 0;
	int rc, i;

	rsp = calloc(1, SDB_SUPPORTED_LOGS_RSP_SZ);
	if (!rsp) {
		fprintf(stderr, "sdb-tunnel get-supported-logs: out of memory\n");
		return -1;
	}

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0) {
				free(rsp);
				return -1;
			}
			port_id = (uint8_t)rc;
		} else {
			fprintf(stderr,
				"Usage: sdb-tunnel get-supported-logs [--port <vdm0|vdm1|i3c>]\n");
			free(rsp);
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x00; /* GET_SUPPORTED_LOGS */
	req.msg.command_set = 0x04; /* LOGS               */

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = sdb_tunnel_fetch_supported_logs(ep, port_id, rsp);
	if (rc) {
		if (rc > 0)
			fprintf(stderr,
				"sdb-tunnel get-supported-logs failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"sdb-tunnel get-supported-logs ioctl failed\n");
		free(rsp);
		return rc;
	}

	print_supported_logs(rsp);
	free(rsp);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-supported-feat (inner opcode 0x0500)                */
/* ------------------------------------------------------------------ */

static void sdb_parse_supported_features_rsp(
	const struct cxlmi_cmd_get_supported_features_rsp *wire,
	struct cxlmi_cmd_get_supported_features_rsp *host,
	uint32_t count_bytes)
{
	uint16_t n;
	int i;

	memset(host, 0, MBCCI_FEATURE_RSP_BUF_SZ(count_bytes));
	n = le16_to_cpu(wire->num_supported_feature_entries);
	host->num_supported_feature_entries = n;
	host->device_supported_features =
		le16_to_cpu(wire->device_supported_features);

	for (i = 0; i < n; i++) {
		memcpy(host->supported_feature_entries[i].feature_id,
		       wire->supported_feature_entries[i].feature_id,
		       sizeof(wire->supported_feature_entries[i].feature_id));
		host->supported_feature_entries[i].feature_index =
			le16_to_cpu(wire->supported_feature_entries[i].feature_index);
		host->supported_feature_entries[i].get_feature_size =
			le16_to_cpu(wire->supported_feature_entries[i].get_feature_size);
		host->supported_feature_entries[i].set_feature_size =
			le16_to_cpu(wire->supported_feature_entries[i].set_feature_size);
		host->supported_feature_entries[i].attribute_flags =
			le32_to_cpu(wire->supported_feature_entries[i].attribute_flags);
		host->supported_feature_entries[i].get_feature_version =
			wire->supported_feature_entries[i].get_feature_version;
		host->supported_feature_entries[i].set_feature_version =
			wire->supported_feature_entries[i].set_feature_version;
		host->supported_feature_entries[i].set_feature_effects =
			le16_to_cpu(wire->supported_feature_entries[i].set_feature_effects);
	}
}

static int sdb_tunnel_fetch_supported_features(
	struct cxlmi_endpoint *ep, uint8_t port_id,
	const struct cxlmi_cmd_get_supported_features_req *in,
	struct cxlmi_cmd_get_supported_features_rsp *host)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_get_supported_features_req payload;
	} __attribute__((packed)) req;

	uint8_t *rsp_buf;
	struct cxlmi_cci_msg *inner_rsp;
	struct cxlmi_cmd_get_supported_features_rsp *wire_rsp;
	size_t rsp_buf_sz;
	int rc;

	rsp_buf_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
		     sizeof(struct cxlmi_cci_msg) +
		     MBCCI_FEATURE_RSP_BUF_SZ(in->count);
	rsp_buf = calloc(1, rsp_buf_sz);
	if (!rsp_buf)
		return -1;

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.payload));

	req.msg.command     = 0x00; /* GET_SUPPORTED_FEATURES */
	req.msg.command_set = 0x05; /* FEATURES               */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] = (uint8_t)((sizeof(req.payload) >> 8) & 0xff);
	req.msg.pl_length[2] = (uint8_t)((sizeof(req.payload) >> 16) & 0xff);

	req.payload.count = cpu_to_le32(in->count);
	req.payload.starting_feature_index = cpu_to_le16(in->starting_feature_index);

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       rsp_buf, rsp_buf_sz);
	if (rc) {
		free(rsp_buf);
		return rc;
	}

	inner_rsp = (struct cxlmi_cci_msg *)(rsp_buf +
					    sizeof(struct sdb_tunnel_rsp_hdr));
	if (inner_rsp->return_code != 0) {
		rc = (int)inner_rsp->return_code;
		free(rsp_buf);
		return rc;
	}

	wire_rsp = (struct cxlmi_cmd_get_supported_features_rsp *)inner_rsp->payload;
	sdb_parse_supported_features_rsp(wire_rsp, host, in->count);
	free(rsp_buf);
	return 0;
}

static int sdb_tunnel_get_supported_feat(struct cxlmi_endpoint *ep,
					 int argc, char **argv)
{
	struct cxlmi_cmd_get_supported_features_req req;
	struct cxlmi_cmd_get_supported_features_rsp *rsp;
	char *feat_argv[16];
	int feat_argc = 0;
	uint8_t port_id = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (feat_argc >= (int)(sizeof(feat_argv) / sizeof(feat_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel get-supported-feat: too many arguments\n");
				return -1;
			}
			feat_argv[feat_argc++] = argv[i];
		}
	}

	rc = parse_get_supported_features_req(feat_argc, feat_argv, &req);
	if (rc)
		return rc;

	rsp = calloc(1, MBCCI_FEATURE_RSP_BUF_SZ(req.count));
	if (!rsp) {
		fprintf(stderr, "sdb-tunnel get-supported-feat: out of memory\n");
		return -1;
	}

	rc = sdb_tunnel_fetch_supported_features(ep, port_id, &req, rsp);
	if (rc) {
		if (rc > 0)
			fprintf(stderr,
				"sdb-tunnel get-supported-feat failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"sdb-tunnel get-supported-feat ioctl failed\n");
		free(rsp);
		return rc;
	}

	print_supported_features(rsp);
	free(rsp);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel get-log (inner opcode 0x0401)                           */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_get_log(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr     hdr;
		struct cxlmi_cci_msg        msg;
		struct cxlmi_cmd_get_log_req payload;
	} __attribute__((packed)) req;

	struct get_log_params params;
	char *log_argv[16];
	int log_argc = 0;
	uint8_t *rsp_buf = NULL;
	struct cxlmi_cci_msg *inner_rsp;
	uint8_t *log_data;
	struct cxlmi_cmd_get_supported_logs_rsp *srsp;
	size_t rsp_buf_sz;
	uint8_t port_id = 0;
	int rc, i;

	memset(&params, 0, sizeof(params));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (log_argc >= (int)(sizeof(log_argv) / sizeof(log_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel get-log: too many arguments\n");
				return -1;
			}
			log_argv[log_argc++] = argv[i];
		}
	}

	rc = parse_get_log_req(log_argc, log_argv, &params);
	if (rc)
		return rc;

	if (!params.has_length) {
		srsp = calloc(1, SDB_SUPPORTED_LOGS_RSP_SZ);
		if (!srsp) {
			fprintf(stderr, "sdb-tunnel get-log: out of memory\n");
			return -1;
		}
		rc = sdb_tunnel_fetch_supported_logs(ep, port_id, srsp);
		if (rc) {
			if (rc > 0)
				fprintf(stderr,
					"sdb-tunnel get-log: get-supported-logs failed: %s\n",
					cxlmi_cmd_retcode_tostr(rc));
			else
				fprintf(stderr,
					"sdb-tunnel get-log: get-supported-logs ioctl failed\n");
			free(srsp);
			return rc;
		}
		params.length = lookup_log_size(srsp, params.uuid);
		free(srsp);

		if (params.length == 0) {
			fprintf(stderr,
				"sdb-tunnel get-log: UUID not found in supported logs list\n");
			return -1;
		}
	}

	rsp_buf_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
		     sizeof(struct cxlmi_cci_msg) +
		     params.length;
	rsp_buf = calloc(1, rsp_buf_sz);
	if (!rsp_buf) {
		fprintf(stderr, "sdb-tunnel get-log: out of memory\n");
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.payload));

	req.msg.command     = 0x01; /* GET_LOG */
	req.msg.command_set = 0x04; /* LOGS    */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] = (uint8_t)((sizeof(req.payload) >> 8) & 0xff);

	memcpy(req.payload.uuid, params.uuid, sizeof(req.payload.uuid));
	req.payload.offset = cpu_to_le32(params.offset);
	req.payload.length = cpu_to_le32(params.length);

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       rsp_buf, rsp_buf_sz);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel get-log failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel get-log ioctl failed\n");
		free(rsp_buf);
		return rc;
	}

	dump_hex("sdb-tunnel RX", rsp_buf, rsp_buf_sz);

	inner_rsp = (struct cxlmi_cci_msg *)(rsp_buf +
					    sizeof(struct sdb_tunnel_rsp_hdr));
	if (inner_rsp->return_code != 0) {
		uint16_t ret = inner_rsp->return_code;

		fprintf(stderr,
			"sdb-tunnel get-log: inner CCI error 0x%04x\n", ret);
		free(rsp_buf);
		return (int)ret;
	}

	log_data = inner_rsp->payload;
	print_log_header(params.uuid, params.offset, params.length);
	print_log_payload(params.uuid, params.offset, params.length, log_data,
			  params.has_text);

	free(rsp_buf);
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
			"  identify           [--port <vdm0|vdm1|i3c>]                        Generic Component Identify (0x0001)\n"
			"  identify_memdev    [--port <vdm0|vdm1|i3c>]                        Identify Memory Device (0x4000)\n"
			"  get-partition      [--port <vdm0|vdm1|i3c>]                        Get Partition Info (0x4100)\n"
			"  set-partition      [--port <vdm0|vdm1|i3c>] --next-volatile <MiB> [--flags <n>] [--bp-dirty-shutdown]\n"
			"                                                                         Set Partition Info (0x4101)\n"
			"  get-fw-info        [--port <vdm0|vdm1|i3c>]                        Get FW Info (0x0200)\n"
			"  transfer-fw        [--port <vdm0|vdm1|i3c>] --input <file> --slot <n> [--chunk-size <n>]\n"
			"                                                                         Transfer FW (0x0201)\n"
			"  activate-fw        [--port <vdm0|vdm1|i3c>] --slot <n> [--action online|offline]\n"
			"                                                                         Activate FW (0x0202)\n"
			"  get-health-info    [--port <vdm0|vdm1|i3c>]                        Get Health Info (0x4200)\n"
			"  get-alert-config   [--port <vdm0|vdm1|i3c>]                        Get Alert Configuration (0x4201)\n"
			"  set-alert-config   [--port <vdm0|vdm1|i3c>] [--life-used-warning <pct>] [--over-temp-warning <n>] ...\n"
			"                                                                         Set Alert Configuration (0x4202)\n"
			"  get-sld-qos-ctrl   [--port <vdm0|vdm1|i3c>]                        Get SLD QoS Control (0x4700)\n"
			"  set-sld-qos-ctrl   [--port <vdm0|vdm1|i3c>] [--egress-congestion-control-enable <0|1>] [--egress-tpr-enable <0|1>] ...\n"
			"                                                                         Set SLD QoS Control (0x4701)\n"
			"  get-sld-qos-status [--port <vdm0|vdm1|i3c>]                        Get SLD QoS Status (0x4702)\n"
			"  fm-get-ld-info     [--port <vdm0|vdm1|i3c>]                        FM Get LD Info (0x5400)\n"
			"  fm-get-ld-alloc    [--port <vdm0|vdm1|i3c>] [--start-ld-id <n>] [--limit <n>]\n"
			"                                                                         FM Get LD Allocations (0x5401)\n"
			"  get-supported-logs [--port <vdm0|vdm1|i3c>]                        Get Supported Logs (0x0400)\n"
			"  get-supported-feat [--port <vdm0|vdm1|i3c>] [--count <bytes>] [--start-index <n>]\n"
			"                                                                         Get Supported Features (0x0500)\n"
			"  get-log            [--port <vdm0|vdm1|i3c>] --uuid <uuid> [--offset <n>] [--length <n>] [--text]\n"
			"                                                                         Get Log (0x0401)\n"
			"  get-resp-msg-limit [--port <vdm0|vdm1|i3c>]                        Get Response Message Limit (0x0003)\n"
			"  set-resp-msg-limit [--port <vdm0|vdm1|i3c>] --limit <n>            Set Response Message Limit (0x0004)\n"
			"  bg-op-abort          [--port <vmd0|vmd1|i3c>]                                          Request Abort Background Operation (0x0005)\n"
			"  get-event-records    [--port <vmd0|vmd1|i3c>] --log <info|warn|...>                 Get Event Records (0x0100)\n"
			"  clear-event-records      [--port <vmd0|vmd1|i3c>] --log <...> [--all|--handle <h>...]  Clear Event Records (0x0101)\n"
			"  get-mctp-evt-int-policy  [--port <vmd0|vmd1|i3c>]                                   Get MCTP Event Interrupt Policy (0x0104)\n"
			"  set-mctp-evt-int-policy  [--port <vmd0|vmd1|i3c>] --settings <hex>                  Set MCTP Event Interrupt Policy (0x0105)\n"
			"  get-timestamp            [--port <vmd0|vmd1|i3c>]                                   Get Timestamp (0x0300)\n"
			"  set-timestamp            [--port <vmd0|vmd1|i3c>] [--ts <ns>]                       Set Timestamp (0x0301, default: host time)\n");
		return -1;
	}

	if (strcmp(argv[1], "identify") == 0)
		return sdb_tunnel_identify(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "identify_memdev") == 0)
		return sdb_tunnel_identify_memdev(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-partition") == 0)
		return sdb_tunnel_get_partition(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "set-partition") == 0)
		return sdb_tunnel_set_partition(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-fw-info") == 0)
		return sdb_tunnel_get_fw_info(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "transfer-fw") == 0)
		return sdb_tunnel_transfer_fw(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "activate-fw") == 0)
		return sdb_tunnel_activate_fw(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-health-info") == 0)
		return sdb_tunnel_get_health_info(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-alert-config") == 0)
		return sdb_tunnel_get_alert_config(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "set-alert-config") == 0)
		return sdb_tunnel_set_alert_config(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-sld-qos-ctrl") == 0)
		return sdb_tunnel_get_sld_qos_ctrl(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "set-sld-qos-ctrl") == 0)
		return sdb_tunnel_set_sld_qos_ctrl(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-sld-qos-status") == 0)
		return sdb_tunnel_get_sld_qos_status(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "fm-get-ld-info") == 0)
		return sdb_tunnel_fm_get_ld_info(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "fm-get-ld-alloc") == 0)
		return sdb_tunnel_fm_get_ld_alloc(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-supported-logs") == 0)
		return sdb_tunnel_get_supported_logs(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-supported-feat") == 0)
		return sdb_tunnel_get_supported_feat(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-log") == 0)
		return sdb_tunnel_get_log(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "bg-op-abort") == 0)
		return sdb_tunnel_bg_op_abort(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-resp-msg-limit") == 0)
		return sdb_tunnel_get_resp_msg_limit(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "set-resp-msg-limit") == 0)
		return sdb_tunnel_set_resp_msg_limit(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-event-records") == 0)
		return sdb_tunnel_get_event_records(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "clear-event-records") == 0)
		return sdb_tunnel_clear_event_records(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-mctp-evt-int-policy") == 0)
		return sdb_tunnel_get_mctp_evt_int_policy(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "set-mctp-evt-int-policy") == 0)
		return sdb_tunnel_set_mctp_evt_int_policy(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-timestamp") == 0)
		return sdb_tunnel_get_timestamp(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "set-timestamp") == 0)
		return sdb_tunnel_set_timestamp(ep, argc - 2, argv + 2);

	fprintf(stderr, "sdb-tunnel: unknown cci-cmd '%s'\n", argv[1]);
	fprintf(stderr,
		"  supported: identify, identify_memdev, get-partition, set-partition, get-fw-info, transfer-fw, activate-fw, get-health-info, get-alert-config, set-alert-config, get-sld-qos-ctrl, set-sld-qos-ctrl, get-sld-qos-status, fm-get-ld-info, fm-get-ld-alloc, get-supported-logs, get-supported-feat, get-log, bg-op-abort, get-resp-msg-limit, set-resp-msg-limit,"
		" get-event-records, clear-event-records,"
		" get-mctp-evt-int-policy, set-mctp-evt-int-policy,"
		" get-timestamp, set-timestamp\n");
	return -1;
}
