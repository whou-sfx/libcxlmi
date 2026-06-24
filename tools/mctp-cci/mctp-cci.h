// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mctp-cci: a libcxlmi-based MCTP CLI for sending CCI commands.
 *
 * Dispatch tables: each cmd_*.c exports a `const struct mctp_cci_top <name>_top;`.
 * main.c wires all tops into a static array and dispatches top→sub.
 */
#ifndef MCTP_CCI_H
#define MCTP_CCI_H

#include <libcxlmi.h>
#include <stddef.h>
#include <stdint.h>

struct mctp_cci_cmd {
    const char *name;
    const char *help;
    int (*fn)(struct cxlmi_endpoint *ep,
              struct cxlmi_tunnel_info *ti,
              int argc, char **argv);
};

struct mctp_cci_top {
    const char *name;
    const char *help;
    const struct mctp_cci_cmd *cmds;
    size_t n_cmds;
};

/* Shared utility helpers (util.c) */
int parse_tunnel_args(int argc, char **argv, struct cxlmi_tunnel_info *ti);
int parse_log_uuid(const char *str, uint8_t uuid[16]);
int parse_hex_u64(const char *str, uint64_t *out);
int parse_size_with_unit(const char *str, uint64_t *out);
void print_uuid(const uint8_t uuid[16]);
void print_log_payload(const uint8_t *data, size_t len,
                       int has_text, int little_endian);
int cel_uuid_match(const uint8_t uuid[16]);
void print_cel_log(const uint8_t *data, size_t len, uint32_t base_offset);
int read_file_to_buffer(const char *path, void **buf, size_t *sz);
int write_file_from_buffer(const char *path, const void *buf, size_t sz);
int mctp_cci_report_libcxlmi_error(const char *cmd_name);

/* Top-level table symbols — defined one per cmd_*.c, referenced from main.c */
extern const struct mctp_cci_top info_top;
extern const struct mctp_cci_top events_top;
extern const struct mctp_cci_top fw_top;
extern const struct mctp_cci_top ts_top;
extern const struct mctp_cci_top logs_top;
extern const struct mctp_cci_top features_top;
extern const struct mctp_cci_top identify_top;
extern const struct mctp_cci_top partition_top;
extern const struct mctp_cci_top health_top;
extern const struct mctp_cci_top poison_top;
extern const struct mctp_cci_top sanitize_top;
extern const struct mctp_cci_top pmem_top;
extern const struct mctp_cci_top security_top;
extern const struct mctp_cci_top qos_top;
extern const struct mctp_cci_top dcd_top;
extern const struct mctp_cci_top switch_top;
extern const struct mctp_cci_top mld_top;
extern const struct mctp_cci_top dcd_mgmt_top;
extern const struct mctp_cci_top vendor_top;

#endif /* MCTP_CCI_H */
