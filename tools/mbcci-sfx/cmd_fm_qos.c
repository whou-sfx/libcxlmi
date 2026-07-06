// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: FM API QoS commands (MLD Components / 54h, 5403h–5409h).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include <ccan/endian/endian.h>

#include "mbcci-sfx.h"

#define FM_GET_QOS_LD_DEFAULT_COUNT 16

#define FM_QOS_LD_HDR_SZ MBCCI_FM_QOS_LD_HDR_SZ

#define FM_GET_QOS_LD_USAGE \
	"fm-get-qos-alloc-bw|fm-get-qos-bw-limit [--number-ld <n>] [--start-ld-id <n>] [--raw-dump <file>]"

#define FM_SET_QOS_LD_USAGE \
	"fm-set-qos-alloc-bw|fm-set-qos-bw-limit --input <hexfile> [--number-ld <n>] [--start-ld-id <n>]"

#define FM_SET_QOS_CTRL_USAGE \
	"fm-set-qos-ctrl [--egress-congestion-control-enable <0|1>] [--egress-tpr-enable <0|1>] " \
	"[--egress-moderate <pct>] [--egress-severe <pct>] [--backpressure-interval <n>] " \
	"[--recmpbasis <n>] [--completion-collection-interval <n>] [--input <hexfile>]"

#define QOS_EGRESS_CC_ENABLE (1U << 0)
#define QOS_EGRESS_TPR_ENABLE (1U << 1)

static int parse_u8_val(const char *arg, uint8_t *out, const char *name,
			unsigned long max)
{
	unsigned long val = strtoul(arg, NULL, 0);

	if (val > max) {
		fprintf(stderr, "%s: value out of range (0-%lu)\n", name, max);
		return -1;
	}
	*out = (uint8_t)val;
	return 0;
}

static int parse_bool_flag(const char *arg, uint8_t bit,
			   uint8_t *control, const char *name)
{
	unsigned long val = strtoul(arg, NULL, 0);

	if (val > 1) {
		fprintf(stderr, "%s: expected 0 or 1\n", name);
		return -1;
	}
	if (val)
		*control |= bit;
	return 0;
}

size_t fm_qos_ld_payload_size(uint8_t number_ld)
{
	return FM_QOS_LD_HDR_SZ + number_ld;
}

void print_fm_qos_control(const struct cxlmi_cmd_fmapi_get_qos_control_rsp *rsp)
{
	print_qos_telemetry_control(rsp->qos_telemetry_control);
	printf("Egress Moderate Percentage:       %u%%\n",
	       rsp->egress_moderate_percentage);
	printf("Egress Severe Percentage:         %u%%\n",
	       rsp->egress_severe_percentage);
	printf("Backpressure Sample Interval:   %u\n",
	       rsp->backpressure_sample_interval);
	printf("Recmpbasis:                       %u\n", rsp->recmpbasis);
	printf("Completion Collection Interval:   %u\n",
	       rsp->completion_collection_interval);
}

void print_fm_qos_status(const struct cxlmi_cmd_fmapi_get_qos_status_rsp *rsp)
{
	printf("Backpressure Avg Percentage: %u%%\n",
	       rsp->backpressure_avg_percentage);
}

void print_fm_qos_allocated_bw(
	const struct cxlmi_cmd_fmapi_get_qos_allocated_bw_rsp *rsp)
{
	unsigned int i;

	printf("Number LD:   %u\n", rsp->number_ld);
	printf("Start LD ID: %u\n", rsp->start_ld_id);
	for (i = 0; i < rsp->number_ld; i++) {
		printf("  LD[%u] qos_allocation_fraction: %u\n",
		       rsp->start_ld_id + i, rsp->qos_allocation_fraction[i]);
	}
}

void print_fm_qos_bw_limit(const struct cxlmi_cmd_fmapi_get_qos_bw_limit_rsp *rsp)
{
	unsigned int i;

	printf("Number LD:   %u\n", rsp->number_ld);
	printf("Start LD ID: %u\n", rsp->start_ld_id);
	for (i = 0; i < rsp->number_ld; i++) {
		printf("  LD[%u] qos_limit_fraction: %u\n",
		       rsp->start_ld_id + i, rsp->qos_limit_fraction[i]);
	}
}

int parse_fm_get_qos_ld_req(int argc, char **argv,
			    struct fm_get_qos_ld_params *params)
{
	unsigned long val;
	int i;

	memset(params, 0, sizeof(*params));
	params->req.number_ld = FM_GET_QOS_LD_DEFAULT_COUNT;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--number-ld") == 0 && i + 1 < argc) {
			val = strtoul(argv[++i], NULL, 0);
			if (val == 0 || val > UINT8_MAX) {
				fprintf(stderr, "--number-ld: out of range (1-255)\n");
				return -1;
			}
			params->req.number_ld = (uint8_t)val;
		} else if (strcmp(argv[i], "--start-ld-id") == 0 && i + 1 < argc) {
			val = strtoul(argv[++i], NULL, 0);
			if (val > UINT8_MAX) {
				fprintf(stderr, "--start-ld-id: out of range (0-255)\n");
				return -1;
			}
			params->req.start_ld_id = (uint8_t)val;
		} else if (strcmp(argv[i], "--raw-dump") == 0 && i + 1 < argc) {
			params->raw_dump_file = argv[++i];
		} else {
			fprintf(stderr, "Usage: %s\n", FM_GET_QOS_LD_USAGE);
			return -1;
		}
	}

	return 0;
}

int parse_fm_set_qos_ld_req(int argc, char **argv,
			    struct fm_set_qos_ld_params *params)
{
	unsigned long val;
	int i;

	memset(params, 0, sizeof(*params));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
			params->input_file = argv[++i];
		} else if (strcmp(argv[i], "--number-ld") == 0 && i + 1 < argc) {
			val = strtoul(argv[++i], NULL, 0);
			if (val == 0 || val > UINT8_MAX) {
				fprintf(stderr, "--number-ld: out of range (1-255)\n");
				return -1;
			}
			params->number_ld = (uint8_t)val;
			params->has_number_ld = 1;
		} else if (strcmp(argv[i], "--start-ld-id") == 0 && i + 1 < argc) {
			val = strtoul(argv[++i], NULL, 0);
			if (val > UINT8_MAX) {
				fprintf(stderr, "--start-ld-id: out of range (0-255)\n");
				return -1;
			}
			params->start_ld_id = (uint8_t)val;
			params->has_start_ld_id = 1;
		} else {
			fprintf(stderr, "Usage: %s\n", FM_SET_QOS_LD_USAGE);
			return -1;
		}
	}

	if (!params->input_file) {
		fprintf(stderr, "Usage: %s\n", FM_SET_QOS_LD_USAGE);
		return -1;
	}

	return 0;
}

int fm_qos_ld_apply_set_overrides(uint8_t *payload, size_t payload_len,
				  const struct fm_set_qos_ld_params *params,
				  uint8_t *number_ld_out)
{
	uint8_t number_ld;

	if (payload_len < FM_QOS_LD_HDR_SZ ||
	    payload_len - FM_QOS_LD_HDR_SZ > UINT8_MAX)
		return -1;

	number_ld = params->has_number_ld ? params->number_ld : payload[0];
	if (fm_qos_ld_payload_size(number_ld) != payload_len)
		return -1;

	if (params->has_number_ld)
		payload[0] = params->number_ld;
	if (params->has_start_ld_id)
		payload[1] = params->start_ld_id;

	*number_ld_out = payload[0];
	return 0;
}

int parse_fm_set_qos_ctrl_req(int argc, char **argv,
			      struct fm_set_qos_ctrl_params *params)
{
	unsigned long val;
	int i;

	memset(params, 0, sizeof(*params));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
			params->input_file = argv[++i];
		} else if (strcmp(argv[i], "--egress-congestion-control-enable") == 0 &&
			   i + 1 < argc) {
			if (parse_bool_flag(argv[++i], QOS_EGRESS_CC_ENABLE,
					    &params->req.qos_telemetry_control,
					    "--egress-congestion-control-enable") < 0)
				return -1;
		} else if (strcmp(argv[i], "--egress-tpr-enable") == 0 &&
			   i + 1 < argc) {
			if (parse_bool_flag(argv[++i], QOS_EGRESS_TPR_ENABLE,
					    &params->req.qos_telemetry_control,
					    "--egress-tpr-enable") < 0)
				return -1;
		} else if (strcmp(argv[i], "--egress-moderate") == 0 &&
			   i + 1 < argc) {
			if (parse_u8_val(argv[++i], &params->req.egress_moderate_percentage,
					 "--egress-moderate", 255) < 0)
				return -1;
		} else if (strcmp(argv[i], "--egress-severe") == 0 &&
			   i + 1 < argc) {
			if (parse_u8_val(argv[++i], &params->req.egress_severe_percentage,
					 "--egress-severe", 255) < 0)
				return -1;
		} else if (strcmp(argv[i], "--backpressure-interval") == 0 &&
			   i + 1 < argc) {
			if (parse_u8_val(argv[++i],
					 &params->req.backpressure_sample_interval,
					 "--backpressure-interval", 255) < 0)
				return -1;
		} else if (strcmp(argv[i], "--recmpbasis") == 0 && i + 1 < argc) {
			val = strtoul(argv[++i], NULL, 0);
			if (val > UINT16_MAX) {
				fprintf(stderr, "--recmpbasis: out of range (0-65535)\n");
				return -1;
			}
			params->req.recmpbasis = (uint16_t)val;
		} else if (strcmp(argv[i], "--completion-collection-interval") == 0 &&
			   i + 1 < argc) {
			if (parse_u8_val(argv[++i],
					 &params->req.completion_collection_interval,
					 "--completion-collection-interval", 255) < 0)
				return -1;
		} else {
			fprintf(stderr, "Usage: %s\n", FM_SET_QOS_CTRL_USAGE);
			return -1;
		}
	}

	return 0;
}

void fm_qos_ctrl_host_to_wire(const struct cxlmi_cmd_fmapi_set_qos_control_req *host,
			      struct cxlmi_cmd_fmapi_set_qos_control_req *wire)
{
	wire->qos_telemetry_control = host->qos_telemetry_control;
	wire->egress_moderate_percentage = host->egress_moderate_percentage;
	wire->egress_severe_percentage = host->egress_severe_percentage;
	wire->backpressure_sample_interval = host->backpressure_sample_interval;
	wire->recmpbasis = cpu_to_le16(host->recmpbasis);
	wire->completion_collection_interval = host->completion_collection_interval;
}

void fm_qos_ctrl_wire_to_host(const struct cxlmi_cmd_fmapi_get_qos_control_rsp *wire,
			      struct cxlmi_cmd_fmapi_get_qos_control_rsp *host)
{
	host->qos_telemetry_control = wire->qos_telemetry_control;
	host->egress_moderate_percentage = wire->egress_moderate_percentage;
	host->egress_severe_percentage = wire->egress_severe_percentage;
	host->backpressure_sample_interval = wire->backpressure_sample_interval;
	host->recmpbasis = le16_to_cpu(wire->recmpbasis);
	host->completion_collection_interval = wire->completion_collection_interval;
}

int fm_qos_ctrl_parse_input_payload(const uint8_t *payload, size_t payload_len,
				    struct cxlmi_cmd_fmapi_set_qos_control_req *req)
{
	if (payload_len != sizeof(*req))
		return -1;

	memset(req, 0, sizeof(*req));
	req->qos_telemetry_control = payload[0];
	req->egress_moderate_percentage = payload[1];
	req->egress_severe_percentage = payload[2];
	req->backpressure_sample_interval = payload[3];
	memcpy(&req->recmpbasis, payload + 4, sizeof(uint16_t));
	req->recmpbasis = le16_to_cpu(req->recmpbasis);
	req->completion_collection_interval = payload[6];
	return 0;
}
