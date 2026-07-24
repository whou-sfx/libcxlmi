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

int parse_get_poison_list_req(
	int argc, char **argv,
	struct cxlmi_cmd_memdev_get_poison_list_req *req,
	int *frestart);
int parse_inject_poison_req(
	int argc, char **argv,
	struct cxlmi_cmd_memdev_inject_poison_req *req);
int parse_clear_poison_req(
	int argc, char **argv,
	struct cxlmi_cmd_memdev_clear_poison_req *req);
void print_poison_list(
	const struct cxlmi_cmd_memdev_get_poison_list_rsp *rsp);
int cmd_get_poison_list(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_inject_poison(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_clear_poison(struct cxlmi_endpoint *ep, int argc, char **argv);

int parse_get_scan_media_cap_req(
	int argc, char **argv,
	struct cxlmi_cmd_memdev_get_scan_media_capabilities_req *req);
int parse_scan_media_req(int argc, char **argv,
			 struct cxlmi_cmd_memdev_scan_media_req *req);
void print_scan_media_capabilities(
	const struct cxlmi_cmd_memdev_get_scan_media_capabilities_rsp *rsp);
void print_scan_media_results(
	const struct cxlmi_cmd_memdev_get_scan_media_results_rsp *rsp);
int cmd_get_scan_media_cap(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_scan_media(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_scan_media_results(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_media_operation(struct cxlmi_endpoint *ep, int argc, char **argv);

/* Perform Maintenance (0600h) shared structures and helpers */
struct pm_ppr_args {
	uint8_t  subclass;	/* 0=sPPR, 1=hPPR */
	uint8_t  flags;		/* Bit0: query-resources */
	uint64_t dpa;
	uint8_t  nibble_mask[3];
};

#define PM_MBIST_MAX_TESTS 8
struct pm_mbist_test_entry {
	uint16_t test_id;
	uint8_t  num_iterations;
	uint16_t flags;
	uint16_t pattern_type;
	uint8_t  pattern_value;
	uint32_t prbs_seed;
	uint16_t error_count_threshold;
};

struct pm_mbist_args {
	uint8_t  action;
	uint32_t offset;
	uint64_t start_address;
	uint64_t length;
	uint8_t  results_config;
	uint8_t  config_flags;
	uint8_t  num_tests;
	struct pm_mbist_test_entry tests[PM_MBIST_MAX_TESTS];
};

int parse_pm_ppr_args(int argc, char **argv, struct pm_ppr_args *out);
int parse_pm_mbist_args(int argc, char **argv, struct pm_mbist_args *out);
int cmd_perform_maintenance(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_vu_evtadd(struct cxlmi_endpoint *ep, int argc, char **argv);

struct vu_dlcfg_params {
	const char *input_file;
	uint32_t cfg_type;
	uint32_t chunk_size;
};

typedef int (*vu_dlcfg_send_fn)(struct cxlmi_endpoint *ep, void *ctx,
				void *req, size_t req_sz);

int vu_mb_unlock(struct cxlmi_endpoint *ep);
int vu_mb_lock(struct cxlmi_endpoint *ep);
int parse_vu_dlcfg_req(int argc, char **argv, struct vu_dlcfg_params *params);
int vu_dlcfg_file(struct cxlmi_endpoint *ep,
		  const struct vu_dlcfg_params *params,
		  vu_dlcfg_send_fn send_fn, void *send_ctx);
int cmd_vu_dlcfg(struct cxlmi_endpoint *ep, int argc, char **argv);

#define VU_GETCFG_OUT_PAYLOAD 512
#define VU_GETCFG_REQ_BYTES   32
#define VU_GETCFG_OUTPUT_BYTES (8 + VU_GETCFG_OUT_PAYLOAD)

struct vu_getdevcfg_params {
	const char *output_file;
};

typedef int (*vu_getcfg_send_fn)(struct cxlmi_endpoint *ep, void *ctx,
				 void *req, void *out);

int parse_vu_getdevcfg_req(int argc, char **argv,
			   struct vu_getdevcfg_params *params);
int vu_getdevcfg_fetch(struct cxlmi_endpoint *ep,
		       const struct vu_getdevcfg_params *params,
		       vu_getcfg_send_fn send_fn, void *send_ctx);
int cmd_vu_getdevcfg(struct cxlmi_endpoint *ep, int argc, char **argv);

#define VU_CFGFREQ_REQ_BYTES  32
#define VU_CFGPCIE_REQ_BYTES  32

struct vu_ddrfreq_params {
	uint32_t freqmts;
};

struct vu_pciespeed_params {
	uint32_t portid;
	uint32_t speed;
	uint32_t width;
};

size_t vu_ddrfreq_pack(const struct vu_ddrfreq_params *params,
		       void *buf, size_t buf_sz);
size_t vu_pciespeed_pack(const struct vu_pciespeed_params *params,
			 void *buf, size_t buf_sz);
int parse_vu_ddrfreq_req(int argc, char **argv,
			 struct vu_ddrfreq_params *params);
int parse_vu_pciespeed_req(int argc, char **argv,
			   struct vu_pciespeed_params *params);
int vu_ddrfreq_send(struct cxlmi_endpoint *ep,
		    const struct vu_ddrfreq_params *params);
int vu_pciespeed_send(struct cxlmi_endpoint *ep,
		      const struct vu_pciespeed_params *params);
int cmd_vu_ddrfreq(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_vu_pciespeed(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_get_supported_logs(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_log(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_log_cap(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_clear_log(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_populate_log(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_vendor_log(struct cxlmi_endpoint *ep, int argc, char **argv);

#define MBCCI_FEATURE_ENTRY_SZ 48
#define MBCCI_FEATURE_DEFAULT_COUNT (16 * MBCCI_FEATURE_ENTRY_SZ)
#define MBCCI_FEATURE_RSP_BUF_SZ(count) \
	(sizeof(struct cxlmi_cmd_get_supported_features_rsp) + (count))

enum mbcci_feature_kind {
	MBCCI_FEAT_UNKNOWN = 0,
	MBCCI_FEAT_SPPR,
	MBCCI_FEAT_HPPR,
	MBCCI_FEAT_PARTIAL_SCRUB,
	MBCCI_FEAT_DDR5_ECS,
	MBCCI_FEAT_CVME,
	MBCCI_FEAT_ADDRESS_POLICY,
	MBCCI_FEAT_RAS,
	MBCCI_FEAT_CMC_REFRESH,
	MBCCI_FEAT_DUAL_PORT,
};

enum mbcci_feature_kind mbcci_feature_kind(const uint8_t *uuid);

int cmd_get_supported_feat(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_feature(struct cxlmi_endpoint *ep, int argc, char **argv);
int parse_get_supported_features_req(int argc, char **argv,
				     struct cxlmi_cmd_get_supported_features_req *req);

struct get_feature_params {
	struct cxlmi_cmd_get_feature_req req;
	int has_count;
	const char *dump_file;
};

struct set_feature_params {
	uint8_t feature_id[16];
	const char *input_file;
	uint16_t offset;
	uint32_t set_feature_flags;
	uint8_t version;
	int has_version;
};

int write_hex_payload_file(const char *path, const uint8_t *buf, size_t len);
int read_hex_payload_file(const char *path, uint8_t *buf, size_t max_len,
			  size_t *out_len);

int parse_get_feature_req(int argc, char **argv,
			  struct get_feature_params *params);
int parse_set_feature_req(int argc, char **argv,
			  struct set_feature_params *params);
uint16_t lookup_feature_size(
	const struct cxlmi_cmd_get_supported_features_rsp *sfrsp,
	const uint8_t feature_id[16]);
uint16_t lookup_set_feature_size(
	const struct cxlmi_cmd_get_supported_features_rsp *sfrsp,
	const uint8_t feature_id[16]);
uint8_t lookup_set_feature_version(
	const struct cxlmi_cmd_get_supported_features_rsp *sfrsp,
	const uint8_t feature_id[16]);
uint16_t lookup_feature_size_doc(const uint8_t feature_id[16]);
uint16_t lookup_set_feature_size_doc(const uint8_t feature_id[16]);
uint8_t lookup_set_feature_version_doc(const uint8_t feature_id[16]);
uint16_t resolve_get_feature_count(struct cxlmi_endpoint *ep,
				   const uint8_t feature_id[16]);
uint16_t resolve_set_feature_count(struct cxlmi_endpoint *ep,
				   const uint8_t feature_id[16]);
uint8_t resolve_set_feature_version(struct cxlmi_endpoint *ep,
				    const uint8_t feature_id[16]);
int cmd_set_feature(struct cxlmi_endpoint *ep, int argc, char **argv);
void print_supported_features(
	const struct cxlmi_cmd_get_supported_features_rsp *rsp);
void print_feature_header(const struct cxlmi_cmd_get_feature_req *req);
void print_feature_payload(uint16_t offset, uint16_t count,
			   const uint8_t *buf);

void print_feature_data(const uint8_t feature_id[16], uint16_t offset,
			uint16_t count, const uint8_t *buf);

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
int parse_log_uuid_req(int argc, char **argv, uint8_t uuid[16],
		       const char *usage);
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

struct fm_get_ld_alloc_params {
	struct cxlmi_cmd_fmapi_get_ld_allocations_req req;
	const char *raw_dump_file;
};

struct fm_set_ld_alloc_params {
	const char *input_file;
	uint8_t number_ld;
	uint8_t start_ld_id;
	int has_number_ld;
	int has_start_ld_id;
};

#define MBCCI_FM_SET_LD_ALLOC_HDR_SZ 4

#define MBCCI_FM_GET_LD_ALLOC_HDR_SZ 4

int parse_fm_get_ld_alloc_req(int argc, char **argv,
			      struct fm_get_ld_alloc_params *params);
size_t fm_set_ld_alloc_payload_size(uint8_t number_ld);
size_t fm_get_ld_alloc_payload_size(uint8_t ld_allocation_list_len);
int fm_ld_alloc_build_get_rsp_payload(
	const struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *rsp,
	uint8_t *buf, size_t buf_sz, size_t *out_len);
int fm_ld_alloc_normalize_set_payload(uint8_t *payload, size_t payload_len,
				      const struct fm_set_ld_alloc_params *params,
				      uint8_t *number_ld_out);
int parse_fm_set_ld_alloc_req(int argc, char **argv,
			      struct fm_set_ld_alloc_params *params);
void print_fm_get_ld_alloc(
	const struct cxlmi_cmd_fmapi_get_ld_allocations_rsp *rsp);
void print_fm_set_ld_alloc(
	const struct cxlmi_cmd_fmapi_set_ld_allocations_rsp *rsp);

#define MBCCI_FM_QOS_LD_HDR_SZ 2

struct fm_get_qos_ld_params {
	struct cxlmi_cmd_fmapi_get_qos_allocated_bw_req req;
	const char *raw_dump_file;
};

struct fm_set_qos_ld_params {
	const char *input_file;
	uint8_t number_ld;
	uint8_t start_ld_id;
	int has_number_ld;
	int has_start_ld_id;
};

struct fm_set_qos_ctrl_params {
	struct cxlmi_cmd_fmapi_set_qos_control_req req;
	const char *input_file;
};

size_t fm_qos_ld_payload_size(uint8_t number_ld);
void print_fm_qos_control(const struct cxlmi_cmd_fmapi_get_qos_control_rsp *rsp);
void print_fm_qos_status(const struct cxlmi_cmd_fmapi_get_qos_status_rsp *rsp);
void print_fm_qos_allocated_bw(
	const struct cxlmi_cmd_fmapi_get_qos_allocated_bw_rsp *rsp);
void print_fm_qos_bw_limit(const struct cxlmi_cmd_fmapi_get_qos_bw_limit_rsp *rsp);
int parse_fm_get_qos_ld_req(int argc, char **argv,
			    struct fm_get_qos_ld_params *params);
int parse_fm_set_qos_ld_req(int argc, char **argv,
			    struct fm_set_qos_ld_params *params);
int fm_qos_ld_apply_set_overrides(uint8_t *payload, size_t payload_len,
				  const struct fm_set_qos_ld_params *params,
				  uint8_t *number_ld_out);
int parse_fm_set_qos_ctrl_req(int argc, char **argv,
			      struct fm_set_qos_ctrl_params *params);
void fm_qos_ctrl_host_to_wire(const struct cxlmi_cmd_fmapi_set_qos_control_req *host,
			      struct cxlmi_cmd_fmapi_set_qos_control_req *wire);
void fm_qos_ctrl_wire_to_host(const struct cxlmi_cmd_fmapi_get_qos_control_rsp *wire,
			      struct cxlmi_cmd_fmapi_get_qos_control_rsp *host);
int fm_qos_ctrl_parse_input_payload(const uint8_t *payload, size_t payload_len,
				    struct cxlmi_cmd_fmapi_set_qos_control_req *req);

#endif /* MBCCI_SFX_H */
