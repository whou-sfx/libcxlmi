#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Exercise mailbox Media Operations (4402h) on a real CXL device:
#   discovery -> ranged sanitize -> ranged write zero
#
# WARNING: sanitize and zero modify media in the selected DPA range.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

MEMDEV="mem0"
DPA_RANGE="0:268435456"
START_INDEX=0
NUM_OPS=5
WAIT_SECONDS=5

PASS_COUNT=0
FAIL_COUNT=0
FAILED_LABELS=()
FAILED_EXITS=()

usage() {
	cat <<EOF
Usage: $0 [memN] [options]

Run mailbox Media Operations discovery, sanitize, and zero tests.

WARNING: sanitize and zero are destructive within the selected DPA range.

Options:
  --dpa-range <start>:<length>  DPA range used by sanitize and zero
                                (default: 0:268435456)
  --start-index <n>             Discovery start index (default: 0)
  --num-ops <n>                 Discovery requested operations (default: 5)
  --wait-seconds <n>            Wait between destructive operations
                                (default: 5)
  -h, --help                    Show this help

Arguments:
  memN                          CXL device name (default: mem0)

Environment:
  MBCCI_SFX              Path to mbcci-sfx binary (auto-detected if unset)
  VERIFICATION_LOG_DIR   Optional directory for per-command logs

Examples:
  sudo $0
  sudo $0 mem1 --dpa-range 0x10000000:0x10000000
  sudo $0 --dpa-range 0:536870912 --num-ops 16
EOF
}

die() {
	echo "ERROR: $*" >&2
	exit 1
}

parse_uint() {
	local name="$1"
	local raw="$2"

	[[ "$raw" =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ]] ||
		die "$name must be a non-negative decimal or hexadecimal integer: '$raw'"
	printf '%u' "$((raw))"
}

validate_dpa_range() {
	local raw="$1"
	local start length

	[[ "$raw" == *:* ]] ||
		die "--dpa-range must use <start>:<length>: '$raw'"
	start="${raw%%:*}"
	length="${raw#*:}"
	[ -n "$start" ] && [ -n "$length" ] ||
		die "--dpa-range must use <start>:<length>: '$raw'"
	[ "$length" != "$raw" ] && [[ "$length" != *:* ]] ||
		die "--dpa-range must contain exactly one colon: '$raw'"

	parse_uint "DPA range start" "$start" >/dev/null
	parse_uint "DPA range length" "$length" >/dev/null
	[ "$((length))" -gt 0 ] ||
		die "DPA range length must be greater than zero: '$length'"
}

resolve_mbcci_sfx() {
	if [ -n "${MBCCI_SFX:-}" ]; then
		[ -x "$MBCCI_SFX" ] ||
			die "MBCCI_SFX is not executable: $MBCCI_SFX"
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

	die "mbcci-sfx not found. Set MBCCI_SFX or build it first."
}

sanitize_label() {
	local label="$1"
	label="${label//\//_}"
	label="${label//:/_}"
	echo "$label"
}

run_cmd() {
	local label="$1"
	shift
	local rc=0
	local output=""
	local logfile=""

	echo "==> [$label] $*"
	if output="$("$@" 2>&1)"; then
		rc=0
	else
		rc=$?
	fi

	printf '%s\n' "$output"
	if [ -n "${VERIFICATION_LOG_DIR:-}" ]; then
		mkdir -p "$VERIFICATION_LOG_DIR"
		logfile="$VERIFICATION_LOG_DIR/$(sanitize_label "$label").log"
		printf '%s\n' "$output" >"$logfile"
	fi

	if [ "$rc" -eq 0 ]; then
		echo "    PASS"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		echo "    FAIL (exit $rc)" >&2
		FAILED_LABELS+=("$label")
		FAILED_EXITS+=("$rc")
		FAIL_COUNT=$((FAIL_COUNT + 1))
	fi
	return "$rc"
}

parse_args() {
	while [ $# -gt 0 ]; do
		case "$1" in
		-h|--help)
			usage
			exit 0
			;;
		--dpa-range)
			[ $# -ge 2 ] || die "--dpa-range requires a value"
			DPA_RANGE="$2"
			shift 2
			;;
		--start-index)
			[ $# -ge 2 ] || die "--start-index requires a value"
			START_INDEX="$(parse_uint "--start-index" "$2")"
			shift 2
			;;
		--num-ops)
			[ $# -ge 2 ] || die "--num-ops requires a value"
			NUM_OPS="$(parse_uint "--num-ops" "$2")"
			[ "$NUM_OPS" -gt 0 ] || die "--num-ops must be greater than zero"
			shift 2
			;;
		--wait-seconds)
			[ $# -ge 2 ] || die "--wait-seconds requires a value"
			WAIT_SECONDS="$(parse_uint "--wait-seconds" "$2")"
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

print_summary() {
	local total=$((PASS_COUNT + FAIL_COUNT))
	local i

	echo ""
	echo "======== Summary ========"
	echo "Total: $total  Passed: $PASS_COUNT  Failed: $FAIL_COUNT"
	for i in "${!FAILED_LABELS[@]}"; do
		echo "  FAIL  ${FAILED_LABELS[$i]} (exit ${FAILED_EXITS[$i]})"
	done

	[ "$FAIL_COUNT" -eq 0 ]
}

main() {
	local mbcci
	local -a prefix

	parse_args "$@"
	validate_dpa_range "$DPA_RANGE"

	mbcci="$(resolve_mbcci_sfx)"
	[ -e "/dev/cxl/$MEMDEV" ] || die "/dev/cxl/$MEMDEV not found"

	prefix=()
	if [ "$EUID" -ne 0 ]; then
		command -v sudo >/dev/null 2>&1 ||
			die "root privileges are required and sudo was not found"
		prefix=(sudo)
	fi

	echo "mbcci-sfx Media Operations mailbox tests"
	echo "  device:       $MEMDEV"
	echo "  binary:       $mbcci"
	echo "  DPA range:    $DPA_RANGE"
	echo "  start index:  $START_INDEX"
	echo "  num ops:      $NUM_OPS"
	echo "  wait:         ${WAIT_SECONDS}s"
	echo "  WARNING: sanitize and zero modify the selected DPA range"

	run_cmd "discovery" "${prefix[@]}" "$mbcci" "$MEMDEV" \
		media-operation discovery \
		--start-index "$START_INDEX" --num-ops "$NUM_OPS" || true

	if run_cmd "sanitize" "${prefix[@]}" "$mbcci" "$MEMDEV" \
		media-operation sanitize --operation sanitize \
		--dpa-range "$DPA_RANGE"; then
		echo "==> waiting ${WAIT_SECONDS}s after sanitize"
		sleep "$WAIT_SECONDS"
	fi

	if run_cmd "zero" "${prefix[@]}" "$mbcci" "$MEMDEV" \
		media-operation sanitize --operation zero \
		--dpa-range "$DPA_RANGE"; then
		echo "==> waiting ${WAIT_SECONDS}s after zero"
		sleep "$WAIT_SECONDS"
	fi

	print_summary
}

main "$@"
