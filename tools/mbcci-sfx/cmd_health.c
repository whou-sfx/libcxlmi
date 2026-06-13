// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Health Info and Alert Configuration (Opcodes 4200h–4202h).
 *
 * Sends memory-device health/alert commands directly via the in-band ioctl
 * path provided by libcxlmi and pretty-prints the response payload.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define ALERT_BIT_LUPWT   (1U << 0)
#define ALERT_BIT_DOTPWT  (1U << 1)
#define ALERT_BIT_DUTPWT  (1U << 2)
#define ALERT_BIT_CVMEPWT (1U << 3)
#define ALERT_BIT_CPMEPWT (1U << 4)

#define SET_ALERT_CONFIG_USAGE \
	"set-alert-config [--life-used-warning <pct>] [--over-temp-warning <n>] " \
	"[--under-temp-warning <n>] [--volatile-mem-error-warning <n>] " \
	"[--persistent-mem-error-warning <n>]"

static const char *alert_threshold_names[] = {
	"Life Used Programmable Warning Threshold",
	"Device Over-Temperature Programmable Warning Threshold",
	"Device Under-Temperature Programmable Warning Threshold",
	"Corrected Volatile Memory Error Programmable Warning Threshold",
	"Corrected Persistent Memory Error Programmable Warning Threshold",
};

static void print_alert_bitfield(const char *title, uint8_t value)
{
	unsigned int bit;

	printf("%s: 0x%02x\n", title, value);
	for (bit = 0; bit < 5; bit++) {
		if (value & (1U << bit))
			printf("  [%u] %s\n", bit, alert_threshold_names[bit]);
	}
	if (value & 0xe0)
		printf("  [7:5] Reserved (0x%x)\n", (value >> 5) & 0x7);
}

void print_memdev_health_info(
	const struct cxlmi_cmd_memdev_get_health_info_rsp *hi)
{
	printf("Health Status:                 0x%02x\n", hi->health_status);
	printf("Media Status:                  0x%02x\n", hi->media_status);
	printf("Additional Status:             0x%02x\n", hi->additional_status);
	printf("Life Used:                       %u%%\n", hi->life_used);
	printf("Device Temperature:              %d C\n",
	       (int16_t)hi->device_temperature);
	printf("Dirty Shutdown Count:            %u\n", hi->dirty_shutdown_count);
	printf("Corrected Volatile Error Count:  %u\n",
	       hi->corrected_volatile_error_count);
	printf("Corrected Persistent Error Count:%u\n",
	       hi->corrected_persistent_error_count);
}

int cmd_get_health_info(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_get_health_info_rsp hi;
	int rc;

	(void)argc;
	(void)argv;

	memset(&hi, 0, sizeof(hi));
	rc = cxlmi_cmd_memdev_get_health_info(ep, NULL, &hi);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get health info failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get health info ioctl failed\n");
		return rc;
	}

	print_memdev_health_info(&hi);
	return 0;
}

void print_memdev_alert_config(
	const struct cxlmi_cmd_memdev_get_alert_config_rsp *ac)
{
	print_alert_bitfield("Valid Alerts", ac->valid_alerts);
	print_alert_bitfield("Programmable Alerts", ac->programmable_alerts);
	printf("Life Used Critical Alert Threshold:              %u%%\n",
	       ac->life_used_critical_alert_threshold);
	printf("Life Used Programmable Warning Threshold:        %u%%\n",
	       ac->life_used_programmable_warning_threshold);
	printf("Device Over-Temperature Critical Alert Threshold:%u\n",
	       ac->device_over_temperature_critical_alert_threshold);
	printf("Device Under-Temperature Critical Alert Threshold:%u\n",
	       ac->device_under_temperature_critical_alert_threshold);
	printf("Device Over-Temperature Warning Threshold:         %u\n",
	       ac->device_over_temperature_programmable_warning_threshold);
	printf("Device Under-Temperature Warning Threshold:        %u\n",
	       ac->device_under_temperature_programmable_warning_threshold);
	printf("Corrected Volatile Mem Error Warning Threshold:    %u\n",
	       ac->corrected_volatile_mem_error_programmable_warning_threshold);
	printf("Corrected Persistent Mem Error Warning Threshold:  %u\n",
	       ac->corrected_persistent_mem_error_programmable_warning_threshold);
}

int cmd_get_alert_config(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_get_alert_config_rsp ac;
	int rc;

	(void)argc;
	(void)argv;

	memset(&ac, 0, sizeof(ac));
	rc = cxlmi_cmd_memdev_get_alert_config(ep, NULL, &ac);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get alert config failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get alert config ioctl failed\n");
		return rc;
	}

	print_memdev_alert_config(&ac);
	return 0;
}

static int parse_u8_threshold(const char *arg, uint8_t *out, const char *name)
{
	unsigned long val = strtoul(arg, NULL, 0);

	if (val > UINT8_MAX) {
		fprintf(stderr, "%s: value out of range (0-255)\n", name);
		return -1;
	}
	*out = (uint8_t)val;
	return 0;
}

static int parse_u16_threshold(const char *arg, uint16_t *out, const char *name)
{
	unsigned long val = strtoul(arg, NULL, 0);

	if (val > UINT16_MAX) {
		fprintf(stderr, "%s: value out of range (0-65535)\n", name);
		return -1;
	}
	*out = (uint16_t)val;
	return 0;
}

static void set_alert_action_bits(struct cxlmi_cmd_memdev_set_alert_config_req *req,
				  uint8_t bit)
{
	req->valid_alert_actions |= bit;
	req->enable_alert_actions |= bit;
}

void print_set_alert_config_result(
	const struct cxlmi_cmd_memdev_set_alert_config_req *req)
{
	printf("Set alert config OK\n");
	print_alert_bitfield("Valid Alert Actions", req->valid_alert_actions);
	print_alert_bitfield("Enable Alert Actions", req->enable_alert_actions);
	if (req->valid_alert_actions & ALERT_BIT_LUPWT)
		printf("  Life Used Warning Threshold:        %u%%\n",
		       req->life_used_programmable_warning_threshold);
	if (req->valid_alert_actions & ALERT_BIT_DOTPWT)
		printf("  Over-Temperature Warning Threshold: %u\n",
		       req->device_over_temperature_programmable_warning_threshold);
	if (req->valid_alert_actions & ALERT_BIT_DUTPWT)
		printf("  Under-Temperature Warning Threshold:%u\n",
		       req->device_under_temperature_programmable_warning_threshold);
	if (req->valid_alert_actions & ALERT_BIT_CVMEPWT)
		printf("  Volatile Mem Error Warning Threshold: %u\n",
		       req->corrected_volatile_mem_error_programmable_warning_threshold);
	if (req->valid_alert_actions & ALERT_BIT_CPMEPWT)
		printf("  Persistent Mem Error Warning Threshold:%u\n",
		       req->corrected_persistent_mem_error_programmable_warning_threshold);
}

int parse_set_alert_config_req(int argc, char **argv,
			       struct cxlmi_cmd_memdev_set_alert_config_req *req)
{
	uint16_t over_temp = 0;
	uint16_t under_temp = 0;
	uint16_t volatile_err = 0;
	uint16_t persistent_err = 0;
	int i;

	memset(req, 0, sizeof(*req));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--life-used-warning") == 0 && i + 1 < argc) {
			if (parse_u8_threshold(argv[++i],
					       &req->life_used_programmable_warning_threshold,
					       "--life-used-warning") < 0)
				return -1;
			set_alert_action_bits(req, ALERT_BIT_LUPWT);
		} else if (strcmp(argv[i], "--over-temp-warning") == 0 &&
			   i + 1 < argc) {
			if (parse_u16_threshold(argv[++i], &over_temp,
						"--over-temp-warning") < 0)
				return -1;
			req->device_over_temperature_programmable_warning_threshold =
				over_temp;
			set_alert_action_bits(req, ALERT_BIT_DOTPWT);
		} else if (strcmp(argv[i], "--under-temp-warning") == 0 &&
			   i + 1 < argc) {
			if (parse_u16_threshold(argv[++i], &under_temp,
						"--under-temp-warning") < 0)
				return -1;
			req->device_under_temperature_programmable_warning_threshold =
				under_temp;
			set_alert_action_bits(req, ALERT_BIT_DUTPWT);
		} else if (strcmp(argv[i], "--volatile-mem-error-warning") == 0 &&
			   i + 1 < argc) {
			if (parse_u16_threshold(argv[++i], &volatile_err,
						"--volatile-mem-error-warning") < 0)
				return -1;
			req->corrected_volatile_mem_error_programmable_warning_threshold =
				volatile_err;
			set_alert_action_bits(req, ALERT_BIT_CVMEPWT);
		} else if (strcmp(argv[i], "--persistent-mem-error-warning") == 0 &&
			   i + 1 < argc) {
			if (parse_u16_threshold(argv[++i], &persistent_err,
						"--persistent-mem-error-warning") < 0)
				return -1;
			req->corrected_persistent_mem_error_programmable_warning_threshold =
				persistent_err;
			set_alert_action_bits(req, ALERT_BIT_CPMEPWT);
		} else {
			fprintf(stderr, "Usage: %s\n", SET_ALERT_CONFIG_USAGE);
			return -1;
		}
	}

	return 0;
}

int cmd_set_alert_config(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_set_alert_config_req req;
	int rc;

	rc = parse_set_alert_config_req(argc - 1, argv + 1, &req);
	if (rc)
		return rc;

	rc = cxlmi_cmd_memdev_set_alert_config(ep, NULL, &req);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "set alert config failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "set alert config ioctl failed\n");
		return rc;
	}

	print_set_alert_config_result(&req);
	return 0;
}
