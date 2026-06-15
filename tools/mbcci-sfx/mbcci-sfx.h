/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * mbcci-sfx: a libcxlmi-based MB-CCI ioctl test tool.
 *
 * This header defines the subcommand dispatch table used by main.c.
 * Add new subcommands by appending entries to the subcmds[] array in
 * main.c and providing matching cmd_*() implementations in their own
 * translation units.
 */
#ifndef MBCCI_SFX_H
#define MBCCI_SFX_H

#include <libcxlmi.h>

struct subcmd {
	const char *name;
	int (*fn)(struct cxlmi_endpoint *ep, int argc, char **argv);
	const char *help;
};

int cmd_identify_memdev(struct cxlmi_endpoint *ep, int argc, char **argv);
void print_memdev_identify(const struct cxlmi_cmd_memdev_identify_rsp *id);

int cmd_get_partition(struct cxlmi_endpoint *ep, int argc, char **argv);
void print_memdev_partition_info(
	const struct cxlmi_cmd_memdev_get_partition_info_rsp *pi);

int cmd_set_partition(struct cxlmi_endpoint *ep, int argc, char **argv);
int parse_set_partition_req(int argc, char **argv,
			    struct cxlmi_cmd_memdev_set_partition_info_req *req);
void print_set_partition_result(
	const struct cxlmi_cmd_memdev_set_partition_info_req *req);

int cmd_get_fw_info(struct cxlmi_endpoint *ep, int argc, char **argv);
void print_get_fw_info(const struct cxlmi_cmd_get_fw_info_rsp *fw);

#define CXL_FW_XFER_FIXED 0x80

struct transfer_fw_params {
	const char *input_file;
	uint8_t slot;
	uint32_t chunk_size;
};

typedef int (*transfer_fw_send_fn)(struct cxlmi_endpoint *ep, void *ctx,
				   struct cxlmi_cmd_transfer_fw_req *req,
				   size_t data_sz);

int parse_transfer_fw_req(int argc, char **argv,
			  struct transfer_fw_params *params);
int transfer_fw_file(struct cxlmi_endpoint *ep,
		     const struct transfer_fw_params *params,
		     transfer_fw_send_fn send_fn, void *send_ctx);
int cmd_transfer_fw(struct cxlmi_endpoint *ep, int argc, char **argv);

int parse_activate_fw_req(int argc, char **argv,
			  struct cxlmi_cmd_activate_fw_req *req);
void print_activate_fw_result(const struct cxlmi_cmd_activate_fw_req *req,
			      int rc);
int cmd_activate_fw(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_get_health_info(struct cxlmi_endpoint *ep, int argc, char **argv);
void print_memdev_health_info(
	const struct cxlmi_cmd_memdev_get_health_info_rsp *hi);

int cmd_get_alert_config(struct cxlmi_endpoint *ep, int argc, char **argv);
void print_memdev_alert_config(
	const struct cxlmi_cmd_memdev_get_alert_config_rsp *ac);

int parse_set_alert_config_req(int argc, char **argv,
			       struct cxlmi_cmd_memdev_set_alert_config_req *req);
void print_set_alert_config_result(
	const struct cxlmi_cmd_memdev_set_alert_config_req *req);
int cmd_set_alert_config(struct cxlmi_endpoint *ep, int argc, char **argv);

void print_qos_telemetry_control(uint8_t val);
void print_sld_qos_control(
	const struct cxlmi_cmd_memdev_get_sld_qos_control_rsp *rsp);
int cmd_get_sld_qos_ctrl(struct cxlmi_endpoint *ep, int argc, char **argv);

int parse_set_sld_qos_ctrl_req(int argc, char **argv,
			       struct cxlmi_cmd_memdev_set_sld_qos_control_req *req);
void print_set_sld_qos_ctrl_result(
	const struct cxlmi_cmd_memdev_set_sld_qos_control_req *req);
int cmd_set_sld_qos_ctrl(struct cxlmi_endpoint *ep, int argc, char **argv);

void print_sld_qos_status(
	const struct cxlmi_cmd_memdev_get_sld_qos_status_rsp *rsp);
int cmd_get_sld_qos_status(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_get_event_records(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_clear_event_records(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_event_interrupt_policy(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_set_event_interrupt_policy(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_vu_evtadd(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_get_supported_logs(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_log(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_vendor_log(struct cxlmi_endpoint *ep, int argc, char **argv);

struct get_log_params {
	uint8_t uuid[16];
	uint32_t offset;
	uint32_t length;
	int has_uuid;
	int has_length;
	int has_text;
};

void print_log_uuid(const uint8_t *uuid);
void print_supported_logs(const struct cxlmi_cmd_get_supported_logs_rsp *rsp);
int parse_log_uuid(const char *str, uint8_t *out);
int parse_get_log_req(int argc, char **argv, struct get_log_params *params);
uint32_t lookup_log_size(const struct cxlmi_cmd_get_supported_logs_rsp *srsp,
			 const uint8_t uuid[16]);
void print_log_header(const uint8_t uuid[16], uint32_t offset, uint32_t length);
void print_log_payload(const uint8_t uuid[16], uint32_t offset, uint32_t length,
			 const uint8_t *buf, int has_text);

int cel_uuid_match(const uint8_t uuid[16]);
void print_cel_log(const uint8_t *data, size_t len, uint32_t base_offset);

int cmd_get_timestamp(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_set_timestamp(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_sdb_tunnel(struct cxlmi_endpoint *ep, int argc, char **argv);

void print_qos_telemetry_capability(uint8_t val);
void print_fm_get_ld_info(const struct cxlmi_cmd_fmapi_get_ld_info_rsp *rsp);

int parse_fm_get_ld_alloc_req(int argc, char **argv,
			      struct cxlmi_cmd_fmapi_get_ld_allocations_req *req);
void print_fm_get_ld_alloc(
	const struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *rsp);

#endif /* MBCCI_SFX_H */
