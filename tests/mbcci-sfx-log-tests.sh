#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Exercise mbcci-sfx LOGS commands on a real CXL device:
#   1. mailbox (direct ioctl)
#   2. sdb-tunnel --port vdm1
#   3. sdb-tunnel --port i3c
#
# For each interface:
#   - get-supported-logs
#   - for each advertised UUID:
#       * get-log
#       * get-log-cap
#       * clear-log (if supported)
#       * populate-log (if supported)
#
# Exits non-zero if any test failed; runs all tests regardless.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
FAILED_LABELS=()
FAILED_EXITS=()
SKIPPED_LABELS=()

usage() {
	cat <<EOF
Usage: $0 [memN]

Run mbcci-sfx LOGS smoke tests against a CXL memory device.
For each interface (mailbox, sdb-tunnel vdm1, sdb-tunnel i3c), the script:
  1. gets supported logs
  2. tests get-log for every advertised UUID
  3. tests get-log-cap for every advertised UUID
  4. conditionally tests clear-log / populate-log when capability bits say so

Arguments:
  memN          CXL device name (default: mem0), e.g. mem0, mem1

Environment:
  MBCCI_SFX     Path to mbcci-sfx binary (auto-detected if unset)
  VERIFICATION_LOG_DIR
                Optional directory to store per-command logs

Examples:
  $0
  $0 mem1
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

sanitize_label() {
	local label="$1"
	label="${label//\//_}"
	label="${label//:/_}"
	echo "$label"
}

run_test() {
	local label="$1"
	shift
	local rc=0
	local logfile=""

	echo "==> [$label] $*"
	if [ -n "${VERIFICATION_LOG_DIR:-}" ]; then
		mkdir -p "$VERIFICATION_LOG_DIR"
		logfile="$VERIFICATION_LOG_DIR/$(sanitize_label "$label").log"
		if "$@" >"$logfile" 2>&1; then
			cat "$logfile"
			echo "    PASS"
			PASS_COUNT=$((PASS_COUNT + 1))
		else
			rc=$?
			cat "$logfile" >&2
			echo "    FAIL (exit $rc)" >&2
			FAILED_LABELS+=("$label")
			FAILED_EXITS+=("$rc")
			FAIL_COUNT=$((FAIL_COUNT + 1))
		fi
	elif "$@"; then
		echo "    PASS"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		rc=$?
		echo "    FAIL (exit $rc)" >&2
		FAILED_LABELS+=("$label")
		FAILED_EXITS+=("$rc")
		FAIL_COUNT=$((FAIL_COUNT + 1))
	fi
}

record_skip() {
	local label="$1"
	local reason="$2"

	echo "==> [$label] SKIP: $reason"
	SKIPPED_LABELS+=("$label: $reason")
	SKIP_COUNT=$((SKIP_COUNT + 1))
}

print_failures_for_category() {
	local category="$1"
	local i found=0

	for i in "${!FAILED_LABELS[@]}"; do
		if [[ "${FAILED_LABELS[$i]}" == "$category/"* ]]; then
			if [ "$found" -eq 0 ]; then
				echo ""
				echo "--- $category failures ---"
				found=1
			fi
			echo "  FAIL  ${FAILED_LABELS[$i]} (exit ${FAILED_EXITS[$i]})"
		fi
	done
}

print_skips_for_category() {
	local category="$1"
	local entry found=0

	for entry in "${SKIPPED_LABELS[@]}"; do
		if [[ "$entry" == "$category/"* ]]; then
			if [ "$found" -eq 0 ]; then
				echo ""
				echo "--- $category skips ---"
				found=1
			fi
			echo "  SKIP  $entry"
		fi
	done
}

print_summary() {
	local total=$((PASS_COUNT + FAIL_COUNT + SKIP_COUNT))

	echo ""
	echo "======== Summary ========"
	echo "Total: $total  Passed: $PASS_COUNT  Failed: $FAIL_COUNT  Skipped: $SKIP_COUNT"

	if [ "$FAIL_COUNT" -eq 0 ]; then
		echo ""
		echo "All requested LOGS tests passed."
	else
		echo ""
		echo "Failed cases by category:"
		print_failures_for_category "mailbox"
		print_failures_for_category "sdb/vdm1"
		print_failures_for_category "sdb/i3c"
	fi

	if [ "$SKIP_COUNT" -gt 0 ]; then
		echo ""
		echo "Skipped cases by category:"
		print_skips_for_category "mailbox"
		print_skips_for_category "sdb/vdm1"
		print_skips_for_category "sdb/i3c"
	fi

	[ "$FAIL_COUNT" -eq 0 ]
}

collect_supported_log_uuids() {
	local outfile="$1"
	shift
	local tmp

	tmp="$(mktemp)"
	if ! "$@" >"$tmp" 2>&1; then
		cat "$tmp" >&2
		rm -f "$tmp"
		return 1
	fi

	cat "$tmp"
	awk '/UUID:/ { print $3 }' "$tmp" >"$outfile"
	rm -f "$tmp"
	return 0
}

query_log_capabilities() {
	local outfile="$1"
	shift
	"$@" >"$outfile" 2>&1
}

capability_enabled() {
	local file="$1"
	local key="$2"
	rg -q "^  ${key}: yes$" "$file"
}

run_uuid_matrix() {
	local prefix="$1"
	local supported_cmd_desc="$2"
	local get_supported_mode="$3"
	local cap_mode="$4"
	local get_log_mode="$5"
	local clear_mode="$6"
	local populate_mode="$7"
	local uuids_file cap_file uuid

	uuids_file="$(mktemp)"

	echo ""
	echo "== Supported logs via $supported_cmd_desc =="
	if ! collect_supported_log_uuids "$uuids_file" "$MBCCI" "$MEMDEV" ${get_supported_mode}; then
		echo "Failed to enumerate supported logs for $prefix" >&2
		FAILED_LABELS+=("$prefix/get-supported-logs")
		FAILED_EXITS+=("enum")
		FAIL_COUNT=$((FAIL_COUNT + 1))
		rm -f "$uuids_file"
		return
	fi

	if [ ! -s "$uuids_file" ]; then
		record_skip "$prefix/get-supported-logs" "no UUIDs reported"
		rm -f "$uuids_file"
		return
	fi

	while IFS= read -r uuid; do
		[ -n "$uuid" ] || continue

		run_test "$prefix/get-log/$uuid" \
			"$MBCCI" "$MEMDEV" ${get_log_mode} --uuid "$uuid"

		cap_file="$(mktemp)"
		echo "==> [$prefix/get-log-cap/$uuid] $MBCCI $MEMDEV ${cap_mode} --uuid $uuid"
		if query_log_capabilities "$cap_file" "$MBCCI" "$MEMDEV" ${cap_mode} --uuid "$uuid"; then
			cat "$cap_file"
			echo "    PASS"
			PASS_COUNT=$((PASS_COUNT + 1))

			if capability_enabled "$cap_file" "clear_log_supported"; then
				run_test "$prefix/clear-log/$uuid" \
					"$MBCCI" "$MEMDEV" ${clear_mode} --uuid "$uuid"
			else
				record_skip "$prefix/clear-log/$uuid" "clear_log_supported=no"
			fi

			if capability_enabled "$cap_file" "populate_log_supported"; then
				run_test "$prefix/populate-log/$uuid" \
					"$MBCCI" "$MEMDEV" ${populate_mode} --uuid "$uuid"
			else
				record_skip "$prefix/populate-log/$uuid" "populate_log_supported=no"
			fi
		else
			local rc=$?
			cat "$cap_file" >&2
			echo "    FAIL (exit $rc)" >&2
			FAILED_LABELS+=("$prefix/get-log-cap/$uuid")
			FAILED_EXITS+=("$rc")
			FAIL_COUNT=$((FAIL_COUNT + 1))
			record_skip "$prefix/clear-log/$uuid" "get-log-cap failed"
			record_skip "$prefix/populate-log/$uuid" "get-log-cap failed"
		fi
		rm -f "$cap_file"
	done <"$uuids_file"

	rm -f "$uuids_file"
}

run_mailbox_phase() {
	echo ""
	echo "======== Phase 1: mailbox ========"
	run_uuid_matrix \
		"mailbox" \
		"mailbox get-supported-logs" \
		"get-supported-logs" \
		"get-log-cap" \
		"get-log" \
		"clear-log" \
		"populate-log"
}

run_sdb_phase() {
	local port="$1"

	echo ""
	echo "======== Phase 2: sdb-tunnel --port $port ========"
	run_uuid_matrix \
		"sdb/$port" \
		"sdb-tunnel get-supported-logs --port $port" \
		"sdb-tunnel get-supported-logs --port $port" \
		"sdb-tunnel get-log-cap --port $port" \
		"sdb-tunnel get-log --port $port" \
		"sdb-tunnel clear-log --port $port" \
		"sdb-tunnel populate-log --port $port"
}

main() {
	local MEMDEV="${1:-mem0}"

	case "$MEMDEV" in
	-h|--help)
		usage
		exit 0
		;;
	esac

	MBCCI="$(resolve_mbcci_sfx)"

	[ -e "/dev/cxl/$MEMDEV" ] || die "/dev/cxl/$MEMDEV not found"

	echo "mbcci-sfx LOGS test matrix"
	echo "  device: $MEMDEV"
	echo "  binary: $MBCCI"

	run_mailbox_phase
	run_sdb_phase vdm1
	run_sdb_phase i3c

	print_summary
}

main "$@"
exit $?
