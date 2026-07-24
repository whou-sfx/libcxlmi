#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Exercise mbcci-sfx Poison lifecycle on a real CXL device:
#   inject-poison -> get-poison-list (assert Injected) -> clear-poison
#   -> get-poison-list (assert Injected gone)
#
# Covers:
#   1. mailbox (direct ioctl)
#   2. sdb-tunnel --port vdm1
#   3. sdb-tunnel --port i3c
#
# Requires a safe, 64B-aligned DPA via POISON_DPA.
# Mailbox Poison RAW opcodes may need driver allow-list / raw_allow_all.
#
# Exits non-zero if any test failed; continues across interfaces.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PASS_COUNT=0
FAIL_COUNT=0
FAILED_LABELS=()
FAILED_EXITS=()

# Cleanup state for trap: set when inject succeeds on a path.
CLEANUP_NEEDED=0
CLEANUP_PREFIX=""
CLEANUP_CMD=()

POISON_LENGTH=64

usage() {
	cat <<EOF
Usage: POISON_DPA=<addr> $0 [memN]

Run mbcci-sfx Poison lifecycle tests against a CXL memory device.
For each interface (mailbox, sdb-tunnel vdm1, sdb-tunnel i3c):
  1. get-poison-list --frestart
  2. inject-poison
  3. get-poison-list and assert DPA + ErrorSource=Injected (0x3)
  4. clear-poison
  5. get-poison-list and assert Injected record for that DPA is gone

Arguments:
  memN          CXL device name (default: mem0), e.g. mem0, mem1

Environment:
  POISON_DPA             Required. Safe 64B-aligned DPA (decimal or 0x hex).
  MBCCI_SFX              Path to mbcci-sfx binary (auto-detected if unset)
  VERIFICATION_LOG_DIR   Optional directory to store per-command logs

Notes:
  - Only use a DPA confirmed safe for poison inject/clear on your device.
  - Mailbox path uses RAW opcodes 4300h-4302h; the running CXL driver may
    reject them unless raw commands are allowed (e.g. raw_allow_all).

Examples:
  POISON_DPA=0x1000 $0
  POISON_DPA=0x1000 $0 mem1
  POISON_DPA=4096 MBCCI_SFX=./build/tools/mbcci-sfx/mbcci-sfx $0
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

# Parse POISON_DPA (decimal or 0x hex) to a decimal integer string.
parse_poison_dpa() {
	local raw="$1"
	local val

	if [[ ! "$raw" =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ]]; then
		die "POISON_DPA must be a non-negative integer (decimal or 0x hex): '$raw'"
	fi

	# Force bash arithmetic (handles 0x)
	val=$((raw))
	if [ "$val" -lt 0 ]; then
		die "POISON_DPA must be non-negative: $raw"
	fi
	if (( val % 64 != 0 )); then
		die "POISON_DPA must be 64-byte aligned (got $raw = $val)"
	fi

	printf '%d' "$val"
}

# Format decimal DPA as 0x%016x for matching tool output.
format_dpa_hex() {
	local val="$1"
	printf '0x%016x' "$val"
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

# Run a command; record PASS/FAIL. Returns command exit status.
# Optionally captures stdout+stderr into CAPTURE_OUT (caller-provided via nameref-like global).
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

	LAST_CMD_OUT="$out"
	if [ "$rc" -eq 0 ]; then
		record_pass "$label"
	else
		record_fail "$label" "$rc"
	fi
	return "$rc"
}

# Assert that get-poison-list output contains Injected record for DPA_HEX.
assert_injected_present() {
	local label="$1"
	local output="$2"
	local dpa_hex="$3"
	local line

	echo "==> [$label] assert Injected present for $dpa_hex"
	# Example:
	#   [0] DPA=0x0000000000001000 ErrorSource=Injected (0x3) Length=0x00000001
	if line="$(printf '%s\n' "$output" | grep -E "DPA=${dpa_hex} ErrorSource=Injected \(0x3\)")"; then
		echo "    matched: $line"
		record_pass "$label"
		return 0
	fi

	echo "    FAIL: no Injected (0x3) record for $dpa_hex" >&2
	echo "    output was:" >&2
	printf '%s\n' "$output" >&2
	record_fail "$label" "assert"
	return 1
}

# Assert that get-poison-list output has no Injected record for DPA_HEX.
assert_injected_absent() {
	local label="$1"
	local output="$2"
	local dpa_hex="$3"
	local line

	echo "==> [$label] assert Injected absent for $dpa_hex"
	if line="$(printf '%s\n' "$output" | grep -E "DPA=${dpa_hex} ErrorSource=Injected \(0x3\)" || true)"; then
		if [ -n "$line" ]; then
			echo "    FAIL: still have Injected record: $line" >&2
			record_fail "$label" "assert"
			return 1
		fi
	fi
	record_pass "$label"
	return 0
}

clear_cleanup_state() {
	CLEANUP_NEEDED=0
	CLEANUP_PREFIX=""
	CLEANUP_CMD=()
}

set_cleanup() {
	CLEANUP_NEEDED=1
	CLEANUP_PREFIX="$1"
	shift
	CLEANUP_CMD=("$@")
}

# Best-effort clear if inject succeeded but later steps failed / script exits.
trap_cleanup() {
	if [ "$CLEANUP_NEEDED" -eq 0 ]; then
		return 0
	fi
	echo ""
	echo "=== trap cleanup: clear-poison on $CLEANUP_PREFIX ===" >&2
	"${CLEANUP_CMD[@]}" >/dev/null 2>&1 || \
		echo "WARNING: cleanup clear-poison failed on $CLEANUP_PREFIX" >&2
	clear_cleanup_state
}
trap trap_cleanup EXIT

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
		echo "All Poison tests passed."
		return 0
	fi

	echo ""
	echo "Failed cases by category:"
	print_failures_for_category "mailbox"
	print_failures_for_category "sdb/vdm1"
	print_failures_for_category "sdb/i3c"
	return 1
}

# Build command arrays for an interface.
# Sets GET_CMD / INJECT_CMD / CLEAR_CMD as bash arrays.
build_path_cmds() {
	local kind="$1"   # mailbox | sdb
	local port="${2:-}"

	if [ "$kind" = "mailbox" ]; then
		GET_CMD=("$MBCCI" "$MEMDEV" get-poison-list
			--dpa "$POISON_DPA_HEX" --length "$POISON_LENGTH" --frestart)
		INJECT_CMD=("$MBCCI" "$MEMDEV" inject-poison --dpa "$POISON_DPA_HEX")
		CLEAR_CMD=("$MBCCI" "$MEMDEV" clear-poison --dpa "$POISON_DPA_HEX")
	else
		GET_CMD=("$MBCCI" "$MEMDEV" sdb-tunnel get-poison-list
			--port "$port" --dpa "$POISON_DPA_HEX" --length "$POISON_LENGTH" --frestart)
		INJECT_CMD=("$MBCCI" "$MEMDEV" sdb-tunnel inject-poison
			--port "$port" --dpa "$POISON_DPA_HEX")
		CLEAR_CMD=("$MBCCI" "$MEMDEV" sdb-tunnel clear-poison
			--port "$port" --dpa "$POISON_DPA_HEX")
	fi
}

# Full lifecycle for one interface. Continues on step failure within the path.
run_poison_lifecycle() {
	local prefix="$1"
	local desc="$2"
	shift 2
	# Remaining: unused — cmds come from GET_CMD / INJECT_CMD / CLEAR_CMD

	echo ""
	echo "======== Phase: $desc ========"

	clear_cleanup_state

	# 1) baseline get
	if ! run_cmd "$prefix/get-poison-list/baseline" "${GET_CMD[@]}"; then
		echo "    skipping remaining $prefix steps (baseline get failed)" >&2
		return 0
	fi

	# 2) inject
	if ! run_cmd "$prefix/inject-poison" "${INJECT_CMD[@]}"; then
		echo "    skipping remaining $prefix steps (inject failed)" >&2
		return 0
	fi
	set_cleanup "$prefix" "${CLEAR_CMD[@]}"

	# 3) get + assert Injected present
	if run_cmd "$prefix/get-poison-list/after-inject" "${GET_CMD[@]}"; then
		assert_injected_present \
			"$prefix/assert/injected-present" \
			"$LAST_CMD_OUT" \
			"$POISON_DPA_HEX" || true
	else
		echo "    skipping inject-present assert (get after inject failed)" >&2
	fi

	# 4) clear
	if ! run_cmd "$prefix/clear-poison" "${CLEAR_CMD[@]}"; then
		echo "    skipping post-clear checks (clear failed); trap will retry clear" >&2
		return 0
	fi
	# Clear succeeded — no longer need trap cleanup for this path
	clear_cleanup_state

	# 5) get + assert Injected absent
	if run_cmd "$prefix/get-poison-list/after-clear" "${GET_CMD[@]}"; then
		assert_injected_absent \
			"$prefix/assert/injected-absent" \
			"$LAST_CMD_OUT" \
			"$POISON_DPA_HEX" || true
	else
		echo "    skipping inject-absent assert (get after clear failed)" >&2
	fi
}

run_mailbox_phase() {
	build_path_cmds mailbox
	run_poison_lifecycle "mailbox" "mailbox"
}

run_sdb_phase() {
	local port="$1"
	build_path_cmds sdb "$port"
	run_poison_lifecycle "sdb/$port" "sdb-tunnel --port $port"
}

main() {
	local MEMDEV="${1:-mem0}"
	local dpa_dec

	case "$MEMDEV" in
	-h|--help)
		usage
		exit 0
		;;
	esac

	[ -n "${POISON_DPA:-}" ] || die "POISON_DPA is required (safe 64B-aligned DPA). See --help."

	dpa_dec="$(parse_poison_dpa "$POISON_DPA")"
	POISON_DPA_HEX="$(format_dpa_hex "$dpa_dec")"

	MBCCI="$(resolve_mbcci_sfx)"
	[ -e "/dev/cxl/$MEMDEV" ] || die "/dev/cxl/$MEMDEV not found"

	echo "mbcci-sfx Poison lifecycle tests"
	echo "  device:  $MEMDEV"
	echo "  binary:  $MBCCI"
	echo "  DPA:     $POISON_DPA -> $POISON_DPA_HEX"
	echo "  length:  $POISON_LENGTH"
	echo "  note:    mailbox Poison RAW may require driver allow-list"

	LAST_CMD_OUT=""

	run_mailbox_phase
	run_sdb_phase vdm1
	run_sdb_phase i3c

	print_summary
}

main "$@"
exit $?
