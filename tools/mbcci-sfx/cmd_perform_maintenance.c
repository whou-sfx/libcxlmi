// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Perform Maintenance (Opcode 0600h) — PPR and MBIST.
 */
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

#define PM_USAGE \
	"perform-maintenance ppr|mbist [args...]"

#define PM_PPR_USAGE \
	"perform-maintenance ppr --type sppr|hppr --dpa <hex> --nibble-mask <hex>" \
	" [--query-resources]"

#define PM_MBIST_USAGE \
	"perform-maintenance mbist --action full|start|continue|end|abort" \
	" [--offset <n>] [--start-address <hex>] [--length <hex>]" \
	" [--num-tests <n>] [--results-config <n>] [--config-flags <n>]" \
	" [--test-id <n> [--iterations <n>] [--test-flags <n>]" \
	" [--pattern-type <n>] [--pattern-value <n>]" \
	" [--prbs-seed <n>] [--error-threshold <n>]] ..."

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

static int parse_u64_hex(const char *arg, uint64_t *val, const char *name)
{
	char *end;

	errno = 0;
	*val = strtoull(arg, &end, 0);
	if (errno || end == arg || *end != '\0') {
		fprintf(stderr, "%s: invalid value '%s'\n", name, arg);
		return -1;
	}
	return 0;
}

static int parse_u32_val(const char *arg, uint32_t *val, const char *name)
{
	uint64_t tmp;

	if (parse_u64_hex(arg, &tmp, name))
		return -1;
	if (tmp > 0xffffffff) {
		fprintf(stderr, "%s: value out of range '%s'\n", name, arg);
		return -1;
	}
	*val = (uint32_t)tmp;
	return 0;
}

static int parse_u16_val(const char *arg, uint16_t *val, const char *name)
{
	uint64_t tmp;

	if (parse_u64_hex(arg, &tmp, name))
		return -1;
	if (tmp > 0xffff) {
		fprintf(stderr, "%s: value out of range '%s'\n", name, arg);
		return -1;
	}
	*val = (uint16_t)tmp;
	return 0;
}

static int parse_u8_val(const char *arg, uint8_t *val, const char *name)
{
	uint64_t tmp;

	if (parse_u64_hex(arg, &tmp, name))
		return -1;
	if (tmp > 0xff) {
		fprintf(stderr, "%s: value out of range '%s'\n", name, arg);
		return -1;
	}
	*val = (uint8_t)tmp;
	return 0;
}

/*
 * Parse a nibble mask hex string (up to 6 hex digits = 3 bytes).
 * Accepts "0x" prefix; stored big-endian in nibble_mask[3].
 */
static int parse_nibble_mask(const char *arg, uint8_t nibble_mask[3])
{
	char *end;
	unsigned long long val;

	errno = 0;
	val = strtoull(arg, &end, 16);
	if (errno || end == arg || *end != '\0' || val > 0xffffffULL) {
		fprintf(stderr,
			"--nibble-mask: expected up to 6 hex digits, got '%s'\n",
			arg);
		return -1;
	}
	nibble_mask[0] = (uint8_t)((val >> 16) & 0xff);
	nibble_mask[1] = (uint8_t)((val >>  8) & 0xff);
	nibble_mask[2] = (uint8_t)(val & 0xff);
	return 0;
}

/* ------------------------------------------------------------------ */
/* PPR argument parsing (shared with sdb-tunnel)                      */
/* ------------------------------------------------------------------ */

int parse_pm_ppr_args(int argc, char **argv, struct pm_ppr_args *out)
{
	int i, has_type = 0, has_dpa = 0, has_mask = 0;

	memset(out, 0, sizeof(*out));

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
			const char *t = argv[++i];

			if (strcmp(t, "sppr") == 0) {
				out->subclass = 0x00;
			} else if (strcmp(t, "hppr") == 0) {
				out->subclass = 0x01;
			} else {
				fprintf(stderr,
					"--type: expected sppr|hppr, got '%s'\n",
					t);
				return -1;
			}
			has_type = 1;
		} else if (strcmp(argv[i], "--query-resources") == 0) {
			out->flags |= 0x01;
		} else if (strcmp(argv[i], "--dpa") == 0 && i + 1 < argc) {
			if (parse_u64_hex(argv[++i], &out->dpa, "--dpa"))
				return -1;
			has_dpa = 1;
		} else if (strcmp(argv[i], "--nibble-mask") == 0 && i + 1 < argc) {
			if (parse_nibble_mask(argv[++i], out->nibble_mask))
				return -1;
			has_mask = 1;
		} else {
			fprintf(stderr, "Usage: %s\n", PM_PPR_USAGE);
			return -1;
		}
	}

	if (!has_type || !has_dpa || !has_mask) {
		fprintf(stderr,
			"perform-maintenance ppr: --type, --dpa and --nibble-mask are required\n");
		fprintf(stderr, "Usage: %s\n", PM_PPR_USAGE);
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* MBIST argument parsing (shared with sdb-tunnel)                    */
/* ------------------------------------------------------------------ */

int parse_pm_mbist_args(int argc, char **argv, struct pm_mbist_args *out)
{
	int i, has_action = 0, cur_test = -1;

	memset(out, 0, sizeof(*out));
	out->offset = 0;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--action") == 0 && i + 1 < argc) {
			const char *a = argv[++i];

			if (strcmp(a, "full") == 0)
				out->action = 0x00;
			else if (strcmp(a, "start") == 0)
				out->action = 0x01;
			else if (strcmp(a, "continue") == 0)
				out->action = 0x02;
			else if (strcmp(a, "end") == 0)
				out->action = 0x03;
			else if (strcmp(a, "abort") == 0)
				out->action = 0x04;
			else {
				fprintf(stderr,
					"--action: expected full|start|continue|end|abort, got '%s'\n",
					a);
				return -1;
			}
			has_action = 1;
		} else if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc) {
			if (parse_u32_val(argv[++i], &out->offset, "--offset"))
				return -1;
		} else if (strcmp(argv[i], "--start-address") == 0 && i + 1 < argc) {
			if (parse_u64_hex(argv[++i], &out->start_address,
					  "--start-address"))
				return -1;
		} else if (strcmp(argv[i], "--length") == 0 && i + 1 < argc) {
			if (parse_u64_hex(argv[++i], &out->length, "--length"))
				return -1;
		} else if (strcmp(argv[i], "--num-tests") == 0 && i + 1 < argc) {
			uint8_t n;

			if (parse_u8_val(argv[++i], &n, "--num-tests"))
				return -1;
			if (n > PM_MBIST_MAX_TESTS) {
				fprintf(stderr,
					"--num-tests: max %d, got %u\n",
					PM_MBIST_MAX_TESTS, n);
				return -1;
			}
			out->num_tests = n;
		} else if (strcmp(argv[i], "--results-config") == 0 && i + 1 < argc) {
			if (parse_u8_val(argv[++i], &out->results_config,
					 "--results-config"))
				return -1;
		} else if (strcmp(argv[i], "--config-flags") == 0 && i + 1 < argc) {
			if (parse_u8_val(argv[++i], &out->config_flags,
					 "--config-flags"))
				return -1;
		} else if (strcmp(argv[i], "--test-id") == 0 && i + 1 < argc) {
			cur_test++;
			if (cur_test >= PM_MBIST_MAX_TESTS) {
				fprintf(stderr,
					"--test-id: too many test entries (max %d)\n",
					PM_MBIST_MAX_TESTS);
				return -1;
			}
			if (parse_u16_val(argv[++i],
					  &out->tests[cur_test].test_id,
					  "--test-id"))
				return -1;
			/* default iterations = 1 */
			out->tests[cur_test].num_iterations = 1;
		} else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
			if (cur_test < 0) {
				fprintf(stderr,
					"--iterations: must follow --test-id\n");
				return -1;
			}
			if (parse_u8_val(argv[++i],
					 &out->tests[cur_test].num_iterations,
					 "--iterations"))
				return -1;
		} else if (strcmp(argv[i], "--test-flags") == 0 && i + 1 < argc) {
			if (cur_test < 0) {
				fprintf(stderr,
					"--test-flags: must follow --test-id\n");
				return -1;
			}
			if (parse_u16_val(argv[++i],
					  &out->tests[cur_test].flags,
					  "--test-flags"))
				return -1;
		} else if (strcmp(argv[i], "--pattern-type") == 0 && i + 1 < argc) {
			if (cur_test < 0) {
				fprintf(stderr,
					"--pattern-type: must follow --test-id\n");
				return -1;
			}
			if (parse_u16_val(argv[++i],
					  &out->tests[cur_test].pattern_type,
					  "--pattern-type"))
				return -1;
		} else if (strcmp(argv[i], "--pattern-value") == 0 && i + 1 < argc) {
			if (cur_test < 0) {
				fprintf(stderr,
					"--pattern-value: must follow --test-id\n");
				return -1;
			}
			if (parse_u8_val(argv[++i],
					 &out->tests[cur_test].pattern_value,
					 "--pattern-value"))
				return -1;
		} else if (strcmp(argv[i], "--prbs-seed") == 0 && i + 1 < argc) {
			if (cur_test < 0) {
				fprintf(stderr,
					"--prbs-seed: must follow --test-id\n");
				return -1;
			}
			if (parse_u32_val(argv[++i],
					  &out->tests[cur_test].prbs_seed,
					  "--prbs-seed"))
				return -1;
		} else if (strcmp(argv[i], "--error-threshold") == 0 && i + 1 < argc) {
			if (cur_test < 0) {
				fprintf(stderr,
					"--error-threshold: must follow --test-id\n");
				return -1;
			}
			if (parse_u16_val(argv[++i],
					  &out->tests[cur_test].error_count_threshold,
					  "--error-threshold"))
				return -1;
		} else {
			fprintf(stderr, "Usage: %s\n", PM_MBIST_USAGE);
			return -1;
		}
	}

	if (!has_action) {
		fprintf(stderr,
			"perform-maintenance mbist: --action is required\n");
		fprintf(stderr, "Usage: %s\n", PM_MBIST_USAGE);
		return -1;
	}

	/* if tests were specified via --test-id, reconcile num_tests */
	if (cur_test >= 0 && out->num_tests == 0)
		out->num_tests = (uint8_t)(cur_test + 1);

	return 0;
}

/* ------------------------------------------------------------------ */
/* PPR subcommand (mailbox path)                                       */
/* ------------------------------------------------------------------ */

static int cmd_pm_ppr(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct pm_ppr_args args;
	struct {
		struct cxlmi_cmd_perform_maintenance_req hdr;
		struct cxlmi_perform_maintenance_ppr_params ppr;
	} __attribute__((packed)) req;
	int rc;

	rc = parse_pm_ppr_args(argc, argv, &args);
	if (rc)
		return rc;

	memset(&req, 0, sizeof(req));
	req.hdr.maint_op_class    = 0x01; /* PPR */
	req.hdr.maint_op_subclass = args.subclass;
	req.ppr.flags             = args.flags;
	req.ppr.dpa               = args.dpa;
	memcpy(req.ppr.nibble_mask, args.nibble_mask, 3);

	rc = cxlmi_cmd_perform_maintenance(ep, NULL,
					   (struct cxlmi_cmd_perform_maintenance_req *)&req,
					   sizeof(req.ppr));
	if (rc && rc != CXLMI_RET_BACKGROUND) {
		if (rc > 0)
			fprintf(stderr,
				"perform-maintenance ppr failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"perform-maintenance ppr ioctl failed\n");
		return rc;
	}

	if (rc == CXLMI_RET_BACKGROUND)
		printf("Perform Maintenance (PPR) started as background operation\n");
	else
		printf("Perform Maintenance (PPR) OK\n");

	printf("  Type:         %s\n",
	       args.subclass == 0 ? "sPPR (Soft PPR)" : "hPPR (Hard PPR)");
	printf("  Query-only:   %s\n", (args.flags & 0x01) ? "yes" : "no");
	printf("  DPA:          0x%016llx\n", (unsigned long long)args.dpa);
	printf("  Nibble Mask:  %02x%02x%02x\n",
	       args.nibble_mask[0], args.nibble_mask[1], args.nibble_mask[2]);
	return 0;
}

/* ------------------------------------------------------------------ */
/* MBIST subcommand (mailbox path)                                     */
/* ------------------------------------------------------------------ */

static const char *mbist_action_name(uint8_t action)
{
	switch (action) {
	case 0x00: return "Full";
	case 0x01: return "Start Chunk";
	case 0x02: return "Continue Chunk";
	case 0x03: return "End Chunk";
	case 0x04: return "Abort";
	default:   return "Unknown";
	}
}

static int cmd_pm_mbist(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct pm_mbist_args args;
	struct cxlmi_cmd_perform_maintenance_req *req;
	struct cxlmi_perform_maintenance_mbist_params *mbist_hdr;
	struct cxlmi_media_test_common_config *cfg;
	struct cxlmi_media_test_params_entry *entries;
	size_t params_sz, req_sz;
	uint8_t t;
	int rc;

	rc = parse_pm_mbist_args(argc, argv, &args);
	if (rc)
		return rc;

	/*
	 * params layout (starting at req->params[]):
	 *   cxlmi_perform_maintenance_mbist_params header (6B: action+offset+rsvd)
	 *   cxlmi_media_test_common_config          (32B)
	 *   N * cxlmi_media_test_params_entry       (N * 32B)
	 */
	params_sz = sizeof(*mbist_hdr) +
		    sizeof(*cfg) +
		    args.num_tests * sizeof(*entries);
	req_sz = sizeof(*req) + params_sz;

	req = calloc(1, req_sz);
	if (!req) {
		perror("perform-maintenance mbist: calloc");
		return -1;
	}

	req->maint_op_class    = 0x03; /* Built-in Test */
	req->maint_op_subclass = 0x00; /* Media Test */

	mbist_hdr = (struct cxlmi_perform_maintenance_mbist_params *)req->params;
	mbist_hdr->action = args.action;
	mbist_hdr->offset = args.offset;

	cfg = (struct cxlmi_media_test_common_config *)mbist_hdr->test_params;
	cfg->num_tests                = args.num_tests;
	cfg->start_address            = args.start_address;
	cfg->length                   = args.length;
	cfg->media_test_results_config = args.results_config;
	cfg->config_flags             = args.config_flags;

	entries = (struct cxlmi_media_test_params_entry *)(cfg + 1);
	for (t = 0; t < args.num_tests; t++) {
		entries[t].test_id              = args.tests[t].test_id;
		entries[t].num_iterations       = args.tests[t].num_iterations;
		entries[t].flags                = args.tests[t].flags;
		entries[t].pattern_type         = args.tests[t].pattern_type;
		entries[t].pattern_value        = args.tests[t].pattern_value;
		entries[t].prbs_seed            = args.tests[t].prbs_seed;
		entries[t].error_count_threshold = args.tests[t].error_count_threshold;
	}

	rc = cxlmi_cmd_perform_maintenance(ep, NULL, req, params_sz);
	if (rc && rc != CXLMI_RET_BACKGROUND) {
		if (rc > 0)
			fprintf(stderr,
				"perform-maintenance mbist failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr,
				"perform-maintenance mbist ioctl failed\n");
		free(req);
		return rc;
	}

	if (rc == CXLMI_RET_BACKGROUND)
		printf("Perform Maintenance (MBIST) started as background operation\n");
	else
		printf("Perform Maintenance (MBIST) OK\n");

	printf("  Action:        %s\n", mbist_action_name(args.action));
	printf("  Offset:        %u\n", args.offset);
	printf("  Start Address: 0x%016llx\n",
	       (unsigned long long)args.start_address);
	printf("  Length:        0x%016llx (64B units)\n",
	       (unsigned long long)args.length);
	printf("  Num Tests:     %u\n", args.num_tests);
	for (t = 0; t < args.num_tests; t++)
		printf("  [%u] test_id=0x%04x iterations=%u flags=0x%04x\n",
		       t, args.tests[t].test_id,
		       args.tests[t].num_iterations,
		       args.tests[t].flags);

	free(req);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Top-level entry point                                               */
/* ------------------------------------------------------------------ */

int cmd_perform_maintenance(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s\n", PM_USAGE);
		return -1;
	}

	if (strcmp(argv[1], "ppr") == 0)
		return cmd_pm_ppr(ep, argc - 2, argv + 2);
	if (strcmp(argv[1], "mbist") == 0)
		return cmd_pm_mbist(ep, argc - 2, argv + 2);

	fprintf(stderr, "Unknown perform-maintenance action: %s\n", argv[1]);
	fprintf(stderr, "Usage: %s\n", PM_USAGE);
	return -1;
}
