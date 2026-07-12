// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: VU CFGFREQ / CFGPCIE commands (OPCODE_VU=0xCC53).
 *
 * Sets DDR frequency (CFGFREQ=0x09) and PCIe link speed/width (CFGPCIE=0x0a)
 * via the vendor-specific mailbox CCI path.
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
#include <strings.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define VU_DDRFREQ_USAGE \
	"vu-ddrfreq --freq <n>"

#define VU_PCIESPEED_USAGE \
	"vu-pciespeed --pcie-port <0|1> --speed <gen1|gen2|gen3|gen4|gen5|gen6> --width <x1|x2|x4|x8>"

static const char *pcie_speed_name(uint32_t speed)
{
	switch (speed) {
	case 1: return "gen1";
	case 2: return "gen2";
	case 3: return "gen3";
	case 4: return "gen4";
	case 5: return "gen5";
	case 6: return "gen6";
	default: return "?";
	}
}

static int parse_pcie_speed(const char *name, uint32_t *speed_out)
{
	static const struct {
		const char *name;
		uint32_t    value;
	} map[] = {
		{ "gen1", 1 }, { "gen2", 2 }, { "gen3", 3 },
		{ "gen4", 4 }, { "gen5", 5 }, { "gen6", 6 },
	};
	size_t i;

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (strcasecmp(name, map[i].name) == 0) {
			*speed_out = map[i].value;
			return 0;
		}
	}

	fprintf(stderr,
		"vu-pciespeed: unknown --speed '%s' (valid: gen1..gen6)\n", name);
	return -1;
}

static int parse_pcie_width(const char *name, uint32_t *width_out)
{
	static const struct {
		const char *name;
		uint32_t    value;
	} map[] = {
		{ "x1", 1 }, { "x2", 2 }, { "x4", 4 }, { "x8", 8 },
	};
	size_t i;

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (strcasecmp(name, map[i].name) == 0) {
			*width_out = map[i].value;
			return 0;
		}
	}

	fprintf(stderr,
		"vu-pciespeed: unknown --width '%s' (valid: x1, x2, x4, x8)\n",
		name);
	return -1;
}

static int vu_send_simple(struct cxlmi_endpoint *ep, const void *req,
			  size_t req_sz, const char *cmd_name)
{
	int rc = cxlmi_cmd_vendor_specific(ep, NULL, OPCODE_VU,
					   (void *)req, req_sz, NULL, 0);

	if (rc) {
		if (rc > 0)
			fprintf(stderr, "%s failed: %s\n", cmd_name,
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "%s ioctl failed\n", cmd_name);
	}
	return rc;
}

size_t vu_ddrfreq_pack(const struct vu_ddrfreq_params *params,
		       void *buf, size_t buf_sz)
{
	cfgfreq req = {
		.vuCmdId = CFGFREQ,
		.status  = 0,
		.in_sz   = (u32)sizeof(req),
		.out_sz  = 0,
		.freqmts = params->freqmts,
	};

	if (buf_sz < sizeof(req))
		return 0;
	memcpy(buf, &req, sizeof(req));
	return sizeof(req);
}

size_t vu_pciespeed_pack(const struct vu_pciespeed_params *params,
			 void *buf, size_t buf_sz)
{
	cfgpcie req = {
		.vuCmdId = CFGPCIE,
		.status  = 0,
		.in_sz   = (u32)sizeof(req),
		.out_sz  = 0,
		.portid  = params->portid,
		.speed   = params->speed,
		.width   = params->width,
	};

	if (buf_sz < sizeof(req))
		return 0;
	memcpy(buf, &req, sizeof(req));
	return sizeof(req);
}

int parse_vu_ddrfreq_req(int argc, char **argv,
			 struct vu_ddrfreq_params *params)
{
	int has_freq = 0;
	int i;

	memset(params, 0, sizeof(*params));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--freq") == 0 && i + 1 < argc) {
			unsigned long freq = strtoul(argv[++i], NULL, 0);

			if (freq == 0 || freq > UINT32_MAX) {
				fprintf(stderr,
					"vu-ddrfreq: invalid --freq '%s'\n",
					argv[i]);
				return -1;
			}
			params->freqmts = (uint32_t)freq;
			has_freq = 1;
		} else {
			fprintf(stderr, "Usage: %s\n", VU_DDRFREQ_USAGE);
			return -1;
		}
	}

	if (!has_freq) {
		fprintf(stderr, "Usage: %s\n", VU_DDRFREQ_USAGE);
		fprintf(stderr,
			"  --freq  DDR frequency in MT/s (cfgfreq.freqmts)\n");
		return -1;
	}

	return 0;
}

int parse_vu_pciespeed_req(int argc, char **argv,
			   struct vu_pciespeed_params *params)
{
	int has_pcie_port = 0, has_speed = 0, has_width = 0;
	int i;

	memset(params, 0, sizeof(*params));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--pcie-port") == 0 && i + 1 < argc) {
			unsigned long port = strtoul(argv[++i], NULL, 0);

			if (port > 1) {
				fprintf(stderr,
					"vu-pciespeed: --pcie-port must be 0 or 1\n");
				return -1;
			}
			params->portid = (uint32_t)port;
			has_pcie_port = 1;
		} else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
			if (parse_pcie_speed(argv[++i], &params->speed) != 0)
				return -1;
			has_speed = 1;
		} else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
			if (parse_pcie_width(argv[++i], &params->width) != 0)
				return -1;
			has_width = 1;
		} else {
			fprintf(stderr, "Usage: %s\n", VU_PCIESPEED_USAGE);
			return -1;
		}
	}

	if (!has_pcie_port || !has_speed || !has_width) {
		fprintf(stderr, "Usage: %s\n", VU_PCIESPEED_USAGE);
		fprintf(stderr,
			"  --pcie-port  PCIe port 0 or 1\n"
			"  --speed      gen1|gen2|gen3|gen4|gen5|gen6\n"
			"  --width      x1|x2|x4|x8\n");
		return -1;
	}

	return 0;
}

int vu_ddrfreq_send(struct cxlmi_endpoint *ep,
		    const struct vu_ddrfreq_params *params)
{
	uint8_t req[VU_CFGFREQ_REQ_BYTES];
	size_t req_sz;

	req_sz = vu_ddrfreq_pack(params, req, sizeof(req));
	if (req_sz == 0)
		return -1;
	return vu_send_simple(ep, req, req_sz, "vu-ddrfreq");
}

int vu_pciespeed_send(struct cxlmi_endpoint *ep,
		      const struct vu_pciespeed_params *params)
{
	uint8_t req[VU_CFGPCIE_REQ_BYTES];
	size_t req_sz;

	req_sz = vu_pciespeed_pack(params, req, sizeof(req));
	if (req_sz == 0)
		return -1;
	return vu_send_simple(ep, req, req_sz, "vu-pciespeed");
}

int cmd_vu_ddrfreq(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct vu_ddrfreq_params params;
	int rc, lock_rc;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s\n", VU_DDRFREQ_USAGE);
		return -1;
	}

	rc = parse_vu_ddrfreq_req(argc - 1, argv + 1, &params);
	if (rc)
		return rc;

	rc = vu_mb_unlock(ep);
	if (rc)
		return rc;

	rc = vu_ddrfreq_send(ep, &params);

	lock_rc = vu_mb_lock(ep);
	if (rc == 0)
		rc = lock_rc;

	if (rc == 0)
		printf("vu-ddrfreq OK: freq=%u MT/s\n", params.freqmts);
	return rc;
}

int cmd_vu_pciespeed(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct vu_pciespeed_params params;
	int rc, lock_rc;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s\n", VU_PCIESPEED_USAGE);
		return -1;
	}

	rc = parse_vu_pciespeed_req(argc - 1, argv + 1, &params);
	if (rc)
		return rc;

	rc = vu_mb_unlock(ep);
	if (rc)
		return rc;

	rc = vu_pciespeed_send(ep, &params);

	lock_rc = vu_mb_lock(ep);
	if (rc == 0)
		rc = lock_rc;

	if (rc == 0) {
		printf("vu-pciespeed OK: pcie-port=%u speed=%s width=x%u\n",
		       params.portid, pcie_speed_name(params.speed),
		       params.width);
	}
	return rc;
}
