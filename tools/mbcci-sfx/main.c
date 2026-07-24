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
	{ "get-poison-list",           cmd_get_poison_list,
	  "Get Poison List (4300h) --dpa <addr> --length <bytes> [--frestart]" },
	{ "inject-poison",             cmd_inject_poison,
	  "Inject Poison (4301h) --dpa <addr>" },
	{ "clear-poison",              cmd_clear_poison,
	  "Clear Poison (4302h) --dpa <addr> [--write-data <128-hex-digits>]" },
	{ "get-scan-media-cap",        cmd_get_scan_media_cap,
	  "Get Scan Media Capabilities (4303h) --dpa <addr> --length <bytes>" },
	{ "scan-media",                cmd_scan_media,
	  "Scan Media (4304h) --dpa <addr> --length <bytes> [--no-evtlog]" },
	{ "get-scan-media-results",    cmd_get_scan_media_results,
	  "Get Scan Media Results (4305h)" },
	{ "media-operation",           cmd_media_operation,
	  "Media Operations (4402h) discovery|sanitize [args...]" },
	{ "perform-maintenance",       cmd_perform_maintenance,
	  "Perform Maintenance (0600h) ppr|mbist [args...]" },
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
	{ "vu-dlcfg",                  cmd_vu_dlcfg,
	  "VU Download Config (0xCC53/0x07) --input <file> --cfg-type <DEV|DDR> [--chunk-size <n>]" },
	{ "vu-getdevcfg",              cmd_vu_getdevcfg,
	  "VU Get DEV Config (0xCC53/0x08) --output <file>" },
	{ "vu-ddrfreq",                cmd_vu_ddrfreq,
	  "VU Set DDR Freq (0xCC53/0x09) --freq <MT/s>" },
	{ "vu-pciespeed",              cmd_vu_pciespeed,
	  "VU Set PCIe Speed (0xCC53/0x0a) --pcie-port <0|1> --speed <gen1..gen6> --width <x1|x2|x4|x8>" },
	{ "get-supported-logs",        cmd_get_supported_logs,
	  "Get Supported Logs (0400h)" },
	{ "get-supported-feat",        cmd_get_supported_feat,
	  "Get Supported Features (0500h) [--count <bytes>] [--start-index <n>]" },
	{ "get-feature",               cmd_get_feature,
	  "Get Feature (0501h) --feature-id <uuid> [--offset <n>] [--count <n>] [--selection <n>] [--dump <file>]" },
	{ "set-feature",               cmd_set_feature,
	  "Set Feature (0502h) --feature-id <uuid> --input <hexfile> [--offset <n>] [--flags <n>] [--version <n>]" },
	{ "get-log",                   cmd_get_log,
	  "Get Log (0401h) --uuid <hex32> [--offset <n>] [--length <n>]" },
	{ "get-log-cap",               cmd_get_log_cap,
	  "Get Log Capabilities (0402h) --uuid <hex32>" },
	{ "clear-log",                 cmd_clear_log,
	  "Clear Log (0403h) --uuid <hex32>" },
	{ "populate-log",              cmd_populate_log,
	  "Populate Log (0404h) --uuid <hex32>" },
	{ "get-vendor-log",            cmd_get_vendor_log,
	  "Fetch full Vendor Debug Log in 2K chunks -f <output_file>" },
	{ "get-timestamp",             cmd_get_timestamp,
	  "Get device timestamp (0300h)" },
	{ "set-timestamp",             cmd_set_timestamp,
	  "Set device timestamp (0301h) [--ts <ns>] (default: current host time)" },
	{ "sdb-tunnel",                cmd_sdb_tunnel,
	  "Tunnel CCI cmd via sideband (0xCCCC): identify|identify_memdev|get-partition|set-partition|get-fw-info|transfer-fw|vu-dlcfg|vu-getdevcfg|vu-ddrfreq|vu-pciespeed|activate-fw|get-health-info|get-alert-config|set-alert-config|get-poison-list|inject-poison|clear-poison|get-scan-media-cap|scan-media|get-scan-media-results|get-sld-qos-ctrl|set-sld-qos-ctrl|get-sld-qos-status|fm-get-ld-info|fm-get-ld-alloc|fm-set-ld-alloc|fm-get-qos-ctrl|fm-set-qos-ctrl|fm-get-qos-status|fm-get-qos-alloc-bw|fm-set-qos-alloc-bw|fm-get-qos-bw-limit|fm-set-qos-bw-limit|get-supported-logs|get-supported-feat|get-feature|set-feature|get-log|get-log-cap|clear-log|populate-log|bg-op-status|get-resp-msg-limit|set-resp-msg-limit|perform-maintenance [--port ...]" },
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
