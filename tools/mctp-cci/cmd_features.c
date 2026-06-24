// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

/* CXL §8.2.10.6.1 / Table 8-109: each Supported Feature Entry is 48 bytes. */
#define SUPPORTED_FEATURE_ENTRY_BYTES 48U
#define SUPPORTED_FEATURES_DEFAULT_COUNT SUPPORTED_FEATURE_ENTRY_BYTES

static void print_supported_features(const struct cxlmi_cmd_get_supported_features_rsp *rsp,
                                     uint16_t start_idx)
{
    unsigned int i;

    printf("device_supported_features: %u\n", rsp->device_supported_features);
    printf("entries_returned:          %u (starting_feature_index=%u)\n",
           rsp->num_supported_feature_entries, start_idx);

    for (i = 0; i < rsp->num_supported_feature_entries; i++) {
        printf("  [%u] feature_index: %u\n", i,
               rsp->supported_feature_entries[i].feature_index);
        printf("      feature_id:    ");
        print_uuid(rsp->supported_feature_entries[i].feature_id);
        printf("\n");
        printf("      get_size:      %u\n",
               rsp->supported_feature_entries[i].get_feature_size);
        printf("      set_size:      %u\n",
               rsp->supported_feature_entries[i].set_feature_size);
        printf("      attr_flags:    0x%08x\n",
               rsp->supported_feature_entries[i].attribute_flags);
        printf("      get_version:   %u\n",
               rsp->supported_feature_entries[i].get_feature_version);
        printf("      set_version:   %u\n",
               rsp->supported_feature_entries[i].set_feature_version);
        printf("      set_effects:   0x%04x\n",
               rsp->supported_feature_entries[i].set_feature_effects);
    }

    if (rsp->num_supported_feature_entries < rsp->device_supported_features) {
        uint16_t next = start_idx + rsp->num_supported_feature_entries;

        printf("note: %u more feature(s) on device; retry with "
               "'features supported %u <count_bytes>'\n",
               rsp->device_supported_features - rsp->num_supported_feature_entries,
               next);
    }
}

static int do_get_supported(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                            int argc, char **argv)
{
    struct cxlmi_cmd_get_supported_features_req req = {0};
    struct cxlmi_cmd_get_supported_features_rsp *rsp;
    size_t buf_sz;
    int rc;

    if (argc >= 1) req.starting_feature_index = strtoul(argv[0], NULL, 0);
    if (argc >= 2) req.count = strtoul(argv[1], NULL, 0);
    if (req.count == 0)
        req.count = SUPPORTED_FEATURES_DEFAULT_COUNT;

    if (req.count < SUPPORTED_FEATURE_ENTRY_BYTES) {
        fprintf(stderr,
                "features supported: count must be >= %u bytes "
                "(one Supported Feature Entry)\n",
                SUPPORTED_FEATURE_ENTRY_BYTES);
        return 2;
    }

    buf_sz = sizeof(*rsp) + req.count;
    rsp = calloc(1, buf_sz);
    if (!rsp) { perror("calloc"); return 2; }

    rc = cxlmi_cmd_get_supported_features(ep, ti, &req, rsp);
    if (rc < 0) { free(rsp); return mctp_cci_report_libcxlmi_error("get-supported-features"); }
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); free(rsp); return 1; }

    print_supported_features(rsp, req.starting_feature_index);
    free(rsp);
    return 0;
}

static int do_get_feature(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                          int argc, char **argv)
{
    struct cxlmi_cmd_get_feature_req req = {0};
    struct cxlmi_cmd_get_feature_rsp rsp;
    int rc;

    if (argc < 1) { fprintf(stderr, "usage: features get <feature_id>\n"); return 2; }
    memcpy(req.feature_id, argv[0], strnlen(argv[0], sizeof(req.feature_id)));
    rc = cxlmi_cmd_get_feature(ep, ti, &req, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-feature");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static const struct mctp_cci_cmd features_cmds[] = {
    { "supported", "Get Supported Features (0500h) [start_idx [count_bytes]]", do_get_supported },
    { "get",       "Get Feature (0501h) <feature_id>",                  do_get_feature },
};

const struct mctp_cci_top features_top = {
    .name = "features",
    .help = "FEATURES (0x05): supported, get",
    .cmds = features_cmds,
    .n_cmds = sizeof(features_cmds) / sizeof(features_cmds[0]),
};
