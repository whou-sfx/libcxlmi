// SPDX-License-Identifier: LGPL-2.1-or-later
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mctp-cci.h"

int mctp_cci_report_libcxlmi_error(const char *cmd_name)
{
	const char *msg = strerror(errno);

	if (errno && msg)
		fprintf(stderr, "%s: %s\n", cmd_name, msg);
	else
		fprintf(stderr, "%s: libcxlmi transport/parsing error\n", cmd_name);
	return -1;
}

/*
 * parse_tunnel_args: scan argv for --tunnel-port N, --tunnel-ld M,
 * --port-and-ld N,M, --tunnel-mhd. On match, set ti fields and remove
 * the matched options from argv (compacted in place). argc is the
 * ORIGINAL argc at call time; on return it points to the new count.
 */
int parse_tunnel_args(int argc, char **argv, struct cxlmi_tunnel_info *ti)
{
    int i, j = 0, wrote;
    int seen_port = 0, seen_ld = 0, seen_combo = 0, seen_mhd = 0;

    for (i = 0; i < argc; i++) {
        wrote = 1;
        if (strcmp(argv[i], "--tunnel-port") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--tunnel-port expects a value\n");
                return -1;
            }
            ti->port = atoi(argv[i + 1]);
            ti->ld = -1;
            ti->level = 1;
            ti->mhd = false;
            seen_port = 1;
            i++;
            wrote = 0;
        } else if (strcmp(argv[i], "--tunnel-ld") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--tunnel-ld expects a value\n");
                return -1;
            }
            ti->ld = atoi(argv[i + 1]);
            ti->port = -1;
            ti->level = 1;
            ti->mhd = false;
            seen_ld = 1;
            i++;
            wrote = 0;
        } else if (strcmp(argv[i], "--port-and-ld") == 0) {
            char *comma;
            if (i + 1 >= argc) {
                fprintf(stderr, "--port-and-ld expects N,M\n");
                return -1;
            }
            comma = strchr(argv[i + 1], ',');
            if (!comma) {
                fprintf(stderr, "--port-and-ld expects N,M\n");
                return -1;
            }
            *comma = '\0';
            ti->port = atoi(argv[i + 1]);
            ti->ld = atoi(comma + 1);
            ti->level = 2;
            ti->mhd = false;
            seen_combo = 1;
            i++;
            wrote = 0;
        } else if (strcmp(argv[i], "--tunnel-mhd") == 0) {
            ti->port = -1;
            ti->ld = -1;
            ti->level = 1;
            ti->mhd = true;
            seen_mhd = 1;
            wrote = 0;
        }
        if (wrote)
            argv[j++] = argv[i];
    }
    argv[j] = NULL;

    if (seen_combo && (seen_port || seen_ld)) {
        fprintf(stderr, "cannot combine --port-and-ld with --tunnel-port/--tunnel-ld\n");
        return -1;
    }
    if (seen_mhd && (seen_port || seen_ld || seen_combo)) {
        fprintf(stderr, "cannot combine --tunnel-mhd with other tunnel flags\n");
        return -1;
    }
    return j;
}

int parse_hex_u64(const char *str, uint64_t *out)
{
    char *end;
    if (!str || !*str) return -1;
    errno = 0;
    *out = strtoull(str, &end, 0);
    if (errno || *end != '\0') return -1;
    return 0;
}

int parse_size_with_unit(const char *str, uint64_t *out)
{
    char *end;
    uint64_t v;

    if (!str || !*str) return -1;
    v = strtoull(str, &end, 0);
    if (end == str) return -1;
    switch (*end) {
    case 'k': case 'K': v *= 1024; end++; break;
    case 'm': case 'M': v *= 1024 * 1024; end++; break;
    case 'g': case 'G': v *= 1024UL * 1024 * 1024; end++; break;
    case '\0': break;
    default: return -1;
    }
    if (*end != '\0') return -1;
    *out = v;
    return 0;
}

int parse_log_uuid(const char *str, uint8_t uuid[16])
{
    /* Accept "DEADBEEF..." (32 hex chars) or with "-" separators. */
    char buf[33];
    size_t in_len = strlen(str);
    size_t bi = 0, i;

    if (in_len == 32) {
        memcpy(buf, str, 32);
        buf[32] = '\0';
    } else if (in_len == 36) {
        for (i = 0; i < 36; i++) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (str[i] != '-') return -1;
                continue;
            }
            buf[bi++] = str[i];
        }
        buf[bi] = '\0';
        if (bi != 32) return -1;
    } else {
        return -1;
    }
    for (i = 0; i < 16; i++) {
        unsigned int byte;
        if (sscanf(buf + i * 2, "%2x", &byte) != 1) return -1;
        uuid[i] = (uint8_t)byte;
    }
    return 0;
}

void print_uuid(const uint8_t uuid[16])
{
    printf("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           uuid[0], uuid[1], uuid[2], uuid[3],
           uuid[4], uuid[5], uuid[6], uuid[7],
           uuid[8], uuid[9], uuid[10], uuid[11],
           uuid[12], uuid[13], uuid[14], uuid[15]);
}

void print_log_payload(const uint8_t *data, size_t len,
                       int has_text, int little_endian)
{
    size_t i;
    (void)little_endian;
    if (has_text) {
        fwrite(data, 1, len, stdout);
        fputc('\n', stdout);
        return;
    }
    for (i = 0; i < len; i++) {
        if (i && (i % 16) == 0) fputc('\n', stdout);
        printf("%02x ", data[i]);
    }
    fputc('\n', stdout);
}

int read_file_to_buffer(const char *path, void **buf, size_t *sz)
{
    FILE *fp = fopen(path, "rb");
    struct stat st;
    uint8_t *p;

    if (!fp) return -1;
    if (fstat(fileno(fp), &st) < 0) { fclose(fp); return -1; }
    p = malloc(st.st_size);
    if (!p) { fclose(fp); return -1; }
    if (fread(p, 1, st.st_size, fp) != (size_t)st.st_size) {
        free(p); fclose(fp); return -1;
    }
    fclose(fp);
    *buf = p;
    *sz = st.st_size;
    return 0;
}

int write_file_from_buffer(const char *path, const void *buf, size_t sz)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    if (fwrite(buf, 1, sz, fp) != sz) { fclose(fp); return -1; }
    fclose(fp);
    return 0;
}
