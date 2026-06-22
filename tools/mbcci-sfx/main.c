// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: a libcxlmi-based MB-CCI ioctl test tool.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

static const struct subcmd subcmds[] = {
	{ "identify_memdev",           cmd_identify_memdev,
	  "Send Memory Device Identify (4000h) to <memN>" },
	{ "get-partition",             cmd_get_partition,
	  "Get Partition Info (4100h)" },
	{ "set-partition",             cmd_set_partition,
	  "Set Partition Info (4101h) --next-volatile <MiB> [--flags <n>] [--bp-dirty-shutdown]" },
	{ "get-fw-info",               cmd_get_fw_info,
	  "Get FW Info (0200h)" },
	{ "transfer-fw",               cmd_transfer_fw,
	  "Transfer FW (0201h) --input <file> --slot <n> [--chunk-size <n>]" },
	{ "activate-fw",               cmd_activate_fw,
	  "Activate FW (0202h) --slot <n> [--action online|offline]" },
	{ "get-health-info",           cmd_get_health_info,
	  "Get Health Info (4200h)" },
	{ "get-alert-config",          cmd_get_alert_config,
	  "Get Alert Configuration (4201h)" },
	{ "set-alert-config",          cmd_set_alert_config,
	  "Set Alert Configuration (4202h) [--life-used-warning <pct>] [--over-temp-warning <n>] ..." },
	{ "get-sld-qos-ctrl",          cmd_get_sld_qos_ctrl,
	  "Get SLD QoS Control (4700h)" },
	{ "set-sld-qos-ctrl",          cmd_set_sld_qos_ctrl,
	  "Set SLD QoS Control (4701h) [--egress-congestion-control-enable <0|1>] [--egress-tpr-enable <0|1>] ..." },
	{ "get-sld-qos-status",        cmd_get_sld_qos_status,
	  "Get SLD QoS Status (4702h)" },
	{ "get-event-records",         cmd_get_event_records,
	  "Get Event Records (0100h) --log <info|warn|failure|fatal|dcd>" },
	{ "clear-event-records",       cmd_clear_event_records,
	  "Clear Event Records (0101h) --log <log> [--all] [--handle <h>...]" },
	{ "get-event-interrupt-policy",cmd_get_event_interrupt_policy,
	  "Get Event Interrupt Policy (0102h)" },
	{ "set-event-interrupt-policy",cmd_set_event_interrupt_policy,
	  "Set Event Interrupt Policy (0103h) --info <h> --warn <h> --failure <h> --fatal <h>" },
	{ "vu-evtadd",                 cmd_vu_evtadd,
	  "VU Inject Event (0xCC53/0x0129) --loglvl <0-3> --intfmask <hex> --count <n>" },
	{ "get-supported-logs",        cmd_get_supported_logs,
	  "Get Supported Logs (0400h)" },
	{ "get-supported-feat",        cmd_get_supported_feat,
	  "Get Supported Features (0500h) [--count <bytes>] [--start-index <n>]" },
	{ "get-log",                   cmd_get_log,
	  "Get Log (0401h) --uuid <hex32> [--offset <n>] [--length <n>]" },
	{ "get-vendor-log",            cmd_get_vendor_log,
	  "Fetch full Vendor Debug Log in 2K chunks -f <output_file>" },
	{ "get-timestamp",             cmd_get_timestamp,
	  "Get device timestamp (0300h)" },
	{ "set-timestamp",             cmd_set_timestamp,
	  "Set device timestamp (0301h) [--ts <ns>] (default: current host time)" },
	{ "sdb-tunnel",                cmd_sdb_tunnel,
	  "Tunnel CCI cmd via sideband (0xCCCC): identify|identify_memdev|get-partition|set-partition|get-fw-info|transfer-fw|activate-fw|get-health-info|get-alert-config|set-alert-config|get-sld-qos-ctrl|set-sld-qos-ctrl|get-sld-qos-status|fm-get-ld-info|fm-get-ld-alloc|get-supported-logs|get-supported-feat|get-log|get-resp-msg-limit|set-resp-msg-limit [--port ...]" },
};

static const size_t nsubcmds = sizeof(subcmds) / sizeof(subcmds[0]);

static void usage(FILE *out, const char *prog)
{
	size_t i;

	fprintf(out, "Usage: %s <memN> <subcommand> [args...]\n", prog);
	fprintf(out, "       %s -h | --help\n\n", prog);
	fprintf(out, "<memN>      CXL device under /dev/cxl/, e.g. mem0\n\n");
	fprintf(out, "Subcommands:\n");
	for (i = 0; i < nsubcmds; i++)
		fprintf(out, "  %-12s %s\n", subcmds[i].name, subcmds[i].help);
}

static const struct subcmd *find_subcmd(const char *name)
{
	size_t i;

	for (i = 0; i < nsubcmds; i++) {
		if (strcmp(subcmds[i].name, name) == 0)
			return &subcmds[i];
	}
	return NULL;
}

int main(int argc, char **argv)
{
	const char *prog = argv[0] ? argv[0] : "mbcci-sfx";
	const struct subcmd *sc;
	struct cxlmi_ctx *ctx;
	struct cxlmi_endpoint *ep;
	const char *devname;
	const char *cmdname;
	int rc;

	if (argc >= 2 &&
	    (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
		usage(stdout, prog);
		return EXIT_SUCCESS;
	}

	if (argc < 3) {
		usage(stderr, prog);
		return EXIT_FAILURE;
	}

	devname = argv[1];
	cmdname = argv[2];

	sc = find_subcmd(cmdname);
	if (!sc) {
		fprintf(stderr, "Unknown subcommand: %s\n\n", cmdname);
		usage(stderr, prog);
		return EXIT_FAILURE;
	}

	ctx = cxlmi_new_ctx(stderr, LOG_WARNING);
	if (!ctx) {
		fprintf(stderr, "cannot create libcxlmi context\n");
		return EXIT_FAILURE;
	}

	ep = cxlmi_open(ctx, devname);
	if (!ep) {
		fprintf(stderr, "cannot open '/dev/cxl/%s' endpoint\n", devname);
		cxlmi_free_ctx(ctx);
		return EXIT_FAILURE;
	}

	rc = sc->fn(ep, argc - 2, argv + 2);

	cxlmi_close(ep);
	cxlmi_free_ctx(ctx);

	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
