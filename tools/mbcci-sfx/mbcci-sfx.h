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

int cmd_identify(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_get_event_records(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_clear_event_records(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_event_interrupt_policy(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_set_event_interrupt_policy(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_vu_evtadd(struct cxlmi_endpoint *ep, int argc, char **argv);

int cmd_get_supported_logs(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_log(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_vendor_log(struct cxlmi_endpoint *ep, int argc, char **argv);

#endif /* MBCCI_SFX_H */
