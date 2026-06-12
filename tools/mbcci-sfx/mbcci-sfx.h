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

int cmd_get_event_records(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_clear_event_records(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_event_interrupt_policy(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_set_event_interrupt_policy(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_vu_evtadd(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_get_supported_logs(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_log(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_vendor_log(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_get_timestamp(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_set_timestamp(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_sdb_tunnel(struct cxlmi_endpoint *ep, int argc, char **argv);

#endif /* MBCCI_SFX_H */
