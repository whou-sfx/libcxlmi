#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Smoke-test all mbcci-sfx Get/read commands on a real CXL device:
#   1. mailbox (direct ioctl)
#   2. sdb-tunnel --port vdm1
#   3. sdb-tunnel --port i3c
#
# Exits with status 1 if any test failed; runs all tests regardless.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

EVENT_LOGS=(info warn failure fatal dcd)
GET_LOG_SAMPLE_LEN=64
# Command Effects Log (CEL) — see tools/mbcci-sfx/cmd_cel.c
CEL_LOG_UUID="0da9c0b5-bf41-4b78-8f79-96b1623b3f17"

PASS_COUNT=0
FAIL_COUNT=0
FAILED_LABELS=()
FAILED_EXITS=()

usage() {
	cat <<EOF
Usage: $0 [memN]

Run all mbcci-sfx Get/read commands against a CXL memory device.
Runs every test even when some fail; prints a categorized summary at the end.

Arguments:
  memN          CXL device name (default: mem0), e.g. mem0, mem1

Environment:
  MBCCI_SFX     Path to mbcci-sfx binary (auto-detected if unset)

Prerequisites:
  - mbcci-sfx built (meson compile -C build tools/mbcci-sfx)
  - /dev/cxl/<memN> accessible (root or cxl group)
  - get-vendor-log is intentionally skipped (full log fetch)

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

	if command -v mbcci-sfx &>/dev/null; then
		command -v mbcci-sfx
		return
	fi

	die "mbcci-sfx not found. Set MBCCI_SFX or build with: meson compile -C build tools/mbcci-sfx"
}

run_test() {
	local label="$1"
	shift
	local rc=0

	echo "==> [$label] $*"
	if "$@"; then
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
		echo "All Get/read tests passed."
		return 0
	fi

	echo ""
	echo "Failed commands by category:"
	print_failures_for_category "mailbox"
	print_failures_for_category "sdb/vdm1"
	print_failures_for_category "sdb/i3c"

	return 1
}

run_event_log_tests() {
	local prefix="$1"
	shift
	local log

	for log in "${EVENT_LOGS[@]}"; do
		run_test "$prefix/get-event-records/$log" "$@" --log "$log"
	done
}

run_mailbox_get_logs() {
	run_test "mailbox/get-supported-logs" \
		"$MBCCI" "$MEMDEV" get-supported-logs
	run_test "mailbox/get-log/cel" \
		"$MBCCI" "$MEMDEV" get-log --uuid "$CEL_LOG_UUID" --length "$GET_LOG_SAMPLE_LEN"
}

run_mailbox_phase() {
	echo ""
	echo "======== Phase 1: mailbox ========"

	run_test "mailbox/identify_memdev" \
		"$MBCCI" "$MEMDEV" identify_memdev
	run_test "mailbox/get-partition" \
		"$MBCCI" "$MEMDEV" get-partition
	run_test "mailbox/get-fw-info" \
		"$MBCCI" "$MEMDEV" get-fw-info
	run_test "mailbox/get-health-info" \
		"$MBCCI" "$MEMDEV" get-health-info
	run_test "mailbox/get-alert-config" \
		"$MBCCI" "$MEMDEV" get-alert-config
	run_test "mailbox/get-sld-qos-ctrl" \
		"$MBCCI" "$MEMDEV" get-sld-qos-ctrl
	run_test "mailbox/get-event-interrupt-policy" \
		"$MBCCI" "$MEMDEV" get-event-interrupt-policy
	run_test "mailbox/get-supported-feat" \
		"$MBCCI" "$MEMDEV" get-supported-feat
	run_test "mailbox/get-timestamp" \
		"$MBCCI" "$MEMDEV" get-timestamp

	run_event_log_tests "mailbox" "$MBCCI" "$MEMDEV" get-event-records
	run_mailbox_get_logs
}

run_sdb_get_logs() {
	local port="$1"

	run_test "sdb/$port/get-supported-logs" \
		"$MBCCI" "$MEMDEV" sdb-tunnel get-supported-logs --port "$port"
	run_test "sdb/$port/get-log/cel" \
		"$MBCCI" "$MEMDEV" sdb-tunnel get-log --port "$port" \
		--uuid "$CEL_LOG_UUID" --length "$GET_LOG_SAMPLE_LEN"
}

run_sdb_phase() {
	local port="$1"

	echo ""
	echo "======== Phase: sdb-tunnel --port $port ========"

	run_test "sdb/$port/identify" \
		"$MBCCI" "$MEMDEV" sdb-tunnel identify --port "$port"
	run_test "sdb/$port/identify_memdev" \
		"$MBCCI" "$MEMDEV" sdb-tunnel identify_memdev --port "$port"
	run_test "sdb/$port/get-partition" \
		"$MBCCI" "$MEMDEV" sdb-tunnel get-partition --port "$port"
	run_test "sdb/$port/get-fw-info" \
		"$MBCCI" "$MEMDEV" sdb-tunnel get-fw-info --port "$port"
	run_test "sdb/$port/get-health-info" \
		"$MBCCI" "$MEMDEV" sdb-tunnel get-health-info --port "$port"
	run_test "sdb/$port/get-alert-config" \
		"$MBCCI" "$MEMDEV" sdb-tunnel get-alert-config --port "$port"
	run_test "sdb/$port/get-sld-qos-ctrl" \
		"$MBCCI" "$MEMDEV" sdb-tunnel get-sld-qos-ctrl --port "$port"
	run_test "sdb/$port/fm-get-ld-info" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-get-ld-info --port "$port"
	run_test "sdb/$port/fm-get-ld-alloc" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-get-ld-alloc --port "$port"
	run_test "sdb/$port/get-resp-msg-limit" \
		"$MBCCI" "$MEMDEV" sdb-tunnel get-resp-msg-limit --port "$port"
	run_test "sdb/$port/get-mctp-evt-int-policy" \
		"$MBCCI" "$MEMDEV" sdb-tunnel get-mctp-evt-int-policy --port "$port"
	run_test "sdb/$port/get-supported-feat" \
		"$MBCCI" "$MEMDEV" sdb-tunnel get-supported-feat --port "$port"
	run_test "sdb/$port/get-timestamp" \
		"$MBCCI" "$MEMDEV" sdb-tunnel get-timestamp --port "$port"

	run_event_log_tests "sdb/$port" \
		"$MBCCI" "$MEMDEV" sdb-tunnel get-event-records --port "$port"
	run_sdb_get_logs "$port"
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

	echo "mbcci-sfx Get/Read smoke tests"
	echo "  device:  $MEMDEV"
	echo "  binary:  $MBCCI"
	echo "  skipped: get-vendor-log"

	run_mailbox_phase
	run_sdb_phase vdm1
	run_sdb_phase i3c

	print_summary
}

main "$@"
exit $?
