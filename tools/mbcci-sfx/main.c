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
	{ "identify",                  cmd_identify,
	  "Send Memory Device Identify (4000h) to <memN>" },
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
	{ "get-log",                   cmd_get_log,
	  "Get Log (0401h) --uuid <hex32> [--offset <n>] [--length <n>]" },
	{ "get-vendor-log",            cmd_get_vendor_log,
	  "Fetch full Vendor Debug Log in 2K chunks -f <output_file>" },
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
