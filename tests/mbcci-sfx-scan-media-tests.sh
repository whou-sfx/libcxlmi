#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Exercise mbcci-sfx Scan Media commands on a real CXL device:
#   get-scan-media-cap -> scan-media --no-evtlog -> sleep 5s
#   -> get-scan-media-results
#
# Covers:
#   1. mailbox (direct ioctl)
#   2. sdb-tunnel --port vdm1
#   3. sdb-tunnel --port i3c
#
# Defaults: --dpa 0 --length 4194304 (overridable via CLI).
# Exits non-zero if any test failed; continues across interfaces.
#
# Notes:
#   - Scan Media is a background operation; this script waits a fixed 5s.
#   - Mailbox RAW opcodes 4303h-4305h may need driver allow-list / raw_allow_all.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PASS_COUNT=0
FAIL_COUNT=0
FAILED_LABELS=()
FAILED_EXITS=()

SCAN_DPA=0
SCAN_LENGTH=4194304
SCAN_SLEEP_SECS=5

usage() {
	cat <<EOF
Usage: $0 [memN] [--dpa <addr>] [--length <bytes>]

Run mbcci-sfx Scan Media tests against a CXL memory device.
For each interface (mailbox, sdb-tunnel vdm1, sdb-tunnel i3c):
  1. get-scan-media-cap
  2. scan-media --no-evtlog
  3. sleep ${SCAN_SLEEP_SECS}s
  4. get-scan-media-results

Arguments:
  memN              CXL device name (default: mem0), e.g. mem0, mem1
  --dpa <addr>      Start DPA (default: 0). Decimal or 0x hex.
  --length <bytes>  Scan length in bytes (default: 4194304). Decimal or 0x hex.

Environment:
  MBCCI_SFX              Path to mbcci-sfx binary (auto-detected if unset)
  VERIFICATION_LOG_DIR   Optional directory to store per-command logs

Notes:
  - Scan Media starts as a background operation; this script sleeps a fixed
    ${SCAN_SLEEP_SECS}s before get-scan-media-results (not a completion poll).
  - Mailbox path uses RAW opcodes 4303h-4305h; the running CXL driver may
    reject them unless raw commands are allowed (e.g. raw_allow_all).

Examples:
  $0
  $0 mem1
  $0 --dpa 0x1000 --length 0x100000
  $0 mem0 --dpa 0 --length 8388608
  MBCCI_SFX=./build/tools/mbcci-sfx/mbcci-sfx $0
EOF
}

die() {
	echo "ERROR: $*" >&2
	exit 1
}

resolve_mbcci_sfx() {
	if [ -n "${MBCCI_SFX:-}" ]; then
		[ -x "$MBCCI_SFX" ] || die "MBCCI_SFX is not executable: $MBCCI_SFX"
		echo "$MBCCI_SFX"
		return
	fi

	local candidate="$PROJECT_DIR/build/tools/mbcci-sfx/mbcci-sfx"
	if [ -x "$candidate" ]; then
		echo "$candidate"
		return
	fi

	if command -v mbcci-sfx >/dev/null 2>&1; then
		command -v mbcci-sfx
		return
	fi

	die "mbcci-sfx not found. Set MBCCI_SFX or build with: meson compile -C build tools/mbcci-sfx"
}

# Parse a non-negative integer (decimal or 0x hex) to a decimal string.
parse_u64() {
	local name="$1"
	local raw="$2"
	local val

	if [[ ! "$raw" =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ]]; then
		die "$name must be a non-negative integer (decimal or 0x hex): '$raw'"
	fi

	val=$((raw))
	if [ "$val" -lt 0 ]; then
		die "$name must be non-negative: $raw"
	fi

	printf '%d' "$val"
}

format_hex() {
	local val="$1"
	printf '0x%x' "$val"
}

sanitize_label() {
	local label="$1"
	label="${label//\//_}"
	label="${label//:/_}"
	echo "$label"
}

record_pass() {
	local label="$1"
	echo "    PASS"
	PASS_COUNT=$((PASS_COUNT + 1))
}

record_fail() {
	local label="$1"
	local rc="$2"
	echo "    FAIL (exit $rc)" >&2
	FAILED_LABELS+=("$label")
	FAILED_EXITS+=("$rc")
	FAIL_COUNT=$((FAIL_COUNT + 1))
}

run_cmd() {
	local label="$1"
	shift
	local rc=0
	local logfile=""
	local out=""

	echo "==> [$label] $*"
	if [ -n "${VERIFICATION_LOG_DIR:-}" ]; then
		mkdir -p "$VERIFICATION_LOG_DIR"
		logfile="$VERIFICATION_LOG_DIR/$(sanitize_label "$label").log"
		if out="$("$@" 2>&1)"; then
			rc=0
		else
			rc=$?
		fi
		printf '%s\n' "$out" | tee "$logfile"
		[ "$rc" -ne 0 ] && printf '%s\n' "$out" >&2
	else
		if out="$("$@" 2>&1)"; then
			rc=0
		else
			rc=$?
		fi
		printf '%s\n' "$out"
		[ "$rc" -ne 0 ] && printf '%s\n' "$out" >&2
	fi

	if [ "$rc" -eq 0 ]; then
		record_pass "$label"
	else
		record_fail "$label" "$rc"
	fi
	return "$rc"
}

print_failures_for_category() {
	local category="$1"
	local i found=0

	for i in "${!FAILED_LABELS[@]}"; do
		if [[ "${FAILED_LABELS[$i]}" == "$category/"* ]]; then
			if [ "$found" -eq 0 ]; then
				echo ""
				echo "--- $category ---"
				found=1
			fi
			echo "  FAIL  ${FAILED_LABELS[$i]} (exit ${FAILED_EXITS[$i]})"
		fi
	done
}

print_summary() {
	local total=$((PASS_COUNT + FAIL_COUNT))

	echo ""
	echo "======== Summary ========"
	echo "Total: $total  Passed: $PASS_COUNT  Failed: $FAIL_COUNT"

	if [ "$FAIL_COUNT" -eq 0 ]; then
		echo ""
		echo "All Scan Media tests passed."
		return 0
	fi

	echo ""
	echo "Failed cases by category:"
	print_failures_for_category "mailbox"
	print_failures_for_category "sdb/vdm1"
	print_failures_for_category "sdb/i3c"
	return 1
}

# Build CAP_CMD / SCAN_CMD / RESULTS_CMD for an interface.
build_path_cmds() {
	local kind="$1"   # mailbox | sdb
	local port="${2:-}"

	if [ "$kind" = "mailbox" ]; then
		CAP_CMD=("$MBCCI" "$MEMDEV" get-scan-media-cap
			--dpa "$SCAN_DPA_HEX" --length "$SCAN_LENGTH")
		SCAN_CMD=("$MBCCI" "$MEMDEV" scan-media
			--dpa "$SCAN_DPA_HEX" --length "$SCAN_LENGTH" --no-evtlog)
		RESULTS_CMD=("$MBCCI" "$MEMDEV" get-scan-media-results)
	else
		CAP_CMD=("$MBCCI" "$MEMDEV" sdb-tunnel get-scan-media-cap
			--port "$port" --dpa "$SCAN_DPA_HEX" --length "$SCAN_LENGTH")
		SCAN_CMD=("$MBCCI" "$MEMDEV" sdb-tunnel scan-media
			--port "$port" --dpa "$SCAN_DPA_HEX" --length "$SCAN_LENGTH"
			--no-evtlog)
		RESULTS_CMD=("$MBCCI" "$MEMDEV" sdb-tunnel get-scan-media-results
			--port "$port")
	fi
}

# Cap -> scan -> sleep -> results for one interface. Continues on step failure.
run_scan_media_flow() {
	local prefix="$1"
	local desc="$2"

	echo ""
	echo "======== Phase: $desc ========"

	if ! run_cmd "$prefix/get-scan-media-cap" "${CAP_CMD[@]}"; then
		echo "    continuing $prefix after get-scan-media-cap failure" >&2
	fi

	if ! run_cmd "$prefix/scan-media" "${SCAN_CMD[@]}"; then
		echo "    skipping remaining $prefix steps (scan-media failed)" >&2
		return 0
	fi

	echo "==> [$prefix/sleep] waiting ${SCAN_SLEEP_SECS}s for background scan"
	sleep "$SCAN_SLEEP_SECS"
	echo "    done"

	run_cmd "$prefix/get-scan-media-results" "${RESULTS_CMD[@]}" || true
}

run_mailbox_phase() {
	build_path_cmds mailbox
	run_scan_media_flow "mailbox" "mailbox"
}

run_sdb_phase() {
	local port="$1"
	build_path_cmds sdb "$port"
	run_scan_media_flow "sdb/$port" "sdb-tunnel --port $port"
}

parse_args() {
	MEMDEV="mem0"

	while [ $# -gt 0 ]; do
		case "$1" in
		-h|--help)
			usage
			exit 0
			;;
		--dpa)
			[ $# -ge 2 ] || die "--dpa requires a value"
			SCAN_DPA="$(parse_u64 "--dpa" "$2")"
			shift 2
			;;
		--length)
			[ $# -ge 2 ] || die "--length requires a value"
			SCAN_LENGTH="$(parse_u64 "--length" "$2")"
			shift 2
			;;
		mem*)
			MEMDEV="$1"
			shift
			;;
		*)
			die "unknown argument: $1 (see --help)"
			;;
		esac
	done
}

main() {
	parse_args "$@"

	SCAN_DPA_HEX="$(format_hex "$SCAN_DPA")"

	MBCCI="$(resolve_mbcci_sfx)"
	[ -e "/dev/cxl/$MEMDEV" ] || die "/dev/cxl/$MEMDEV not found"

	echo "mbcci-sfx Scan Media tests"
	echo "  device:  $MEMDEV"
	echo "  binary:  $MBCCI"
	echo "  DPA:     $SCAN_DPA -> $SCAN_DPA_HEX"
	echo "  length:  $SCAN_LENGTH"
	echo "  sleep:   ${SCAN_SLEEP_SECS}s after scan-media"
	echo "  note:    mailbox Scan Media RAW may require driver allow-list"

	run_mailbox_phase
	run_sdb_phase vdm1
	run_sdb_phase i3c

	print_summary
}

main "$@"
exit $?
