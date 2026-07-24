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
 *   get-poison-list   Get Poison List (opcode 0x4300)
 *   inject-poison     Inject Poison (opcode 0x4301)
 *   clear-poison      Clear Poison (opcode 0x4302)
 *   get-scan-media-cap Get Scan Media Capabilities (opcode 0x4303)
 *   scan-media        Scan Media (opcode 0x4304)
 *   get-scan-media-results Get Scan Media Results (opcode 0x4305)
 *   get-sld-qos-ctrl  Get SLD QoS Control (opcode 0x4700)
 *   set-sld-qos-ctrl  Set SLD QoS Control (opcode 0x4701)
 *   get-sld-qos-status Get SLD QoS Status (opcode 0x4702)
 *   fm-get-ld-info    FM Get LD Info (opcode 0x5400)
 *   fm-get-ld-alloc   FM Get LD Allocations (opcode 0x5401)
 *   fm-set-ld-alloc   FM Set LD Allocations (opcode 0x5402)
 *   fm-get-qos-ctrl   FM Get QoS Control (opcode 0x5403)
 *   fm-set-qos-ctrl   FM Set QoS Control (opcode 0x5404)
 *   fm-get-qos-status FM Get QoS Status (opcode 0x5405)
 *   fm-get-qos-alloc-bw FM Get QoS Allocated BW (opcode 0x5406)
 *   fm-set-qos-alloc-bw FM Set QoS Allocated BW (opcode 0x5407)
 *   fm-get-qos-bw-limit FM Get QoS BW Limit (opcode 0x5408)
 *   fm-set-qos-bw-limit FM Set QoS BW Limit (opcode 0x5409)
 *   get-supported-logs Get Supported Logs (opcode 0x0400)
 *   get-log           Get Log (opcode 0x0401)
 *   get-log-cap       Get Log Capabilities (opcode 0x0402)
 *   clear-log         Clear Log (opcode 0x0403)
 *   populate-log      Populate Log (opcode 0x0404)
 *   get-supported-feat Get Supported Features (opcode 0x0500)
 *   get-feature       Get Feature (opcode 0x0501)
 *   set-feature       Set Feature (opcode 0x0502)
 *   bg-op-status      Background Operation Status (opcode 0x0002)
 */
#include <stddef.h>
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
/* sdb-tunnel vu-dlcfg (inner opcode 0xCC53 / vuCmdId DLCFG=0x07)     */
/* ------------------------------------------------------------------ */

#define SDB_VU_CMD_SET 0xCC
#define SDB_VU_CMD     0x53

struct sdb_vu_dlcfg_ctx {
	uint8_t port_id;
};

static int sdb_vu_tunnel_exchange(struct cxlmi_endpoint *ep, uint8_t port_id,
				    const void *vu_payload, size_t vu_payload_sz,
				    void *vu_rsp, size_t vu_rsp_sz)
{
	size_t full_req_sz = sizeof(struct sdb_tunnel_req_hdr) +
			     sizeof(struct cxlmi_cci_msg) + vu_payload_sz;
	size_t full_rsp_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
			     sizeof(struct cxlmi_cci_msg) + vu_rsp_sz;
	uint8_t *req_buf = NULL;
	uint8_t *rsp_buf = NULL;
	struct sdb_tunnel_req_hdr *req_hdr;
	struct cxlmi_cci_msg *req_msg;
	struct cxlmi_cci_msg *inner_rsp;
	uint8_t *req_pl;
	int rc;

	req_buf = calloc(1, full_req_sz);
	if (!req_buf) {
		fprintf(stderr, "sdb-tunnel vu: out of memory\n");
		return -1;
	}

	rsp_buf = calloc(1, full_rsp_sz);
	if (!rsp_buf) {
		fprintf(stderr, "sdb-tunnel vu: out of memory\n");
		free(req_buf);
		return -1;
	}

	req_hdr = (struct sdb_tunnel_req_hdr *)req_buf;
	req_msg = (struct cxlmi_cci_msg *)(req_buf + sizeof(*req_hdr));
	req_pl  = req_buf + sizeof(*req_hdr) + sizeof(*req_msg);

	req_hdr->id           = port_id;
	req_hdr->target_type  = 0;
	req_hdr->command_size = (uint16_t)(sizeof(*req_msg) + vu_payload_sz);

	req_msg->command     = SDB_VU_CMD;
	req_msg->command_set = SDB_VU_CMD_SET;
	req_msg->pl_length[0] = (uint8_t)(vu_payload_sz & 0xff);
	req_msg->pl_length[1] = (uint8_t)((vu_payload_sz >> 8) & 0xff);

	memcpy(req_pl, vu_payload, vu_payload_sz);

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", req_buf, full_req_sz);

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       req_buf, full_req_sz,
				       rsp_buf, full_rsp_sz);
	free(req_buf);

	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel vu failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel vu ioctl failed\n");
		free(rsp_buf);
		return rc;
	}

	if (rsp_buf) {
		dump_hex("sdb-tunnel RX", rsp_buf, full_rsp_sz);

		inner_rsp = (struct cxlmi_cci_msg *)(rsp_buf +
			sizeof(struct sdb_tunnel_rsp_hdr));
		if (inner_rsp->return_code != 0) {
			uint16_t ret = inner_rsp->return_code;

			fprintf(stderr,
				"sdb-tunnel vu: inner CCI error 0x%04x\n", ret);
			free(rsp_buf);
			return (int)ret;
		}

		if (vu_rsp && vu_rsp_sz)
			memcpy(vu_rsp, inner_rsp->payload, vu_rsp_sz);
	}
	free(rsp_buf);

	return 0;
}

static int sdb_vu_tunnel_send(struct cxlmi_endpoint *ep, uint8_t port_id,
			      const void *vu_payload, size_t vu_payload_sz)
{
	return sdb_vu_tunnel_exchange(ep, port_id, vu_payload, vu_payload_sz,
				      NULL, 0);
}

static int sdb_vu_tunnel_unlock(struct cxlmi_endpoint *ep, uint8_t port_id)
{
	uint32_t req[8] = { 0 };

	req[0] = 0; /* VUUNLOCK */
	return sdb_vu_tunnel_send(ep, port_id, req, sizeof(req));
}

static int sdb_vu_tunnel_lock(struct cxlmi_endpoint *ep, uint8_t port_id)
{
	uint32_t req[8] = { 0 };

	req[0] = 1; /* VULOCK */
	return sdb_vu_tunnel_send(ep, port_id, req, sizeof(req));
}

static int sdb_vu_dlcfg_send(struct cxlmi_endpoint *ep, void *ctx,
			     void *req, size_t req_sz)
{
	struct sdb_vu_dlcfg_ctx *sctx = ctx;

	return sdb_vu_tunnel_send(ep, sctx->port_id, req, req_sz);
}

static int sdb_tunnel_vu_dlcfg(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct vu_dlcfg_params params;
	struct sdb_vu_dlcfg_ctx ctx = { 0 };
	char *part_argv[16];
	int part_argc = 0;
	int rc, lock_rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			ctx.port_id = (uint8_t)rc;
		} else {
			if (part_argc >= (int)(sizeof(part_argv) / sizeof(part_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel vu-dlcfg: too many arguments\n");
				return -1;
			}
			part_argv[part_argc++] = argv[i];
		}
	}

	rc = parse_vu_dlcfg_req(part_argc, part_argv, &params);
	if (rc)
		return rc;

	rc = sdb_vu_tunnel_unlock(ep, ctx.port_id);
	if (rc)
		return rc;

	rc = vu_dlcfg_file(ep, &params, sdb_vu_dlcfg_send, &ctx);

	lock_rc = sdb_vu_tunnel_lock(ep, ctx.port_id);
	if (rc == 0)
		rc = lock_rc;

	return rc;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel vu-getdevcfg (inner opcode 0xCC53 / vuCmdId GETCFG=0x08) */
/* ------------------------------------------------------------------ */

static int sdb_vu_getcfg_send(struct cxlmi_endpoint *ep, void *ctx,
			      void *req, void *out)
{
	struct sdb_vu_dlcfg_ctx *sctx = ctx;

	return sdb_vu_tunnel_exchange(ep, sctx->port_id, req,
				      VU_GETCFG_REQ_BYTES, out,
				      VU_GETCFG_OUTPUT_BYTES);
}

static int sdb_tunnel_vu_getdevcfg(struct cxlmi_endpoint *ep,
				   int argc, char **argv)
{
	struct vu_getdevcfg_params params;
	struct sdb_vu_dlcfg_ctx ctx = { 0 };
	char *part_argv[16];
	int part_argc = 0;
	int rc, lock_rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			ctx.port_id = (uint8_t)rc;
		} else {
			if (part_argc >= (int)(sizeof(part_argv) / sizeof(part_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel vu-getdevcfg: too many arguments\n");
				return -1;
			}
			part_argv[part_argc++] = argv[i];
		}
	}

	rc = parse_vu_getdevcfg_req(part_argc, part_argv, &params);
	if (rc)
		return rc;

	rc = sdb_vu_tunnel_unlock(ep, ctx.port_id);
	if (rc)
		return rc;

	rc = vu_getdevcfg_fetch(ep, &params, sdb_vu_getcfg_send, &ctx);

	lock_rc = sdb_vu_tunnel_lock(ep, ctx.port_id);
	if (rc == 0)
		rc = lock_rc;

	return rc;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel vu-ddrfreq / vu-pciespeed (0xCC53 / 0x09 / 0x0a)      */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_vu_ddrfreq(struct cxlmi_endpoint *ep,
				 int argc, char **argv)
{
	struct vu_ddrfreq_params params;
	uint8_t req[VU_CFGFREQ_REQ_BYTES];
	struct sdb_vu_dlcfg_ctx ctx = { 0 };
	char *part_argv[16];
	int part_argc = 0;
	size_t req_sz;
	int rc, lock_rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			ctx.port_id = (uint8_t)rc;
		} else {
			if (part_argc >= (int)(sizeof(part_argv) / sizeof(part_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel vu-ddrfreq: too many arguments\n");
				return -1;
			}
			part_argv[part_argc++] = argv[i];
		}
	}

	rc = parse_vu_ddrfreq_req(part_argc, part_argv, &params);
	if (rc)
		return rc;

	req_sz = vu_ddrfreq_pack(&params, req, sizeof(req));
	if (req_sz == 0)
		return -1;

	rc = sdb_vu_tunnel_unlock(ep, ctx.port_id);
	if (rc)
		return rc;

	rc = sdb_vu_tunnel_send(ep, ctx.port_id, req, req_sz);

	lock_rc = sdb_vu_tunnel_lock(ep, ctx.port_id);
	if (rc == 0)
		rc = lock_rc;

	if (rc == 0)
		printf("vu-ddrfreq OK: freq=%u MT/s\n", params.freqmts);
	return rc;
}

static int sdb_tunnel_vu_pciespeed(struct cxlmi_endpoint *ep,
				   int argc, char **argv)
{
	struct vu_pciespeed_params params;
	uint8_t req[VU_CFGPCIE_REQ_BYTES];
	struct sdb_vu_dlcfg_ctx ctx = { 0 };
	char *part_argv[16];
	int part_argc = 0;
	size_t req_sz;
	int rc, lock_rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			ctx.port_id = (uint8_t)rc;
		} else {
			if (part_argc >= (int)(sizeof(part_argv) / sizeof(part_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel vu-pciespeed: too many arguments\n");
				return -1;
			}
			part_argv[part_argc++] = argv[i];
		}
	}

	rc = parse_vu_pciespeed_req(part_argc, part_argv, &params);
	if (rc)
		return rc;

	req_sz = vu_pciespeed_pack(&params, req, sizeof(req));
	if (req_sz == 0)
		return -1;

	rc = sdb_vu_tunnel_unlock(ep, ctx.port_id);
	if (rc)
		return rc;

	rc = sdb_vu_tunnel_send(ep, ctx.port_id, req, req_sz);

	lock_rc = sdb_vu_tunnel_lock(ep, ctx.port_id);
	if (rc == 0)
		rc = lock_rc;

	if (rc == 0) {
		printf("vu-pciespeed OK: pcie-port=%u speed=gen%u width=x%u\n",
		       params.portid, params.speed, params.width);
	}
	return rc;
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
/* sdb-tunnel bg-op-status (inner opcode 0x0002)                      */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_bg_op_status(struct cxlmi_endpoint *ep,
				 int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr         hdr;
		struct cxlmi_cci_msg              msg;
		struct cxlmi_cmd_bg_op_status_rsp wire;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_bg_op_status_rsp out;
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
				"Usage: sdb-tunnel bg-op-status [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x02; /* BACKGROUND_OPERATION_STATUS */
	req.msg.command_set = 0x00; /* INFOSTAT */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel bg-op-status failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel bg-op-status ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel bg-op-status: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	out.status = rsp.wire.status;
	out.opcode = le16_to_cpu(rsp.wire.opcode);
	out.returncode = le16_to_cpu(rsp.wire.returncode);
	out.vendor_ext_status = le16_to_cpu(rsp.wire.vendor_ext_status);

	printf("Status:             %u\n", out.status);
	printf("Opcode:             0x%04x\n", out.opcode);
	printf("Return Code:        0x%04x\n", out.returncode);
	printf("Vendor Ext Status:  0x%04x\n", out.vendor_ext_status);
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
		struct cxlmi_cmd_get_mctp_event_interrupt_policy_rsp rsp;
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

	req.payload.event_interrupt_settings = cpu_to_le16((uint16_t)settings_val);

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

	{
		uint16_t s = le16_to_cpu(rsp.rsp.event_interrupt_settings);

		printf("MCTP Event Interrupt Settings set: 0x%04x\n", s);
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
/* sdb-tunnel poison commands (inner opcodes 0x4300-0x4302)          */
/* ------------------------------------------------------------------ */

/* Match mailbox poison sizing: header + records fit in 2048B. */
#define SDB_POISON_LIST_RSP_HDR_SZ 0x20
#define SDB_POISON_MAX_RECORDS \
	((CXL_MAILBOX_MAX_PAYLOAD_SIZE - SDB_POISON_LIST_RSP_HDR_SZ) / \
	 sizeof(struct cxlmi_memdev_media_err_record))
#define SDB_POISON_MAX_ITERATIONS 64
#define SDB_POISON_REQ_RESTART_BIT (1ULL << 0)
#define SDB_POISON_RSP_FLAG_MORE   (1U << 0)

static int sdb_split_poison_args(int argc, char **argv, uint8_t *port_id,
				 char **poison_argv, int *poison_argc)
{
	int rc;
	int i;

	*port_id = 0;
	*poison_argc = 0;
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			*port_id = (uint8_t)rc;
		} else {
			poison_argv[(*poison_argc)++] = argv[i];
		}
	}

	return 0;
}

static uint32_t sdb_payload_length(const struct cxlmi_cci_msg *msg)
{
	return msg->pl_length[0] |
	       ((uint32_t)msg->pl_length[1] << 8) |
	       ((uint32_t)(msg->pl_length[2] & 0x0f) << 16);
}

static int sdb_tunnel_get_poison_list(struct cxlmi_endpoint *ep,
				      int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr hdr;
		struct cxlmi_cci_msg msg;
		struct cxlmi_cmd_memdev_get_poison_list_req payload;
	} __attribute__((packed)) req;
	struct cxlmi_cmd_memdev_get_poison_list_req params;
	struct cxlmi_cmd_memdev_get_poison_list_rsp *wire_rsp;
	struct cxlmi_cmd_memdev_get_poison_list_rsp *host_rsp;
	struct cxlmi_cci_msg *inner_rsp;
	char **poison_argv;
	uint8_t *rsp_buf;
	size_t rsp_payload_sz;
	size_t rsp_buf_sz;
	uint64_t base_dpa;
	uint64_t length;
	uint32_t payload_len;
	uint16_t record_count;
	uint8_t port_id;
	int frestart = 0;
	int poison_argc;
	int iter;
	int rc;
	int i;

	poison_argv = calloc(argc ? (size_t)argc : 1, sizeof(*poison_argv));
	if (!poison_argv) {
		perror("sdb-tunnel get-poison-list: calloc");
		return -1;
	}
	rc = sdb_split_poison_args(argc, argv, &port_id, poison_argv,
				   &poison_argc);
	if (rc)
		goto out_argv;
	rc = parse_get_poison_list_req(poison_argc, poison_argv, &params,
				       &frestart);
	if (rc)
		goto out_argv;

	base_dpa = params.get_poison_list_phy_addr & ~SDB_POISON_REQ_RESTART_BIT;
	length = params.get_poison_list_phy_addr_len;

	rsp_payload_sz = sizeof(*host_rsp) +
		SDB_POISON_MAX_RECORDS *
			sizeof(struct cxlmi_memdev_media_err_record);
	rsp_buf_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
		     sizeof(struct cxlmi_cci_msg) + rsp_payload_sz;
	rsp_buf = calloc(1, rsp_buf_sz);
	host_rsp = calloc(1, rsp_payload_sz);
	if (!rsp_buf || !host_rsp) {
		perror("sdb-tunnel get-poison-list: calloc");
		free(rsp_buf);
		free(host_rsp);
		rc = -1;
		goto out_argv;
	}

	for (iter = 0; iter < SDB_POISON_MAX_ITERATIONS; iter++) {
		uint64_t req_dpa = base_dpa;

		if (iter == 0 && frestart)
			req_dpa |= SDB_POISON_REQ_RESTART_BIT;

		memset(&req, 0, sizeof(req));
		memset(rsp_buf, 0, rsp_buf_sz);
		memset(host_rsp, 0, rsp_payload_sz);

		req.hdr.id = port_id;
		req.hdr.target_type = 0;
		req.hdr.command_size =
			(uint16_t)(sizeof(req.msg) + sizeof(req.payload));
		req.msg.command = 0x00;     /* GET_POISON_LIST */
		req.msg.command_set = 0x43; /* MEDIA_AND_POISON */
		req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
		req.msg.pl_length[1] =
			(uint8_t)((sizeof(req.payload) >> 8) & 0xff);
		req.payload.get_poison_list_phy_addr = cpu_to_le64(req_dpa);
		req.payload.get_poison_list_phy_addr_len = cpu_to_le64(length);

		if (iter > 0)
			printf("--- poison list continuation %d ---\n", iter);

		dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));
		rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
					       &req, sizeof(req),
					       rsp_buf, rsp_buf_sz);
		if (rc) {
			if (rc > 0)
				fprintf(stderr,
					"sdb-tunnel get-poison-list failed: %s\n",
					cxlmi_cmd_retcode_tostr(rc));
			else
				fprintf(stderr,
					"sdb-tunnel get-poison-list ioctl failed\n");
			goto out_rsp;
		}

		dump_hex("sdb-tunnel RX", rsp_buf, rsp_buf_sz);
		inner_rsp = (struct cxlmi_cci_msg *)
			(rsp_buf + sizeof(struct sdb_tunnel_rsp_hdr));
		if (inner_rsp->return_code != 0) {
			fprintf(stderr,
				"sdb-tunnel get-poison-list: inner CCI error 0x%04x\n",
				inner_rsp->return_code);
			rc = (int)inner_rsp->return_code;
			goto out_rsp;
		}

		payload_len = sdb_payload_length(inner_rsp);
		if (payload_len < sizeof(*wire_rsp) ||
		    payload_len > rsp_payload_sz) {
			fprintf(stderr,
				"sdb-tunnel get-poison-list: invalid response payload length %u\n",
				payload_len);
			rc = -1;
			goto out_rsp;
		}

		wire_rsp = (struct cxlmi_cmd_memdev_get_poison_list_rsp *)
			inner_rsp->payload;
		host_rsp->poison_list_flags = wire_rsp->poison_list_flags;
		host_rsp->overflow_timestamp =
			le64_to_cpu(wire_rsp->overflow_timestamp);
		record_count = le16_to_cpu(wire_rsp->more_err_media_record_cnt);
		if (record_count > (payload_len - sizeof(*wire_rsp)) /
				   sizeof(struct cxlmi_memdev_media_err_record))
			record_count = (uint16_t)
				((payload_len - sizeof(*wire_rsp)) /
				 sizeof(struct cxlmi_memdev_media_err_record));
		if (record_count > SDB_POISON_MAX_RECORDS)
			record_count = SDB_POISON_MAX_RECORDS;
		host_rsp->more_err_media_record_cnt = record_count;
		for (i = 0; i < record_count; i++) {
			host_rsp->records[i].media_err_addr =
				le64_to_cpu(wire_rsp->records[i].media_err_addr);
			host_rsp->records[i].media_err_len =
				le32_to_cpu(wire_rsp->records[i].media_err_len);
		}

		print_poison_list(host_rsp);
		if (!(host_rsp->poison_list_flags & SDB_POISON_RSP_FLAG_MORE)) {
			rc = 0;
			goto out_rsp;
		}
	}

	fprintf(stderr,
		"sdb-tunnel get-poison-list: reached max iterations (%d) with MORE still set\n",
		SDB_POISON_MAX_ITERATIONS);
	rc = -1;

out_rsp:
	free(rsp_buf);
	free(host_rsp);
out_argv:
	free(poison_argv);
	return rc;
}

static int sdb_tunnel_inject_poison(struct cxlmi_endpoint *ep,
				    int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr hdr;
		struct cxlmi_cci_msg msg;
		struct cxlmi_cmd_memdev_inject_poison_req payload;
	} __attribute__((packed)) req;
	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg msg;
	} __attribute__((packed)) rsp;
	struct cxlmi_cmd_memdev_inject_poison_req params;
	char **poison_argv;
	uint8_t port_id;
	int poison_argc;
	int rc;

	poison_argv = calloc(argc ? (size_t)argc : 1, sizeof(*poison_argv));
	if (!poison_argv) {
		perror("sdb-tunnel inject-poison: calloc");
		return -1;
	}
	rc = sdb_split_poison_args(argc, argv, &port_id, poison_argv,
				   &poison_argc);
	if (rc)
		goto out;
	rc = parse_inject_poison_req(poison_argc, poison_argv, &params);
	if (rc)
		goto out;

	memset(&req, 0, sizeof(req));
	req.hdr.id = port_id;
	req.hdr.target_type = 0;
	req.hdr.command_size =
		(uint16_t)(sizeof(req.msg) + sizeof(req.payload));
	req.msg.command = 0x01;     /* INJECT_POISON */
	req.msg.command_set = 0x43; /* MEDIA_AND_POISON */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] =
		(uint8_t)((sizeof(req.payload) >> 8) & 0xff);
	req.payload.inject_poison_phy_addr =
		cpu_to_le64(params.inject_poison_phy_addr);

	memset(&rsp, 0, sizeof(rsp));
	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));
	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req), &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel inject-poison failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"sdb-tunnel inject-poison ioctl failed\n");
		goto out;
	}
	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel inject-poison: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		rc = (int)rsp.msg.return_code;
		goto out;
	}

	printf("Inject poison OK\n");
out:
	free(poison_argv);
	return rc;
}

static int sdb_tunnel_clear_poison(struct cxlmi_endpoint *ep,
				   int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr hdr;
		struct cxlmi_cci_msg msg;
		struct cxlmi_cmd_memdev_clear_poison_req payload;
	} __attribute__((packed)) req;
	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg msg;
	} __attribute__((packed)) rsp;
	struct cxlmi_cmd_memdev_clear_poison_req params;
	char **poison_argv;
	uint8_t port_id;
	int poison_argc;
	int rc;

	poison_argv = calloc(argc ? (size_t)argc : 1, sizeof(*poison_argv));
	if (!poison_argv) {
		perror("sdb-tunnel clear-poison: calloc");
		return -1;
	}
	rc = sdb_split_poison_args(argc, argv, &port_id, poison_argv,
				   &poison_argc);
	if (rc)
		goto out;
	rc = parse_clear_poison_req(poison_argc, poison_argv, &params);
	if (rc)
		goto out;

	memset(&req, 0, sizeof(req));
	req.hdr.id = port_id;
	req.hdr.target_type = 0;
	req.hdr.command_size =
		(uint16_t)(sizeof(req.msg) + sizeof(req.payload));
	req.msg.command = 0x02;     /* CLEAR_POISON */
	req.msg.command_set = 0x43; /* MEDIA_AND_POISON */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] =
		(uint8_t)((sizeof(req.payload) >> 8) & 0xff);
	req.payload.clear_poison_phy_addr =
		cpu_to_le64(params.clear_poison_phy_addr);
	memcpy(req.payload.clear_poison_write_data,
	       params.clear_poison_write_data,
	       sizeof(req.payload.clear_poison_write_data));

	memset(&rsp, 0, sizeof(rsp));
	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));
	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req), &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel clear-poison failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"sdb-tunnel clear-poison ioctl failed\n");
		goto out;
	}
	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel clear-poison: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		rc = (int)rsp.msg.return_code;
		goto out;
	}

	printf("Clear poison OK\n");
out:
	free(poison_argv);
	return rc;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel scan media commands (inner opcodes 0x4303-0x4305)      */
/* ------------------------------------------------------------------ */

#define SDB_SCAN_MEDIA_RESULTS_RSP_HDR_SZ 0x20
#define SDB_SCAN_MEDIA_MAX_RECORDS \
	((CXL_MAILBOX_MAX_PAYLOAD_SIZE - SDB_SCAN_MEDIA_RESULTS_RSP_HDR_SZ) / \
	 sizeof(struct cxlmi_media_error_record))
#define SDB_SCAN_MEDIA_MAX_ITERATIONS 64
#define SDB_SCAN_MEDIA_FLAG_NO_EVENT_LOG (1U << 0)
#define SDB_SCAN_RESULTS_FLAG_MORE (1U << 0)

static int sdb_tunnel_get_scan_media_cap(struct cxlmi_endpoint *ep,
					 int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr hdr;
		struct cxlmi_cci_msg msg;
		struct cxlmi_cmd_memdev_get_scan_media_capabilities_req payload;
	} __attribute__((packed)) req;
	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg msg;
		struct cxlmi_cmd_memdev_get_scan_media_capabilities_rsp payload;
	} __attribute__((packed)) rsp;
	struct cxlmi_cmd_memdev_get_scan_media_capabilities_req params;
	struct cxlmi_cmd_memdev_get_scan_media_capabilities_rsp host;
	char **scan_argv;
	uint8_t port_id;
	int scan_argc;
	int rc;

	scan_argv = calloc(argc ? (size_t)argc : 1, sizeof(*scan_argv));
	if (!scan_argv) {
		perror("sdb-tunnel get-scan-media-cap: calloc");
		return -1;
	}
	rc = sdb_split_poison_args(argc, argv, &port_id, scan_argv, &scan_argc);
	if (rc)
		goto out;
	rc = parse_get_scan_media_cap_req(scan_argc, scan_argv, &params);
	if (rc)
		goto out;

	memset(&req, 0, sizeof(req));
	req.hdr.id = port_id;
	req.hdr.target_type = 0;
	req.hdr.command_size =
		(uint16_t)(sizeof(req.msg) + sizeof(req.payload));
	req.msg.command = 0x03;     /* GET_SCAN_MEDIA_CAPABILITIES */
	req.msg.command_set = 0x43; /* MEDIA_AND_POISON */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] =
		(uint8_t)((sizeof(req.payload) >> 8) & 0xff);
	req.payload.get_scan_media_capabilities_start_physaddr =
		cpu_to_le64(params.get_scan_media_capabilities_start_physaddr);
	req.payload.get_scan_media_capabilities_physaddr_length =
		cpu_to_le64(params.get_scan_media_capabilities_physaddr_length);

	memset(&rsp, 0, sizeof(rsp));
	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));
	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req), &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr,
				"sdb-tunnel get-scan-media-cap failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"sdb-tunnel get-scan-media-cap ioctl failed\n");
		goto out;
	}
	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));
	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel get-scan-media-cap: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		rc = (int)rsp.msg.return_code;
		goto out;
	}

	memset(&host, 0, sizeof(host));
	host.estimated_scan_media_time =
		le32_to_cpu(rsp.payload.estimated_scan_media_time);
	print_scan_media_capabilities(&host);
out:
	free(scan_argv);
	return rc;
}

static int sdb_tunnel_scan_media(struct cxlmi_endpoint *ep,
				 int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr hdr;
		struct cxlmi_cci_msg msg;
		struct cxlmi_cmd_memdev_scan_media_req payload;
	} __attribute__((packed)) req;
	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg msg;
	} __attribute__((packed)) rsp;
	struct cxlmi_cmd_memdev_scan_media_req params;
	char **scan_argv;
	uint8_t port_id;
	int scan_argc;
	int rc;

	scan_argv = calloc(argc ? (size_t)argc : 1, sizeof(*scan_argv));
	if (!scan_argv) {
		perror("sdb-tunnel scan-media: calloc");
		return -1;
	}
	rc = sdb_split_poison_args(argc, argv, &port_id, scan_argv, &scan_argc);
	if (rc)
		goto out;
	rc = parse_scan_media_req(scan_argc, scan_argv, &params);
	if (rc)
		goto out;

	memset(&req, 0, sizeof(req));
	req.hdr.id = port_id;
	req.hdr.target_type = 0;
	req.hdr.command_size =
		(uint16_t)(sizeof(req.msg) + sizeof(req.payload));
	req.msg.command = 0x04;     /* SCAN_MEDIA */
	req.msg.command_set = 0x43; /* MEDIA_AND_POISON */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] =
		(uint8_t)((sizeof(req.payload) >> 8) & 0xff);
	req.payload.scan_media_physaddr =
		cpu_to_le64(params.scan_media_physaddr);
	req.payload.scan_media_physaddr_length =
		cpu_to_le64(params.scan_media_physaddr_length);
	req.payload.scan_media_flags = params.scan_media_flags;

	memset(&rsp, 0, sizeof(rsp));
	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));
	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req), &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel scan-media failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"sdb-tunnel scan-media ioctl failed\n");
		goto out;
	}
	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));
	if (rsp.msg.return_code != 0 &&
	    rsp.msg.return_code != CXLMI_RET_BACKGROUND) {
		fprintf(stderr,
			"sdb-tunnel scan-media: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		rc = (int)rsp.msg.return_code;
		goto out;
	}

	if (rsp.msg.return_code == CXLMI_RET_BACKGROUND)
		printf("Scan media started as background operation\n");
	else
		printf("Scan media OK\n");
	printf("  DPA:    0x%016llx\n",
	       (unsigned long long)params.scan_media_physaddr);
	printf("  Length: 0x%016llx\n",
	       (unsigned long long)params.scan_media_physaddr_length);
	printf("  Flags:  0x%02x%s\n", params.scan_media_flags,
	       (params.scan_media_flags & SDB_SCAN_MEDIA_FLAG_NO_EVENT_LOG) ?
	       " NO_EVTLOG" : "");
	rc = 0;
out:
	free(scan_argv);
	return rc;
}

static int sdb_tunnel_get_scan_media_results(struct cxlmi_endpoint *ep,
					     int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr hdr;
		struct cxlmi_cci_msg msg;
	} __attribute__((packed)) req;
	struct cxlmi_cmd_memdev_get_scan_media_results_rsp *wire_rsp;
	struct cxlmi_cmd_memdev_get_scan_media_results_rsp *host_rsp;
	struct cxlmi_cci_msg *inner_rsp;
	uint8_t *rsp_buf;
	size_t rsp_payload_sz;
	size_t rsp_buf_sz;
	uint32_t payload_len;
	uint16_t record_count;
	uint8_t port_id = 0;
	int iter;
	int rc;
	int i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			fprintf(stderr,
				"Usage: sdb-tunnel get-scan-media-results [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	rsp_payload_sz = sizeof(*host_rsp) +
		SDB_SCAN_MEDIA_MAX_RECORDS *
			sizeof(struct cxlmi_media_error_record);
	rsp_buf_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
		     sizeof(struct cxlmi_cci_msg) + rsp_payload_sz;
	rsp_buf = calloc(1, rsp_buf_sz);
	host_rsp = calloc(1, rsp_payload_sz);
	if (!rsp_buf || !host_rsp) {
		perror("sdb-tunnel get-scan-media-results: calloc");
		free(rsp_buf);
		free(host_rsp);
		return -1;
	}

	for (iter = 0; iter < SDB_SCAN_MEDIA_MAX_ITERATIONS; iter++) {
		memset(&req, 0, sizeof(req));
		memset(rsp_buf, 0, rsp_buf_sz);
		memset(host_rsp, 0, rsp_payload_sz);

		req.hdr.id = port_id;
		req.hdr.target_type = 0;
		req.hdr.command_size = (uint16_t)sizeof(req.msg);
		req.msg.command = 0x05;     /* GET_SCAN_MEDIA_RESULTS */
		req.msg.command_set = 0x43; /* MEDIA_AND_POISON */

		if (iter > 0)
			printf("--- scan media results continuation %d ---\n",
			       iter);

		dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));
		rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
					       &req, sizeof(req),
					       rsp_buf, rsp_buf_sz);
		if (rc) {
			if (rc > 0)
				fprintf(stderr,
					"sdb-tunnel get-scan-media-results failed: %s\n",
					cxlmi_cmd_retcode_tostr(rc));
			else
				fprintf(stderr,
					"sdb-tunnel get-scan-media-results ioctl failed\n");
			goto out;
		}

		dump_hex("sdb-tunnel RX", rsp_buf, rsp_buf_sz);
		inner_rsp = (struct cxlmi_cci_msg *)
			(rsp_buf + sizeof(struct sdb_tunnel_rsp_hdr));
		if (inner_rsp->return_code != 0) {
			fprintf(stderr,
				"sdb-tunnel get-scan-media-results: inner CCI error 0x%04x\n",
				inner_rsp->return_code);
			rc = (int)inner_rsp->return_code;
			goto out;
		}

		payload_len = sdb_payload_length(inner_rsp);
		if (payload_len < SDB_SCAN_MEDIA_RESULTS_RSP_HDR_SZ ||
		    payload_len > rsp_payload_sz) {
			fprintf(stderr,
				"sdb-tunnel get-scan-media-results: invalid response payload length %u\n",
				payload_len);
			rc = -1;
			goto out;
		}

		wire_rsp = (struct cxlmi_cmd_memdev_get_scan_media_results_rsp *)
			inner_rsp->payload;
		host_rsp->scan_media_restart_physaddr =
			le64_to_cpu(wire_rsp->scan_media_restart_physaddr);
		host_rsp->scan_media_restart_physaddr_length =
			le64_to_cpu(wire_rsp->scan_media_restart_physaddr_length);
		host_rsp->scan_media_flags = wire_rsp->scan_media_flags;
		record_count = le16_to_cpu(wire_rsp->media_error_count);
		if (record_count > (payload_len - SDB_SCAN_MEDIA_RESULTS_RSP_HDR_SZ) /
				   sizeof(struct cxlmi_media_error_record))
			record_count = (uint16_t)
				((payload_len - SDB_SCAN_MEDIA_RESULTS_RSP_HDR_SZ) /
				 sizeof(struct cxlmi_media_error_record));
		if (record_count > SDB_SCAN_MEDIA_MAX_RECORDS)
			record_count = SDB_SCAN_MEDIA_MAX_RECORDS;
		host_rsp->media_error_count = record_count;
		for (i = 0; i < record_count; i++) {
			host_rsp->record[i].media_error_address =
				le64_to_cpu(wire_rsp->record[i].media_error_address);
			host_rsp->record[i].media_error_length =
				le32_to_cpu(wire_rsp->record[i].media_error_length);
		}

		print_scan_media_results(host_rsp);
		if (!(host_rsp->scan_media_flags & SDB_SCAN_RESULTS_FLAG_MORE)) {
			rc = 0;
			goto out;
		}
	}

	fprintf(stderr,
		"sdb-tunnel get-scan-media-results: reached max iterations (%d) with MORE still set\n",
		SDB_SCAN_MEDIA_MAX_ITERATIONS);
	rc = -1;

out:
	free(rsp_buf);
	free(host_rsp);
	return rc;
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

	struct fm_get_ld_alloc_params params;
	char *alloc_argv[16];
	int alloc_argc = 0;
	uint8_t *rsp_buf = NULL;
	uint8_t *host_buf = NULL;
	uint8_t dump_buf[MBCCI_FM_GET_LD_ALLOC_HDR_SZ +
			 255 * sizeof(struct cxlmi_cmd_fmapi_ld_allocations_list)];
	struct cxlmi_cci_msg *inner_rsp;
	struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *wire_rsp;
	struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *host_rsp;
	size_t list_bytes, rsp_buf_sz, dump_len = 0;
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

	rc = parse_fm_get_ld_alloc_req(alloc_argc, alloc_argv, &params);
	if (rc)
		return rc;

	list_bytes = params.req.ld_allocation_list_limit *
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

	req.payload.start_ld_id = params.req.start_ld_id;
	req.payload.ld_allocation_list_limit =
		params.req.ld_allocation_list_limit;

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

	if (params.raw_dump_file) {
		size_t pl_len = inner_rsp->pl_length[0] |
				((size_t)inner_rsp->pl_length[1] << 8) |
				((size_t)(inner_rsp->pl_length[2] & 0x0f) << 16);

		if (pl_len == 0) {
			rc = fm_ld_alloc_build_get_rsp_payload(host_rsp, dump_buf,
							       sizeof(dump_buf),
							       &dump_len);
			if (rc) {
				fprintf(stderr,
					"sdb-tunnel fm-get-ld-alloc: nothing to dump (empty allocation list)\n");
				free(rsp_buf);
				free(host_buf);
				return -1;
			}
		} else {
			if (pl_len > sizeof(dump_buf)) {
				fprintf(stderr,
					"sdb-tunnel fm-get-ld-alloc: response payload too large (%zu bytes)\n",
					pl_len);
				free(rsp_buf);
				free(host_buf);
				return -1;
			}
			memcpy(dump_buf, wire_rsp, pl_len);
			dump_len = pl_len;
		}

		rc = write_hex_payload_file(params.raw_dump_file, dump_buf, dump_len);
		if (rc) {
			fprintf(stderr,
				"sdb-tunnel fm-get-ld-alloc: failed to write '%s': ",
				params.raw_dump_file);
			perror(NULL);
			free(rsp_buf);
			free(host_buf);
			return -1;
		}
		printf("Raw dump written to %s (%zu bytes, get-ld-alloc response payload)\n",
		       params.raw_dump_file, dump_len);
	}

	free(rsp_buf);
	free(host_buf);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel fm-set-ld-alloc (inner opcode 0x5402)                 */
/* ------------------------------------------------------------------ */

static void sdb_parse_fm_set_ld_alloc_rsp(
	const struct cxlmi_cmd_fmapi_set_ld_allocations_rsp *wire,
	struct cxlmi_cmd_fmapi_set_ld_allocations_rsp *host,
	uint8_t number_ld)
{
	unsigned int i;

	host->number_ld = wire->number_ld;
	host->start_ld_id = wire->start_ld_id;

	for (i = 0; i < number_ld; i++) {
		host->ld_allocation_list[i].range_1_allocation_mult =
			le64_to_cpu(wire->ld_allocation_list[i].range_1_allocation_mult);
		host->ld_allocation_list[i].range_2_allocation_mult =
			le64_to_cpu(wire->ld_allocation_list[i].range_2_allocation_mult);
	}
}

static int sdb_tunnel_fm_set_ld_alloc(struct cxlmi_endpoint *ep,
				      int argc, char **argv)
{
	struct fm_set_ld_alloc_params params;
	char *set_argv[16];
	int set_argc = 0;
	uint8_t *req_buf = NULL;
	uint8_t *rsp_buf = NULL;
	uint8_t *host_buf = NULL;
	uint8_t payload[MBCCI_FM_SET_LD_ALLOC_HDR_SZ +
			255 * sizeof(struct cxlmi_cmd_fmapi_ld_allocations_list)];
	struct sdb_tunnel_req_hdr *req_hdr;
	struct cxlmi_cci_msg *req_msg;
	struct cxlmi_cci_msg *inner_rsp;
	struct cxlmi_cmd_fmapi_set_ld_allocations_rsp *wire_rsp;
	struct cxlmi_cmd_fmapi_set_ld_allocations_rsp *host_rsp;
	size_t payload_len, req_buf_sz, rsp_buf_sz, list_bytes;
	uint8_t port_id = 0, number_ld;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (set_argc >= (int)(sizeof(set_argv) / sizeof(set_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel fm-set-ld-alloc: too many arguments\n");
				return -1;
			}
			set_argv[set_argc++] = argv[i];
		}
	}

	rc = parse_fm_set_ld_alloc_req(set_argc, set_argv, &params);
	if (rc)
		return rc;

	rc = read_hex_payload_file(params.input_file, payload,
				   sizeof(payload), &payload_len);
	if (rc == -1) {
		fprintf(stderr, "sdb-tunnel fm-set-ld-alloc: cannot open '%s': ",
			params.input_file);
		perror(NULL);
		return -1;
	}
	if (rc == -2) {
		fprintf(stderr,
			"sdb-tunnel fm-set-ld-alloc: invalid hex in '%s'\n",
			params.input_file);
		return -1;
	}
	if (rc == -3) {
		fprintf(stderr,
			"sdb-tunnel fm-set-ld-alloc: payload in '%s' exceeds maximum size\n",
			params.input_file);
		return -1;
	}

	if (payload_len < MBCCI_FM_GET_LD_ALLOC_HDR_SZ ||
	    (payload_len - MBCCI_FM_GET_LD_ALLOC_HDR_SZ) %
	    sizeof(struct cxlmi_cmd_fmapi_ld_allocations_list)) {
		fprintf(stderr,
			"sdb-tunnel fm-set-ld-alloc: payload size %zu is not "
			"4 + N*16 bytes\n", payload_len);
		return -1;
	}

	rc = fm_ld_alloc_normalize_set_payload(payload, payload_len, &params,
					       &number_ld);
	if (rc) {
		fprintf(stderr,
			"sdb-tunnel fm-set-ld-alloc: payload in '%s' is neither "
			"get-ld-alloc response nor set-ld-alloc request format\n",
			params.input_file);
		return -1;
	}

	if (fm_set_ld_alloc_payload_size(number_ld) != payload_len) {
		fprintf(stderr,
			"sdb-tunnel fm-set-ld-alloc: number_ld %u does not match "
			"payload size %zu\n", number_ld, payload_len);
		return -1;
	}

	req_buf_sz = sizeof(struct sdb_tunnel_req_hdr) +
		     sizeof(struct cxlmi_cci_msg) + payload_len;
	list_bytes = number_ld * sizeof(struct cxlmi_cmd_fmapi_ld_allocations_list);
	rsp_buf_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
		     sizeof(struct cxlmi_cci_msg) +
		     sizeof(struct cxlmi_cmd_fmapi_set_ld_allocations_rsp) +
		     list_bytes;

	req_buf = calloc(1, req_buf_sz);
	rsp_buf = calloc(1, rsp_buf_sz);
	host_buf = calloc(1, sizeof(struct cxlmi_cmd_fmapi_set_ld_allocations_rsp) +
			       list_bytes);
	if (!req_buf || !rsp_buf || !host_buf) {
		fprintf(stderr, "sdb-tunnel fm-set-ld-alloc: out of memory\n");
		free(req_buf);
		free(rsp_buf);
		free(host_buf);
		return -1;
	}

	req_hdr = (struct sdb_tunnel_req_hdr *)req_buf;
	req_msg = (struct cxlmi_cci_msg *)(req_buf + sizeof(*req_hdr));

	req_hdr->id           = port_id;
	req_hdr->target_type  = 0;
	req_hdr->command_size = (uint16_t)(sizeof(*req_msg) + payload_len);

	req_msg->command     = 0x02; /* SET_LD_ALLOCATIONS */
	req_msg->command_set = 0x54; /* MLD_COMPONENTS     */
	req_msg->pl_length[0] = (uint8_t)(payload_len & 0xff);
	req_msg->pl_length[1] = (uint8_t)((payload_len >> 8) & 0xff);
	req_msg->pl_length[2] = (uint8_t)((payload_len >> 16) & 0xff);
	memcpy(req_msg->payload, payload, payload_len);

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", req_buf, req_buf_sz);

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       req_buf, req_buf_sz,
				       rsp_buf, rsp_buf_sz);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel fm-set-ld-alloc failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel fm-set-ld-alloc ioctl failed\n");
		free(req_buf);
		free(rsp_buf);
		free(host_buf);
		return rc;
	}

	dump_hex("sdb-tunnel RX", rsp_buf, rsp_buf_sz);

	inner_rsp = (struct cxlmi_cci_msg *)(rsp_buf + sizeof(struct sdb_tunnel_rsp_hdr));
	if (inner_rsp->return_code != 0) {
		uint16_t err = inner_rsp->return_code;

		fprintf(stderr,
			"sdb-tunnel fm-set-ld-alloc: inner CCI error 0x%04x\n",
			err);
		free(req_buf);
		free(rsp_buf);
		free(host_buf);
		return (int)err;
	}

	wire_rsp = (struct cxlmi_cmd_fmapi_set_ld_allocations_rsp *)inner_rsp->payload;
	host_rsp = (struct cxlmi_cmd_fmapi_set_ld_allocations_rsp *)host_buf;
	sdb_parse_fm_set_ld_alloc_rsp(wire_rsp, host_rsp, number_ld);
	print_fm_set_ld_alloc(host_rsp);

	free(req_buf);
	free(rsp_buf);
	free(host_buf);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel fm-get-qos-ctrl (inner opcode 0x5403)                 */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_fm_get_qos_ctrl(struct cxlmi_endpoint *ep,
				      int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_fmapi_get_qos_control_rsp rsp;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_fmapi_get_qos_control_rsp qos;
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
				"Usage: sdb-tunnel fm-get-qos-ctrl [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x03; /* GET_QOS_CONTROL */
	req.msg.command_set = 0x54; /* MLD_COMPONENTS  */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel fm-get-qos-ctrl failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel fm-get-qos-ctrl ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel fm-get-qos-ctrl: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	fm_qos_ctrl_wire_to_host(&rsp.rsp, &qos);
	print_fm_qos_control(&qos);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel fm-set-qos-ctrl (inner opcode 0x5404)                 */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_fm_set_qos_ctrl(struct cxlmi_endpoint *ep,
				      int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr                         hdr;
		struct cxlmi_cci_msg                              msg;
		struct cxlmi_cmd_fmapi_set_qos_control_req        payload;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_fmapi_set_qos_control_rsp rsp;
	} __attribute__((packed)) rsp;

	struct fm_set_qos_ctrl_params params;
	struct cxlmi_cmd_fmapi_get_qos_control_rsp host_rsp;
	char *ctrl_argv[16];
	int ctrl_argc = 0;
	uint8_t input_buf[sizeof(struct cxlmi_cmd_fmapi_set_qos_control_req)];
	size_t input_len;
	uint8_t port_id = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (ctrl_argc >= (int)(sizeof(ctrl_argv) / sizeof(ctrl_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel fm-set-qos-ctrl: too many arguments\n");
				return -1;
			}
			ctrl_argv[ctrl_argc++] = argv[i];
		}
	}

	rc = parse_fm_set_qos_ctrl_req(ctrl_argc, ctrl_argv, &params);
	if (rc)
		return rc;

	if (params.input_file) {
		rc = read_hex_payload_file(params.input_file, input_buf,
					   sizeof(input_buf), &input_len);
		if (rc == -1) {
			fprintf(stderr, "sdb-tunnel fm-set-qos-ctrl: cannot open '%s': ",
				params.input_file);
			perror(NULL);
			return -1;
		}
		if (rc == -2) {
			fprintf(stderr,
				"sdb-tunnel fm-set-qos-ctrl: invalid hex in '%s'\n",
				params.input_file);
			return -1;
		}
		if (rc == -3 || fm_qos_ctrl_parse_input_payload(input_buf, input_len,
								&params.req)) {
			fprintf(stderr,
				"sdb-tunnel fm-set-qos-ctrl: payload in '%s' must be "
				"7 bytes\n", params.input_file);
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.payload));

	req.msg.command     = 0x04; /* SET_QOS_CONTROL */
	req.msg.command_set = 0x54; /* MLD_COMPONENTS  */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] = (uint8_t)((sizeof(req.payload) >> 8) & 0xff);

	fm_qos_ctrl_host_to_wire(&params.req, &req.payload);

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel fm-set-qos-ctrl failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel fm-set-qos-ctrl ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel fm-set-qos-ctrl: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	fm_qos_ctrl_wire_to_host((const struct cxlmi_cmd_fmapi_get_qos_control_rsp *)&rsp.rsp,
				 &host_rsp);
	print_fm_qos_control(&host_rsp);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel fm-get-qos-status (inner opcode 0x5405)               */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_fm_get_qos_status(struct cxlmi_endpoint *ep,
					int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr  hdr;
		struct cxlmi_cci_msg       msg;
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr  hdr;
		struct cxlmi_cci_msg       msg;
		struct cxlmi_cmd_fmapi_get_qos_status_rsp rsp;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_fmapi_get_qos_status_rsp status;
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
				"Usage: sdb-tunnel fm-get-qos-status [--port <vdm0|vdm1|i3c>]\n");
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = sizeof(req.msg);

	req.msg.command     = 0x05; /* GET_QOS_STATUS */
	req.msg.command_set = 0x54; /* MLD_COMPONENTS */

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel fm-get-qos-status failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel fm-get-qos-status ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel fm-get-qos-status: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	status.backpressure_avg_percentage = rsp.rsp.backpressure_avg_percentage;
	print_fm_qos_status(&status);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel fm-get-qos-alloc-bw (inner opcode 0x5406)             */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_fm_get_qos_alloc_bw(struct cxlmi_endpoint *ep,
					  int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr                         hdr;
		struct cxlmi_cci_msg                              msg;
		struct cxlmi_cmd_fmapi_get_qos_allocated_bw_req   payload;
	} __attribute__((packed)) req;

	struct fm_get_qos_ld_params params;
	char *get_argv[16];
	int get_argc = 0;
	uint8_t *rsp_buf = NULL;
	uint8_t *host_buf = NULL;
	struct cxlmi_cci_msg *inner_rsp;
	struct cxlmi_cmd_fmapi_get_qos_allocated_bw_rsp *wire_rsp;
	struct cxlmi_cmd_fmapi_get_qos_allocated_bw_rsp *host_rsp;
	size_t rsp_buf_sz;
	uint8_t port_id = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (get_argc >= (int)(sizeof(get_argv) / sizeof(get_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel fm-get-qos-alloc-bw: too many arguments\n");
				return -1;
			}
			get_argv[get_argc++] = argv[i];
		}
	}

	rc = parse_fm_get_qos_ld_req(get_argc, get_argv, &params);
	if (rc)
		return rc;

	rsp_buf_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
		     sizeof(struct cxlmi_cci_msg) +
		     fm_qos_ld_payload_size(params.req.number_ld);

	rsp_buf = calloc(1, rsp_buf_sz);
	host_buf = calloc(1, fm_qos_ld_payload_size(params.req.number_ld));
	if (!rsp_buf || !host_buf) {
		fprintf(stderr, "sdb-tunnel fm-get-qos-alloc-bw: out of memory\n");
		free(rsp_buf);
		free(host_buf);
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.payload));

	req.msg.command     = 0x06; /* GET_QOS_ALLOCATED_BW */
	req.msg.command_set = 0x54; /* MLD_COMPONENTS       */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] = (uint8_t)((sizeof(req.payload) >> 8) & 0xff);

	req.payload.number_ld = params.req.number_ld;
	req.payload.start_ld_id = params.req.start_ld_id;

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       rsp_buf, rsp_buf_sz);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel fm-get-qos-alloc-bw failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel fm-get-qos-alloc-bw ioctl failed\n");
		free(rsp_buf);
		free(host_buf);
		return rc;
	}

	dump_hex("sdb-tunnel RX", rsp_buf, rsp_buf_sz);

	inner_rsp = (struct cxlmi_cci_msg *)(rsp_buf + sizeof(struct sdb_tunnel_rsp_hdr));
	if (inner_rsp->return_code != 0) {
		uint16_t err = inner_rsp->return_code;

		fprintf(stderr,
			"sdb-tunnel fm-get-qos-alloc-bw: inner CCI error 0x%04x\n",
			err);
		free(rsp_buf);
		free(host_buf);
		return (int)err;
	}

	wire_rsp = (struct cxlmi_cmd_fmapi_get_qos_allocated_bw_rsp *)inner_rsp->payload;
	host_rsp = (struct cxlmi_cmd_fmapi_get_qos_allocated_bw_rsp *)host_buf;
	host_rsp->number_ld = wire_rsp->number_ld;
	host_rsp->start_ld_id = wire_rsp->start_ld_id;
	memcpy(host_rsp->qos_allocation_fraction, wire_rsp->qos_allocation_fraction,
	       host_rsp->number_ld);
	print_fm_qos_allocated_bw(host_rsp);

	if (params.raw_dump_file) {
		size_t pl_len = inner_rsp->pl_length[0] |
				((size_t)inner_rsp->pl_length[1] << 8) |
				((size_t)(inner_rsp->pl_length[2] & 0x0f) << 16);

		if (!pl_len)
			pl_len = fm_qos_ld_payload_size(host_rsp->number_ld);

		rc = write_hex_payload_file(params.raw_dump_file,
					    (const uint8_t *)wire_rsp, pl_len);
		if (rc) {
			fprintf(stderr,
				"sdb-tunnel fm-get-qos-alloc-bw: failed to write '%s': ",
				params.raw_dump_file);
			perror(NULL);
			free(rsp_buf);
			free(host_buf);
			return -1;
		}
		printf("Raw dump written to %s (%zu bytes, get-qos-alloc-bw response payload)\n",
		       params.raw_dump_file, pl_len);
	}

	free(rsp_buf);
	free(host_buf);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel fm-set-qos-alloc-bw (inner opcode 0x5407)             */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_fm_set_qos_alloc_bw(struct cxlmi_endpoint *ep,
					  int argc, char **argv)
{
	struct fm_set_qos_ld_params params;
	char *set_argv[16];
	int set_argc = 0;
	uint8_t *req_buf = NULL;
	uint8_t *rsp_buf = NULL;
	uint8_t *host_buf = NULL;
	uint8_t payload[MBCCI_FM_QOS_LD_HDR_SZ + 255];
	struct sdb_tunnel_req_hdr *req_hdr;
	struct cxlmi_cci_msg *req_msg;
	struct cxlmi_cci_msg *inner_rsp;
	struct cxlmi_cmd_fmapi_set_qos_allocated_bw_rsp *wire_rsp;
	struct cxlmi_cmd_fmapi_set_qos_allocated_bw_rsp *host_rsp;
	size_t payload_len, req_buf_sz, rsp_buf_sz;
	uint8_t port_id = 0, number_ld;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (set_argc >= (int)(sizeof(set_argv) / sizeof(set_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel fm-set-qos-alloc-bw: too many arguments\n");
				return -1;
			}
			set_argv[set_argc++] = argv[i];
		}
	}

	rc = parse_fm_set_qos_ld_req(set_argc, set_argv, &params);
	if (rc)
		return rc;

	rc = read_hex_payload_file(params.input_file, payload,
				   sizeof(payload), &payload_len);
	if (rc == -1) {
		fprintf(stderr, "sdb-tunnel fm-set-qos-alloc-bw: cannot open '%s': ",
			params.input_file);
		perror(NULL);
		return -1;
	}
	if (rc == -2) {
		fprintf(stderr,
			"sdb-tunnel fm-set-qos-alloc-bw: invalid hex in '%s'\n",
			params.input_file);
		return -1;
	}
	if (rc == -3) {
		fprintf(stderr,
			"sdb-tunnel fm-set-qos-alloc-bw: payload in '%s' exceeds maximum size\n",
			params.input_file);
		return -1;
	}

	if (payload_len < MBCCI_FM_QOS_LD_HDR_SZ) {
		fprintf(stderr,
			"sdb-tunnel fm-set-qos-alloc-bw: payload size %zu is too small\n",
			payload_len);
		return -1;
	}

	rc = fm_qos_ld_apply_set_overrides(payload, payload_len, &params,
					   &number_ld);
	if (rc) {
		fprintf(stderr,
			"sdb-tunnel fm-set-qos-alloc-bw: payload in '%s' is not "
			"2 + N bytes\n", params.input_file);
		return -1;
	}

	req_buf_sz = sizeof(struct sdb_tunnel_req_hdr) +
		     sizeof(struct cxlmi_cci_msg) + payload_len;
	rsp_buf_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
		     sizeof(struct cxlmi_cci_msg) +
		     fm_qos_ld_payload_size(number_ld);

	req_buf = calloc(1, req_buf_sz);
	rsp_buf = calloc(1, rsp_buf_sz);
	host_buf = calloc(1, fm_qos_ld_payload_size(number_ld));
	if (!req_buf || !rsp_buf || !host_buf) {
		fprintf(stderr, "sdb-tunnel fm-set-qos-alloc-bw: out of memory\n");
		free(req_buf);
		free(rsp_buf);
		free(host_buf);
		return -1;
	}

	req_hdr = (struct sdb_tunnel_req_hdr *)req_buf;
	req_msg = (struct cxlmi_cci_msg *)(req_buf + sizeof(*req_hdr));

	req_hdr->id           = port_id;
	req_hdr->target_type  = 0;
	req_hdr->command_size = (uint16_t)(sizeof(*req_msg) + payload_len);

	req_msg->command     = 0x07; /* SET_QOS_ALLOCATED_BW */
	req_msg->command_set = 0x54; /* MLD_COMPONENTS       */
	req_msg->pl_length[0] = (uint8_t)(payload_len & 0xff);
	req_msg->pl_length[1] = (uint8_t)((payload_len >> 8) & 0xff);
	req_msg->pl_length[2] = (uint8_t)((payload_len >> 16) & 0xff);
	memcpy(req_msg->payload, payload, payload_len);

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", req_buf, req_buf_sz);

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       req_buf, req_buf_sz,
				       rsp_buf, rsp_buf_sz);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel fm-set-qos-alloc-bw failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel fm-set-qos-alloc-bw ioctl failed\n");
		free(req_buf);
		free(rsp_buf);
		free(host_buf);
		return rc;
	}

	dump_hex("sdb-tunnel RX", rsp_buf, rsp_buf_sz);

	inner_rsp = (struct cxlmi_cci_msg *)(rsp_buf + sizeof(struct sdb_tunnel_rsp_hdr));
	if (inner_rsp->return_code != 0) {
		uint16_t err = inner_rsp->return_code;

		fprintf(stderr,
			"sdb-tunnel fm-set-qos-alloc-bw: inner CCI error 0x%04x\n",
			err);
		free(req_buf);
		free(rsp_buf);
		free(host_buf);
		return (int)err;
	}

	wire_rsp = (struct cxlmi_cmd_fmapi_set_qos_allocated_bw_rsp *)inner_rsp->payload;
	host_rsp = (struct cxlmi_cmd_fmapi_set_qos_allocated_bw_rsp *)host_buf;
	host_rsp->number_ld = wire_rsp->number_ld;
	host_rsp->start_ld_id = wire_rsp->start_ld_id;
	memcpy(host_rsp->qos_allocation_fraction, wire_rsp->qos_allocation_fraction,
	       host_rsp->number_ld);
	print_fm_qos_allocated_bw((const struct cxlmi_cmd_fmapi_get_qos_allocated_bw_rsp *)host_rsp);

	free(req_buf);
	free(rsp_buf);
	free(host_buf);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel fm-get-qos-bw-limit (inner opcode 0x5408)             */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_fm_get_qos_bw_limit(struct cxlmi_endpoint *ep,
					  int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr                     hdr;
		struct cxlmi_cci_msg                          msg;
		struct cxlmi_cmd_fmapi_get_qos_bw_limit_req   payload;
	} __attribute__((packed)) req;

	struct fm_get_qos_ld_params params;
	char *get_argv[16];
	int get_argc = 0;
	uint8_t *rsp_buf = NULL;
	uint8_t *host_buf = NULL;
	struct cxlmi_cci_msg *inner_rsp;
	struct cxlmi_cmd_fmapi_get_qos_bw_limit_rsp *wire_rsp;
	struct cxlmi_cmd_fmapi_get_qos_bw_limit_rsp *host_rsp;
	size_t rsp_buf_sz;
	uint8_t port_id = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (get_argc >= (int)(sizeof(get_argv) / sizeof(get_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel fm-get-qos-bw-limit: too many arguments\n");
				return -1;
			}
			get_argv[get_argc++] = argv[i];
		}
	}

	rc = parse_fm_get_qos_ld_req(get_argc, get_argv, &params);
	if (rc)
		return rc;

	rsp_buf_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
		     sizeof(struct cxlmi_cci_msg) +
		     fm_qos_ld_payload_size(params.req.number_ld);

	rsp_buf = calloc(1, rsp_buf_sz);
	host_buf = calloc(1, fm_qos_ld_payload_size(params.req.number_ld));
	if (!rsp_buf || !host_buf) {
		fprintf(stderr, "sdb-tunnel fm-get-qos-bw-limit: out of memory\n");
		free(rsp_buf);
		free(host_buf);
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.payload));

	req.msg.command     = 0x08; /* GET_QOS_BW_LIMIT */
	req.msg.command_set = 0x54; /* MLD_COMPONENTS   */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] = (uint8_t)((sizeof(req.payload) >> 8) & 0xff);

	req.payload.number_ld = params.req.number_ld;
	req.payload.start_ld_id = params.req.start_ld_id;

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       rsp_buf, rsp_buf_sz);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel fm-get-qos-bw-limit failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel fm-get-qos-bw-limit ioctl failed\n");
		free(rsp_buf);
		free(host_buf);
		return rc;
	}

	dump_hex("sdb-tunnel RX", rsp_buf, rsp_buf_sz);

	inner_rsp = (struct cxlmi_cci_msg *)(rsp_buf + sizeof(struct sdb_tunnel_rsp_hdr));
	if (inner_rsp->return_code != 0) {
		uint16_t err = inner_rsp->return_code;

		fprintf(stderr,
			"sdb-tunnel fm-get-qos-bw-limit: inner CCI error 0x%04x\n",
			err);
		free(rsp_buf);
		free(host_buf);
		return (int)err;
	}

	wire_rsp = (struct cxlmi_cmd_fmapi_get_qos_bw_limit_rsp *)inner_rsp->payload;
	host_rsp = (struct cxlmi_cmd_fmapi_get_qos_bw_limit_rsp *)host_buf;
	host_rsp->number_ld = wire_rsp->number_ld;
	host_rsp->start_ld_id = wire_rsp->start_ld_id;
	memcpy(host_rsp->qos_limit_fraction, wire_rsp->qos_limit_fraction,
	       host_rsp->number_ld);
	print_fm_qos_bw_limit(host_rsp);

	if (params.raw_dump_file) {
		size_t pl_len = inner_rsp->pl_length[0] |
				((size_t)inner_rsp->pl_length[1] << 8) |
				((size_t)(inner_rsp->pl_length[2] & 0x0f) << 16);

		if (!pl_len)
			pl_len = fm_qos_ld_payload_size(host_rsp->number_ld);

		rc = write_hex_payload_file(params.raw_dump_file,
					    (const uint8_t *)wire_rsp, pl_len);
		if (rc) {
			fprintf(stderr,
				"sdb-tunnel fm-get-qos-bw-limit: failed to write '%s': ",
				params.raw_dump_file);
			perror(NULL);
			free(rsp_buf);
			free(host_buf);
			return -1;
		}
		printf("Raw dump written to %s (%zu bytes, get-qos-bw-limit response payload)\n",
		       params.raw_dump_file, pl_len);
	}

	free(rsp_buf);
	free(host_buf);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel fm-set-qos-bw-limit (inner opcode 0x5409)             */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_fm_set_qos_bw_limit(struct cxlmi_endpoint *ep,
					  int argc, char **argv)
{
	struct fm_set_qos_ld_params params;
	char *set_argv[16];
	int set_argc = 0;
	uint8_t *req_buf = NULL;
	uint8_t *rsp_buf = NULL;
	uint8_t *host_buf = NULL;
	uint8_t payload[MBCCI_FM_QOS_LD_HDR_SZ + 255];
	struct sdb_tunnel_req_hdr *req_hdr;
	struct cxlmi_cci_msg *req_msg;
	struct cxlmi_cci_msg *inner_rsp;
	struct cxlmi_cmd_fmapi_set_qos_bw_limit_rsp *wire_rsp;
	struct cxlmi_cmd_fmapi_set_qos_bw_limit_rsp *host_rsp;
	size_t payload_len, req_buf_sz, rsp_buf_sz;
	uint8_t port_id = 0, number_ld;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (set_argc >= (int)(sizeof(set_argv) / sizeof(set_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel fm-set-qos-bw-limit: too many arguments\n");
				return -1;
			}
			set_argv[set_argc++] = argv[i];
		}
	}

	rc = parse_fm_set_qos_ld_req(set_argc, set_argv, &params);
	if (rc)
		return rc;

	rc = read_hex_payload_file(params.input_file, payload,
				   sizeof(payload), &payload_len);
	if (rc == -1) {
		fprintf(stderr, "sdb-tunnel fm-set-qos-bw-limit: cannot open '%s': ",
			params.input_file);
		perror(NULL);
		return -1;
	}
	if (rc == -2) {
		fprintf(stderr,
			"sdb-tunnel fm-set-qos-bw-limit: invalid hex in '%s'\n",
			params.input_file);
		return -1;
	}
	if (rc == -3) {
		fprintf(stderr,
			"sdb-tunnel fm-set-qos-bw-limit: payload in '%s' exceeds maximum size\n",
			params.input_file);
		return -1;
	}

	if (payload_len < MBCCI_FM_QOS_LD_HDR_SZ) {
		fprintf(stderr,
			"sdb-tunnel fm-set-qos-bw-limit: payload size %zu is too small\n",
			payload_len);
		return -1;
	}

	rc = fm_qos_ld_apply_set_overrides(payload, payload_len, &params,
					   &number_ld);
	if (rc) {
		fprintf(stderr,
			"sdb-tunnel fm-set-qos-bw-limit: payload in '%s' is not "
			"2 + N bytes\n", params.input_file);
		return -1;
	}

	req_buf_sz = sizeof(struct sdb_tunnel_req_hdr) +
		     sizeof(struct cxlmi_cci_msg) + payload_len;
	rsp_buf_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
		     sizeof(struct cxlmi_cci_msg) +
		     fm_qos_ld_payload_size(number_ld);

	req_buf = calloc(1, req_buf_sz);
	rsp_buf = calloc(1, rsp_buf_sz);
	host_buf = calloc(1, fm_qos_ld_payload_size(number_ld));
	if (!req_buf || !rsp_buf || !host_buf) {
		fprintf(stderr, "sdb-tunnel fm-set-qos-bw-limit: out of memory\n");
		free(req_buf);
		free(rsp_buf);
		free(host_buf);
		return -1;
	}

	req_hdr = (struct sdb_tunnel_req_hdr *)req_buf;
	req_msg = (struct cxlmi_cci_msg *)(req_buf + sizeof(*req_hdr));

	req_hdr->id           = port_id;
	req_hdr->target_type  = 0;
	req_hdr->command_size = (uint16_t)(sizeof(*req_msg) + payload_len);

	req_msg->command     = 0x09; /* SET_QOS_BW_LIMIT */
	req_msg->command_set = 0x54; /* MLD_COMPONENTS   */
	req_msg->pl_length[0] = (uint8_t)(payload_len & 0xff);
	req_msg->pl_length[1] = (uint8_t)((payload_len >> 8) & 0xff);
	req_msg->pl_length[2] = (uint8_t)((payload_len >> 16) & 0xff);
	memcpy(req_msg->payload, payload, payload_len);

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", req_buf, req_buf_sz);

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       req_buf, req_buf_sz,
				       rsp_buf, rsp_buf_sz);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel fm-set-qos-bw-limit failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel fm-set-qos-bw-limit ioctl failed\n");
		free(req_buf);
		free(rsp_buf);
		free(host_buf);
		return rc;
	}

	dump_hex("sdb-tunnel RX", rsp_buf, rsp_buf_sz);

	inner_rsp = (struct cxlmi_cci_msg *)(rsp_buf + sizeof(struct sdb_tunnel_rsp_hdr));
	if (inner_rsp->return_code != 0) {
		uint16_t err = inner_rsp->return_code;

		fprintf(stderr,
			"sdb-tunnel fm-set-qos-bw-limit: inner CCI error 0x%04x\n",
			err);
		free(req_buf);
		free(rsp_buf);
		free(host_buf);
		return (int)err;
	}

	wire_rsp = (struct cxlmi_cmd_fmapi_set_qos_bw_limit_rsp *)inner_rsp->payload;
	host_rsp = (struct cxlmi_cmd_fmapi_set_qos_bw_limit_rsp *)host_buf;
	host_rsp->number_ld = wire_rsp->number_ld;
	host_rsp->start_ld_id = wire_rsp->start_ld_id;
	memcpy(host_rsp->qos_limit_fraction, wire_rsp->qos_limit_fraction,
	       host_rsp->number_ld);
	print_fm_qos_bw_limit((const struct cxlmi_cmd_fmapi_get_qos_bw_limit_rsp *)host_rsp);

	free(req_buf);
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
/* sdb-tunnel get-feature (inner opcode 0x0501)                     */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_fetch_feature(struct cxlmi_endpoint *ep, uint8_t port_id,
				    const struct cxlmi_cmd_get_feature_req *in,
				    uint8_t *out, uint16_t count)
{
	struct {
		struct sdb_tunnel_req_hdr           hdr;
		struct cxlmi_cci_msg                msg;
		struct cxlmi_cmd_get_feature_req    payload;
	} __attribute__((packed)) req;

	uint8_t *rsp_buf;
	struct cxlmi_cci_msg *inner_rsp;
	size_t rsp_buf_sz;
	int rc;

	rsp_buf_sz = sizeof(struct sdb_tunnel_rsp_hdr) +
		     sizeof(struct cxlmi_cci_msg) +
		     count;
	rsp_buf = calloc(1, rsp_buf_sz);
	if (!rsp_buf)
		return -1;

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.payload));

	req.msg.command     = 0x01; /* GET_FEATURE */
	req.msg.command_set = 0x05; /* FEATURES    */
	req.msg.pl_length[0] = (uint8_t)(sizeof(req.payload) & 0xff);
	req.msg.pl_length[1] = (uint8_t)((sizeof(req.payload) >> 8) & 0xff);
	req.msg.pl_length[2] = (uint8_t)((sizeof(req.payload) >> 16) & 0xff);

	memcpy(req.payload.feature_id, in->feature_id, sizeof(in->feature_id));
	req.payload.offset = cpu_to_le16(in->offset);
	req.payload.count = cpu_to_le16(in->count);
	req.payload.selection = in->selection;

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       rsp_buf, rsp_buf_sz);
	if (rc) {
		free(rsp_buf);
		return rc;
	}

	dump_hex("sdb-tunnel RX", rsp_buf, rsp_buf_sz);

	inner_rsp = (struct cxlmi_cci_msg *)(rsp_buf +
					    sizeof(struct sdb_tunnel_rsp_hdr));
	if (inner_rsp->return_code != 0) {
		rc = (int)inner_rsp->return_code;
		free(rsp_buf);
		return rc;
	}

	memcpy(out, inner_rsp->payload, count);
	free(rsp_buf);
	return 0;
}

static int sdb_tunnel_get_feature(struct cxlmi_endpoint *ep,
				    int argc, char **argv)
{
	struct cxlmi_cmd_get_supported_features_req sf_req = { 0 };
	struct cxlmi_cmd_get_supported_features_rsp *sfrsp;
	struct get_feature_params params;
	char *feat_argv[16];
	int feat_argc = 0;
	uint8_t *data = NULL;
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
					"sdb-tunnel get-feature: too many arguments\n");
				return -1;
			}
			feat_argv[feat_argc++] = argv[i];
		}
	}

	rc = parse_get_feature_req(feat_argc, feat_argv, &params);
	if (rc)
		return rc;

	if (!params.has_count) {
		params.req.count = lookup_feature_size_doc(params.req.feature_id);
		if (!params.req.count) {
			sf_req.count = MBCCI_FEATURE_DEFAULT_COUNT;
			sf_req.starting_feature_index = 0;

			sfrsp = calloc(1, MBCCI_FEATURE_RSP_BUF_SZ(sf_req.count));
			if (!sfrsp) {
				fprintf(stderr, "sdb-tunnel get-feature: out of memory\n");
				return -1;
			}

			rc = sdb_tunnel_fetch_supported_features(ep, port_id, &sf_req,
								 sfrsp);
			if (rc) {
				if (rc > 0)
					fprintf(stderr,
						"sdb-tunnel get-feature: get-supported-feat failed: %s\n",
						cxlmi_cmd_retcode_tostr(rc));
				else
					fprintf(stderr,
						"sdb-tunnel get-feature: get-supported-feat ioctl failed\n");
				free(sfrsp);
				return rc;
			}

			params.req.count = lookup_feature_size(sfrsp,
							       params.req.feature_id);
			free(sfrsp);
		}

		if (params.req.count == 0) {
			fprintf(stderr,
				"sdb-tunnel get-feature: cannot determine feature data size "
				"(not in supported-features list; use --count <bytes>)\n");
			return -1;
		}
	}

	data = calloc(1, params.req.count);
	if (!data) {
		fprintf(stderr, "sdb-tunnel get-feature: out of memory\n");
		return -1;
	}

	rc = sdb_tunnel_fetch_feature(ep, port_id, &params.req, data,
				      params.req.count);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel get-feature failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel get-feature ioctl failed\n");
		free(data);
		return rc;
	}

	print_feature_header(&params.req);

	if (params.dump_file) {
		rc = write_hex_payload_file(params.dump_file, data,
					    params.req.count);
		if (rc) {
			fprintf(stderr, "sdb-tunnel get-feature: failed to write '%s': ",
				params.dump_file);
			perror(NULL);
			free(data);
			return -1;
		}
	}

	print_feature_data(params.req.feature_id, params.req.offset,
			   params.req.count, data);
	free(data);
	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel set-feature (inner opcode 0x0502)                       */
/* ------------------------------------------------------------------ */

#define SET_FEATURE_FIXED_SZ offsetof(struct cxlmi_cmd_set_feature_req, feature_data)

static int sdb_tunnel_set_feature(struct cxlmi_endpoint *ep,
				  int argc, char **argv)
{
	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) rsp;

	struct cxlmi_cmd_get_supported_features_req sf_req = { 0 };
	struct cxlmi_cmd_get_supported_features_rsp *sfrsp = NULL;
	struct set_feature_params params;
	struct cxlmi_cmd_set_feature_req *req_pl;
	struct sdb_tunnel_req_hdr *req_hdr;
	struct cxlmi_cci_msg *req_msg;
	uint8_t *req_buf = NULL;
	uint8_t payload[CXL_MAILBOX_MAX_PAYLOAD_SIZE];
	char *feat_argv[16];
	size_t payload_len = 0, req_buf_sz, pl_sz;
	int feat_argc = 0;
	uint16_t expected_size = 0;
	uint8_t port_id = 0, version = 0;
	int rc, i, need_sf = 0;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (feat_argc >= (int)(sizeof(feat_argv) / sizeof(feat_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel set-feature: too many arguments\n");
				return -1;
			}
			feat_argv[feat_argc++] = argv[i];
		}
	}

	rc = parse_set_feature_req(feat_argc, feat_argv, &params);
	if (rc)
		return rc;

	expected_size = lookup_set_feature_size_doc(params.feature_id);
	if (!expected_size)
		need_sf = 1;
	if (!params.has_version && !lookup_set_feature_version_doc(params.feature_id))
		need_sf = 1;

	if (need_sf) {
		sf_req.count = MBCCI_FEATURE_DEFAULT_COUNT;
		sf_req.starting_feature_index = 0;

		sfrsp = calloc(1, MBCCI_FEATURE_RSP_BUF_SZ(sf_req.count));
		if (!sfrsp) {
			fprintf(stderr, "sdb-tunnel set-feature: out of memory\n");
			return -1;
		}

		rc = sdb_tunnel_fetch_supported_features(ep, port_id, &sf_req,
						       sfrsp);
		if (rc) {
			if (rc > 0)
				fprintf(stderr,
					"sdb-tunnel set-feature: get-supported-feat failed: %s\n",
					cxlmi_cmd_retcode_tostr(rc));
			else
				fprintf(stderr,
					"sdb-tunnel set-feature: get-supported-feat ioctl failed\n");
			free(sfrsp);
			return rc;
		}

		if (!expected_size)
			expected_size = lookup_set_feature_size(sfrsp,
								params.feature_id);
	}

	if (expected_size == 0) {
		fprintf(stderr,
			"sdb-tunnel set-feature: feature not writable or cannot determine set_feature_size\n");
		free(sfrsp);
		return -1;
	}

	rc = read_hex_payload_file(params.input_file, payload,
				   sizeof(payload), &payload_len);
	if (rc == -1) {
		fprintf(stderr, "sdb-tunnel set-feature: cannot open '%s': ",
			params.input_file);
		perror(NULL);
		free(sfrsp);
		return -1;
	}
	if (rc == -2) {
		fprintf(stderr,
			"sdb-tunnel set-feature: invalid hex in '%s'\n",
			params.input_file);
		free(sfrsp);
		return -1;
	}
	if (rc == -3) {
		fprintf(stderr,
			"sdb-tunnel set-feature: payload in '%s' exceeds maximum size\n",
			params.input_file);
		free(sfrsp);
		return -1;
	}

	if (payload_len != expected_size) {
		fprintf(stderr,
			"sdb-tunnel set-feature: payload size %zu does not match expected %u bytes\n",
			payload_len, expected_size);
		free(sfrsp);
		return -1;
	}

	if (params.has_version)
		version = params.version;
	else {
		if (sfrsp)
			version = lookup_set_feature_version(sfrsp,
							     params.feature_id);
		if (!version)
			version = lookup_set_feature_version_doc(params.feature_id);
		if (!version) {
			fprintf(stderr,
				"sdb-tunnel set-feature: cannot determine set_feature_version "
				"(use --version <n>)\n");
			free(sfrsp);
			return -1;
		}
	}

	pl_sz = SET_FEATURE_FIXED_SZ + payload_len;
	req_buf_sz = sizeof(struct sdb_tunnel_req_hdr) +
		     sizeof(struct cxlmi_cci_msg) + pl_sz;
	req_buf = calloc(1, req_buf_sz);
	if (!req_buf) {
		fprintf(stderr, "sdb-tunnel set-feature: out of memory\n");
		free(sfrsp);
		return -1;
	}

	req_hdr = (struct sdb_tunnel_req_hdr *)req_buf;
	req_msg = (struct cxlmi_cci_msg *)(req_buf + sizeof(*req_hdr));
	req_pl = (struct cxlmi_cmd_set_feature_req *)(req_buf +
			sizeof(*req_hdr) + sizeof(*req_msg));

	req_hdr->id = port_id;
	req_hdr->target_type = 0;
	req_hdr->command_size = (uint16_t)(sizeof(*req_msg) + pl_sz);

	req_msg->command = 0x02; /* SET_FEATURE */
	req_msg->command_set = 0x05; /* FEATURES */
	req_msg->pl_length[0] = (uint8_t)(pl_sz & 0xff);
	req_msg->pl_length[1] = (uint8_t)((pl_sz >> 8) & 0xff);
	req_msg->pl_length[2] = (uint8_t)((pl_sz >> 16) & 0xff);

	memcpy(req_pl->feature_id, params.feature_id, 16);
	req_pl->set_feature_flags = cpu_to_le32(params.set_feature_flags);
	req_pl->offset = cpu_to_le16(params.offset);
	req_pl->version = version;
	memcpy(req_pl->feature_data, payload, payload_len);

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", req_buf, req_buf_sz);

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       req_buf, req_buf_sz,
				       &rsp, sizeof(rsp));
	free(req_buf);
	free(sfrsp);

	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel set-feature failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel set-feature ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel set-feature: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	printf("Set feature ");
	print_log_uuid(params.feature_id);
	printf(", %zu bytes at offset %u (port %u)\n",
	       payload_len, params.offset, port_id);
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
/* sdb-tunnel get-log-cap (inner opcode 0x0402)                       */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_get_log_cap(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct {
		struct sdb_tunnel_req_hdr hdr;
		struct cxlmi_cci_msg      msg;
		uint8_t                   uuid[16];
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
		uint32_t                  parameter_flags;
	} __attribute__((packed)) rsp;

	uint8_t uuid[16];
	char *log_argv[16];
	int log_argc = 0;
	uint8_t port_id = 0;
	int rc, i;
	uint32_t flags;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (log_argc >= (int)(sizeof(log_argv) / sizeof(log_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel get-log-cap: too many arguments\n");
				return -1;
			}
			log_argv[log_argc++] = argv[i];
		}
	}

	rc = parse_log_uuid_req(log_argc, log_argv, uuid,
				"Usage: sdb-tunnel get-log-cap [--port <vdm0|vdm1|i3c>] --uuid <uuid>\n"
				"  --uuid  log UUID (32-char hex or standard format)\n");
	if (rc)
		return rc;

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.uuid));
	req.msg.command      = 0x02; /* GET_LOG_CAPS */
	req.msg.command_set  = 0x04; /* LOGS */
	req.msg.pl_length[0] = sizeof(req.uuid);
	memcpy(req.uuid, uuid, sizeof(req.uuid));

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel get-log-cap failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel get-log-cap ioctl failed\n");
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel get-log-cap: inner CCI error 0x%04x\n",
			rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	flags = le32_to_cpu(rsp.parameter_flags);
	printf("Log capabilities for ");
	print_log_uuid(uuid);
	printf(" (port %u)\n", port_id);
	printf("  parameter_flags: 0x%08x\n", flags);
	printf("  clear_log_supported: %s\n", (flags & 0x01) ? "yes" : "no");
	printf("  populate_log_supported: %s\n", (flags & 0x02) ? "yes" : "no");
	printf("  auto_populate_log_supported: %s\n",
	       (flags & 0x04) ? "yes" : "no");
	printf("  persistent_across_cold_reset: %s\n",
	       (flags & 0x08) ? "yes" : "no");

	return 0;
}

/* ------------------------------------------------------------------ */
/* sdb-tunnel clear-log / populate-log (inner opcode 0x0403 / 0x0404) */
/* ------------------------------------------------------------------ */

static int sdb_tunnel_log_uuid_cmd(struct cxlmi_endpoint *ep, int argc,
				   char **argv, uint8_t inner_cmd,
				   const char *cmd_name, const char *usage,
				   const char *action)
{
	struct {
		struct sdb_tunnel_req_hdr hdr;
		struct cxlmi_cci_msg      msg;
		uint8_t                   uuid[16];
	} __attribute__((packed)) req;

	struct {
		struct sdb_tunnel_rsp_hdr hdr;
		struct cxlmi_cci_msg      msg;
	} __attribute__((packed)) rsp;

	uint8_t uuid[16];
	char *log_argv[16];
	int log_argc = 0;
	uint8_t port_id = 0;
	int rc, i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			rc = parse_port_id(argv[++i]);
			if (rc < 0)
				return -1;
			port_id = (uint8_t)rc;
		} else {
			if (log_argc >= (int)(sizeof(log_argv) / sizeof(log_argv[0]))) {
				fprintf(stderr,
					"sdb-tunnel %s: too many arguments\n",
					cmd_name);
				return -1;
			}
			log_argv[log_argc++] = argv[i];
		}
	}

	rc = parse_log_uuid_req(log_argc, log_argv, uuid, usage);
	if (rc)
		return rc;

	memset(&req, 0, sizeof(req));
	req.hdr.id           = port_id;
	req.hdr.target_type  = 0;
	req.hdr.command_size = (uint16_t)(sizeof(req.msg) + sizeof(req.uuid));

	req.msg.command     = inner_cmd;
	req.msg.command_set = 0x04; /* LOGS */
	req.msg.pl_length[0] = sizeof(req.uuid);
	memcpy(req.uuid, uuid, sizeof(req.uuid));

	memset(&rsp, 0, sizeof(rsp));

	dump_hex("sdb-tunnel TX (opcode=0xCCCC)", &req, sizeof(req));

	rc = cxlmi_cmd_vendor_specific(ep, NULL, SDB_TUNNEL_OPCODE,
				       &req, sizeof(req),
				       &rsp, sizeof(rsp));
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "sdb-tunnel %s failed: %s\n",
				cmd_name, cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "sdb-tunnel %s ioctl failed\n",
				cmd_name);
		return rc;
	}

	dump_hex("sdb-tunnel RX", &rsp, sizeof(rsp));

	if (rsp.msg.return_code != 0) {
		fprintf(stderr,
			"sdb-tunnel %s: inner CCI error 0x%04x\n",
			cmd_name, rsp.msg.return_code);
		return (int)rsp.msg.return_code;
	}

	printf("%s log ", action);
	print_log_uuid(uuid);
	printf(" (port %u)\n", port_id);
	return 0;
}

static int sdb_tunnel_clear_log(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	return sdb_tunnel_log_uuid_cmd(ep, argc, argv, 0x03, "clear-log",
		"Usage: sdb-tunnel clear-log [--port <vdm0|vdm1|i3c>] --uuid <uuid>\n"
		"  --uuid  log UUID (32-char hex or standard format)\n",
		"Cleared");
}

static int sdb_tunnel_populate_log(struct cxlmi_endpoint *ep,
				   int argc, char **argv)
{
	return sdb_tunnel_log_uuid_cmd(ep, argc, argv, 0x04, "populate-log",
		"Usage: sdb-tunnel populate-log [--port <vdm0|vdm1|i3c>] --uuid <uuid>\n"
		"  --uuid  log UUID (32-char hex or standard format)\n",
		"Populated");
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
			"  vu-dlcfg           [--port <vdm0|vdm1|i3c>] --input <file> --cfg-type <DEV|DDR> [--chunk-size <n>]\n"
			"                                                                         VU Download Config (0xCC53/0x07)\n"
			"  vu-getdevcfg       [--port <vdm0|vdm1|i3c>] --output <file>\n"
			"                                                                         VU Get DEV Config (0xCC53/0x08)\n"
			"  vu-ddrfreq         [--port <vdm0|vdm1|i3c>] --freq <n>\n"
			"                                                                         VU Set DDR Freq (0xCC53/0x09)\n"
			"  vu-pciespeed       [--port <vdm0|vdm1|i3c>] --pcie-port <0|1> --speed <genN> --width <xN>\n"
			"                                                                         VU Set PCIe Speed (0xCC53/0x0a)\n"
			"  activate-fw        [--port <vdm0|vdm1|i3c>] --slot <n> [--action online|offline]\n"
			"                                                                         Activate FW (0x0202)\n"
			"  get-health-info    [--port <vdm0|vdm1|i3c>]                        Get Health Info (0x4200)\n"
			"  get-alert-config   [--port <vdm0|vdm1|i3c>]                        Get Alert Configuration (0x4201)\n"
			"  set-alert-config   [--port <vdm0|vdm1|i3c>] [--life-used-warning <pct>] [--over-temp-warning <n>] ...\n"
			"                                                                         Set Alert Configuration (0x4202)\n"
			"  get-poison-list   [--port <vdm0|vdm1|i3c>] --dpa <addr> --length <bytes> [--frestart]\n"
			"                                                                         Get Poison List (0x4300)\n"
			"  inject-poison     [--port <vdm0|vdm1|i3c>] --dpa <addr>             Inject Poison (0x4301)\n"
			"  clear-poison      [--port <vdm0|vdm1|i3c>] --dpa <addr> [--write-data <128-hex-digits>]\n"
			"                                                                         Clear Poison (0x4302)\n"
			"  get-scan-media-cap [--port <vdm0|vdm1|i3c>] --dpa <addr> --length <bytes>\n"
			"                                                                         Get Scan Media Capabilities (0x4303)\n"
			"  scan-media        [--port <vdm0|vdm1|i3c>] --dpa <addr> --length <bytes> [--no-evtlog]\n"
			"                                                                         Scan Media (0x4304)\n"
			"  get-scan-media-results [--port <vdm0|vdm1|i3c>]                     Get Scan Media Results (0x4305)\n"
			"  get-sld-qos-ctrl   [--port <vdm0|vdm1|i3c>]                        Get SLD QoS Control (0x4700)\n"
			"  set-sld-qos-ctrl   [--port <vdm0|vdm1|i3c>] [--egress-congestion-control-enable <0|1>] [--egress-tpr-enable <0|1>] ...\n"
			"                                                                         Set SLD QoS Control (0x4701)\n"
			"  get-sld-qos-status [--port <vdm0|vdm1|i3c>]                        Get SLD QoS Status (0x4702)\n"
			"  fm-get-ld-info     [--port <vdm0|vdm1|i3c>]                        FM Get LD Info (0x5400)\n"
			"  fm-get-ld-alloc    [--port <vdm0|vdm1|i3c>] [--start-ld-id <n>] [--limit <n>] [--raw-dump <file>]\n"
			"                                                                         FM Get LD Allocations (0x5401)\n"
			"  fm-set-ld-alloc    [--port <vdm0|vdm1|i3c>] --input <hexfile> [--number-ld <n>] [--start-ld-id <n>]\n"
			"                                                                         FM Set LD Allocations (0x5402)\n"
			"  fm-get-qos-ctrl    [--port <vdm0|vdm1|i3c>]                        FM Get QoS Control (0x5403)\n"
			"  fm-set-qos-ctrl    [--port <vdm0|vdm1|i3c>] [--egress-congestion-control-enable <0|1>] [--egress-tpr-enable <0|1>] ...\n"
			"                     [--input <hexfile>]                               FM Set QoS Control (0x5404)\n"
			"  fm-get-qos-status  [--port <vdm0|vdm1|i3c>]                        FM Get QoS Status (0x5405)\n"
			"  fm-get-qos-alloc-bw [--port <vdm0|vdm1|i3c>] [--number-ld <n>] [--start-ld-id <n>] [--raw-dump <file>]\n"
			"                                                                         FM Get QoS Allocated BW (0x5406)\n"
			"  fm-set-qos-alloc-bw [--port <vdm0|vdm1|i3c>] --input <hexfile> [--number-ld <n>] [--start-ld-id <n>]\n"
			"                                                                         FM Set QoS Allocated BW (0x5407)\n"
			"  fm-get-qos-bw-limit [--port <vdm0|vdm1|i3c>] [--number-ld <n>] [--start-ld-id <n>] [--raw-dump <file>]\n"
			"                                                                         FM Get QoS BW Limit (0x5408)\n"
			"  fm-set-qos-bw-limit [--port <vdm0|vdm1|i3c>] --input <hexfile> [--number-ld <n>] [--start-ld-id <n>]\n"
			"                                                                         FM Set QoS BW Limit (0x5409)\n"
			"  get-supported-logs [--port <vdm0|vdm1|i3c>]                        Get Supported Logs (0x0400)\n"
			"  get-supported-feat [--port <vdm0|vdm1|i3c>] [--count <bytes>] [--start-index <n>]\n"
			"                                                                         Get Supported Features (0x0500)\n"
			"  get-feature        [--port <vdm0|vdm1|i3c>] --feature-id <uuid> [--offset <n>] [--count <n>] [--selection <n>] [--dump <file>]\n"
			"                                                                         Get Feature (0x0501)\n"
			"  set-feature        [--port <vdm0|vdm1|i3c>] --feature-id <uuid> --input <hexfile> [--offset <n>] [--flags <n>] [--version <n>]\n"
			"                                                                         Set Feature (0x0502)\n"
			"  get-log            [--port <vdm0|vdm1|i3c>] --uuid <uuid> [--offset <n>] [--length <n>] [--text]\n"
			"                                                                         Get Log (0x0401)\n"
			"  get-log-cap        [--port <vdm0|vdm1|i3c>] --uuid <uuid>                       Get Log Capabilities (0x0402)\n"
			"  clear-log          [--port <vdm0|vdm1|i3c>] --uuid <uuid>                       Clear Log (0x0403)\n"
			"  populate-log       [--port <vdm0|vdm1|i3c>] --uuid <uuid>                       Populate Log (0x0404)\n"
			"  bg-op-status       [--port <vdm0|vdm1|i3c>]                        Background Operation Status (0x0002)\n"
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
	if (strcmp(argv[1], "vu-dlcfg") == 0)
		return sdb_tunnel_vu_dlcfg(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "vu-getdevcfg") == 0)
		return sdb_tunnel_vu_getdevcfg(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "vu-ddrfreq") == 0)
		return sdb_tunnel_vu_ddrfreq(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "vu-pciespeed") == 0)
		return sdb_tunnel_vu_pciespeed(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "activate-fw") == 0)
		return sdb_tunnel_activate_fw(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-health-info") == 0)
		return sdb_tunnel_get_health_info(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-alert-config") == 0)
		return sdb_tunnel_get_alert_config(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "set-alert-config") == 0)
		return sdb_tunnel_set_alert_config(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-poison-list") == 0)
		return sdb_tunnel_get_poison_list(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "inject-poison") == 0)
		return sdb_tunnel_inject_poison(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "clear-poison") == 0)
		return sdb_tunnel_clear_poison(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-scan-media-cap") == 0)
		return sdb_tunnel_get_scan_media_cap(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "scan-media") == 0)
		return sdb_tunnel_scan_media(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-scan-media-results") == 0)
		return sdb_tunnel_get_scan_media_results(ep, argc - 2, argv + 2);
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
	if (strcmp(argv[1], "fm-set-ld-alloc") == 0)
		return sdb_tunnel_fm_set_ld_alloc(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "fm-get-qos-ctrl") == 0)
		return sdb_tunnel_fm_get_qos_ctrl(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "fm-set-qos-ctrl") == 0)
		return sdb_tunnel_fm_set_qos_ctrl(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "fm-get-qos-status") == 0)
		return sdb_tunnel_fm_get_qos_status(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "fm-get-qos-alloc-bw") == 0)
		return sdb_tunnel_fm_get_qos_alloc_bw(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "fm-set-qos-alloc-bw") == 0)
		return sdb_tunnel_fm_set_qos_alloc_bw(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "fm-get-qos-bw-limit") == 0)
		return sdb_tunnel_fm_get_qos_bw_limit(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "fm-set-qos-bw-limit") == 0)
		return sdb_tunnel_fm_set_qos_bw_limit(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-supported-logs") == 0)
		return sdb_tunnel_get_supported_logs(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-supported-feat") == 0)
		return sdb_tunnel_get_supported_feat(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-feature") == 0)
		return sdb_tunnel_get_feature(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "set-feature") == 0)
		return sdb_tunnel_set_feature(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-log") == 0)
		return sdb_tunnel_get_log(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "get-log-cap") == 0)
		return sdb_tunnel_get_log_cap(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "clear-log") == 0)
		return sdb_tunnel_clear_log(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "populate-log") == 0)
		return sdb_tunnel_populate_log(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "bg-op-status") == 0)
		return sdb_tunnel_bg_op_status(ep, argc - 2, argv + 2);
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
		"  supported: identify, identify_memdev, get-partition, set-partition, get-fw-info, transfer-fw, activate-fw, get-health-info, get-alert-config, set-alert-config, get-poison-list, inject-poison, clear-poison, get-scan-media-cap, scan-media, get-scan-media-results, get-sld-qos-ctrl, set-sld-qos-ctrl, get-sld-qos-status, fm-get-ld-info, fm-get-ld-alloc, fm-set-ld-alloc, fm-get-qos-ctrl, fm-set-qos-ctrl, fm-get-qos-status, fm-get-qos-alloc-bw, fm-set-qos-alloc-bw, fm-get-qos-bw-limit, fm-set-qos-bw-limit, get-supported-logs, get-supported-feat, get-feature, set-feature, get-log, get-log-cap, clear-log, populate-log, bg-op-status, bg-op-abort, get-resp-msg-limit, set-resp-msg-limit,"
		" get-event-records, clear-event-records,"
		" get-mctp-evt-int-policy, set-mctp-evt-int-policy,"
		" get-timestamp, set-timestamp\n");
	return -1;
}
