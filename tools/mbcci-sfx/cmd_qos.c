// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: SLD QoS Telemetry (Opcodes 4700h–4702h).
 *
 * Sends SLD QoS commands directly via the in-band ioctl path provided by
 * libcxlmi and pretty-prints the response payload.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define QOS_EGRESS_CC_ENABLE (1U << 0)
#define QOS_EGRESS_TPR_ENABLE (1U << 1)

#define SET_SLD_QOS_CTRL_USAGE \
	"set-sld-qos-ctrl [--egress-congestion-control-enable <0|1>] [--egress-tpr-enable <0|1>] " \
	"[--egress-moderate <pct>] [--egress-severe <pct>] [--backpressure-interval <n>]"

void print_qos_telemetry_control(uint8_t val)
{
	printf("QoS Telemetry Control: 0x%02x\n", val);
	if (val & QOS_EGRESS_CC_ENABLE)
		printf("  [0] Egress Congestion Control Enable\n");
	if (val & QOS_EGRESS_TPR_ENABLE)
		printf("  [1] Egress Port Congestion Temporary Throughput Reduction Enable\n");
	if (val & 0xfc)
		printf("  [7:2] Reserved (0x%x)\n", (val >> 2) & 0x3f);
}

void print_sld_qos_control(const struct cxlmi_cmd_memdev_get_sld_qos_control_rsp *rsp)
{
	print_qos_telemetry_control(rsp->qos_telemetry_control);
	printf("Egress Moderate Percentage:     %u%%\n",
	       rsp->egress_moderate_percentage);
	printf("Egress Severe Percentage:       %u%%\n",
	       rsp->egress_severe_percentage);
	printf("Backpressure Sample Interval:   %u\n",
	       rsp->backpressure_sample_interval);
}

int cmd_get_sld_qos_ctrl(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_get_sld_qos_control_rsp rsp;
	int rc;

	(void)argc;
	(void)argv;

	memset(&rsp, 0, sizeof(rsp));
	rc = cxlmi_cmd_memdev_get_sld_qos_control(ep, NULL, &rsp);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get SLD QoS control failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get SLD QoS control ioctl failed\n");
		return rc;
	}

	print_sld_qos_control(&rsp);
	return 0;
}

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

void print_set_sld_qos_ctrl_result(
	const struct cxlmi_cmd_memdev_set_sld_qos_control_req *req)
{
	printf("Set SLD QoS control OK\n");
	print_qos_telemetry_control(req->qos_telemetry_control);
	if (req->egress_moderate_percentage)
		printf("  Egress Moderate Percentage:   %u%%\n",
		       req->egress_moderate_percentage);
	if (req->egress_severe_percentage)
		printf("  Egress Severe Percentage:     %u%%\n",
		       req->egress_severe_percentage);
	if (req->backpressure_sample_interval)
		printf("  Backpressure Sample Interval: %u\n",
		       req->backpressure_sample_interval);
}

int parse_set_sld_qos_ctrl_req(int argc, char **argv,
			       struct cxlmi_cmd_memdev_set_sld_qos_control_req *req)
{
	int i;

	memset(req, 0, sizeof(*req));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--egress-congestion-control-enable") == 0 &&
			   i + 1 < argc) {
			if (parse_bool_flag(argv[++i], QOS_EGRESS_CC_ENABLE,
					    &req->qos_telemetry_control,
					    "--egress-congestion-control-enable") < 0)
				return -1;
		} else if (strcmp(argv[i], "--egress-tpr-enable") == 0 &&
			   i + 1 < argc) {
			if (parse_bool_flag(argv[++i], QOS_EGRESS_TPR_ENABLE,
					    &req->qos_telemetry_control,
					    "--egress-tpr-enable") < 0)
				return -1;
		} else if (strcmp(argv[i], "--egress-moderate") == 0 &&
			   i + 1 < argc) {
			if (parse_u8_val(argv[++i], &req->egress_moderate_percentage,
					 "--egress-moderate", 255) < 0)
				return -1;
		} else if (strcmp(argv[i], "--egress-severe") == 0 &&
			   i + 1 < argc) {
			if (parse_u8_val(argv[++i], &req->egress_severe_percentage,
					 "--egress-severe", 255) < 0)
				return -1;
		} else if (strcmp(argv[i], "--backpressure-interval") == 0 &&
			   i + 1 < argc) {
			if (parse_u8_val(argv[++i], &req->backpressure_sample_interval,
					 "--backpressure-interval", 255) < 0)
				return -1;
		} else {
			fprintf(stderr, "Usage: %s\n", SET_SLD_QOS_CTRL_USAGE);
			return -1;
		}
	}

	return 0;
}

int cmd_set_sld_qos_ctrl(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_set_sld_qos_control_req req;
	int rc;

	rc = parse_set_sld_qos_ctrl_req(argc - 1, argv + 1, &req);
	if (rc)
		return rc;

	rc = cxlmi_cmd_memdev_set_sld_qos_control(ep, NULL, &req);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "set SLD QoS control failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "set SLD QoS control ioctl failed\n");
		return rc;
	}

	print_set_sld_qos_ctrl_result(&req);
	return 0;
}

void print_sld_qos_status(const struct cxlmi_cmd_memdev_get_sld_qos_status_rsp *rsp)
{
	printf("Backpressure Avg Percentage: %u%%\n",
	       rsp->backpressure_avg_percentage);
}

int cmd_get_sld_qos_status(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_get_sld_qos_status_rsp rsp;
	int rc;

	(void)argc;
	(void)argv;

	memset(&rsp, 0, sizeof(rsp));
	rc = cxlmi_cmd_memdev_get_sld_qos_status(ep, NULL, &rsp);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get SLD QoS status failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get SLD QoS status ioctl failed\n");
		return rc;
	}

	print_sld_qos_status(&rsp);
	return 0;
}
