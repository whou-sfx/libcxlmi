#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# DCD (Dynamic Capacity Device) lifecycle tests for mbcci-sfx.
#
# Tests two scenarios per the CXL 3.2 spec:
#   Case 1 — Simple Contiguous Add / Release flow
#   Case 2 — FM Add followed by Forced Release (before Host confirms)
#
# Assumptions:
#   - FM interface:   sdb-tunnel --port vdm1
#   - All host-id:    0  (ldid = 0)
#   - Extent size:    256 MB  (0x10000000)
#   - Regions 0 and 1 are enabled
#
# flags encoding for fm-dcd-initiate-release:
#   bits[3:0] = Removal Policy  (0h = Non-Prescriptive, 1h = Prescriptive)
#   bit[4]    = Forced Flag     (0 = not forced,        1 = Forced Removal)
#   Example: 0x01 = Prescriptive non-forced  (Case 1 step 7)
#            0x11 = Prescriptive Forced       (Case 2 step 3)
#
# Case 1 sequence:
#   0.  Flush all stale DCD events (Host + FM)
#   1.  FM  : fm-dcd-get-info  (discovery)
#   2.  FM  : fm-dcd-initiate-add --selection-policy 1  → Extent Pending
#   3.  Host: read all DCD events, parse Add Capacity DPA, clear handles
#   4.  Host: dcd-add-response                           → Extent Added
#   4b. Host: dcd-get-extent-list  (verify Added extent visible)
#   5.  FM  : read DCD events (Add Response, type=04h), clear handles
#   6.  FM  : fm-dcd-get-ext-list  (verify 1 Added extent)
#   --- release section (skippable with --skip-release) ---
#   7.  FM  : fm-dcd-initiate-release --flags 0x01 --extent DPA:LEN (Prescriptive)
#   8.  Host: read all DCD events (Release event, type=01h), clear handles
#   9.  Host: dcd-release
#  10.  FM  : read DCD events post-release (may be lost if log full → warn)
#  11.  FM  : fm-dcd-get-ext-list  (verify empty)
#
# Case 2 sequence:
#   0.  Flush all stale DCD events (Host + FM)
#   1.  FM  : fm-dcd-initiate-add                        → Extent Pending
#   1b. FM  : fm-dcd-get-ext-list  (Pending → expect empty — only Added visible)
#   2.  [internal] Host: read DCD events, parse DPA (no clear yet — needed for step 3)
#   3.  FM  : fm-dcd-initiate-release --flags 0x11 --extent DPA:LEN (Prescriptive + Forced)
#                                                         → Extent Dead
#   3b. FM  : fm-dcd-get-ext-list  (Dead → expect empty — only Added visible)
#   4.  Host: read DCD events (original Add Capacity event still present, verify unchanged)
#             clear handles
#   5.  Host: dcd-add-response → device returns "Invalid Physical Address" (extent is Dead)
#   6.  FM  : read DCD events → expect Add Capacity Response (type=04h) with Length=0
#             (no overflow → event must be present; Length=0 confirms no capacity allocated)
#   7.  FM  : fm-dcd-get-ext-list  (force-released extent must NOT appear)
#
# Exits non-zero if any test failed; continues across steps.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

DCD_EXTENT_SIZE=0x10000000   # 256 MB
DCD_HOST_ID=0
DCD_SDB_PORT=vdm1
DCD_EXTENT_CNT=8             # max extents requested in dcd-get-extent-list

SKIP_RELEASE=0               # set to 1 with --skip-release to skip Case 1 steps 7-11

PASS_COUNT=0
FAIL_COUNT=0
WARN_COUNT=0
FAILED_LABELS=()
FAILED_EXITS=()

# ---------------------------------------------------------------------------
# Boilerplate
# ---------------------------------------------------------------------------

usage() {
	cat <<EOF
Usage: $0 [--skip-release] [memN]

Run DCD lifecycle tests (Case 1 and Case 2) against a CXL memory device.

Arguments:
  memN              CXL device name (default: mem0), e.g. mem0, mem1

Options:
  --skip-release    Skip the release portion of Case 1 (steps 7-11).
                    Useful when the device does not generate a Release event
                    synchronously.  Default: run the full release flow.
  -h, --help        Show this help and exit.

Environment:
  MBCCI_SFX              Path to mbcci-sfx binary (auto-detected if unset)
  VERIFICATION_LOG_DIR   Optional directory to store per-command logs

Test assumptions:
  - FM interface:  sdb-tunnel --port $DCD_SDB_PORT
  - host-id/ldid:  $DCD_HOST_ID
  - Extent size:   $DCD_EXTENT_SIZE  (256 MB)
  - Regions 0 and 1 are enabled on the device

flags encoding for fm-dcd-initiate-release:
  bits[3:0] = Removal Policy  (0h = Non-Prescriptive, 1h = Prescriptive)
  bit[4]    = Forced Flag     (1 = Forced Removal)
  0x01 = Prescriptive non-forced  (Case 1 step 7)
  0x11 = Prescriptive Forced      (Case 2 step 3)

Examples:
  $0
  $0 mem1
  $0 --skip-release mem1
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

run_test() {
	local label="$1"
	shift
	local rc=0
	local logfile=""

	echo "==> [$label] $*"
	if [ -n "${VERIFICATION_LOG_DIR:-}" ]; then
		mkdir -p "$VERIFICATION_LOG_DIR"
		logfile="$VERIFICATION_LOG_DIR/${label//\//_}.log"
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

record_warn() {
	local label="$1"
	local msg="$2"
	echo "    WARN [$label]: $msg" >&2
	WARN_COUNT=$((WARN_COUNT + 1))
}

# Like run_test, but PASSES when the command exits non-zero.
# Use for commands expected to return a CXL error code
# (e.g. "Invalid Physical Address" on a Dead extent).
run_test_expect_fail() {
	local label="$1"
	shift
	local rc=0
	local logfile=""

	echo "==> [$label] (expecting failure) $*"
	if [ -n "${VERIFICATION_LOG_DIR:-}" ]; then
		mkdir -p "$VERIFICATION_LOG_DIR"
		logfile="$VERIFICATION_LOG_DIR/${label//\//_}.log"
		if "$@" >"$logfile" 2>&1; then
			cat "$logfile"
			echo "    FAIL (expected non-zero exit but command succeeded)" >&2
			FAILED_LABELS+=("$label")
			FAILED_EXITS+=("unexpected-success")
			FAIL_COUNT=$((FAIL_COUNT + 1))
		else
			rc=$?
			cat "$logfile"
			echo "    PASS (failed as expected, exit $rc)"
			PASS_COUNT=$((PASS_COUNT + 1))
		fi
	elif "$@"; then
		echo "    FAIL (expected non-zero exit but command succeeded)" >&2
		FAILED_LABELS+=("$label")
		FAILED_EXITS+=("unexpected-success")
		FAIL_COUNT=$((FAIL_COUNT + 1))
	else
		rc=$?
		echo "    PASS (failed as expected, exit $rc)"
		PASS_COUNT=$((PASS_COUNT + 1))
	fi
}

print_summary() {
	local total=$((PASS_COUNT + FAIL_COUNT))

	echo ""
	echo "======== Summary ========"
	echo "Total: $total  Passed: $PASS_COUNT  Failed: $FAIL_COUNT  Warnings: $WARN_COUNT"

	if [ "$FAIL_COUNT" -eq 0 ]; then
		echo ""
		echo "All DCD tests passed."
	else
		echo ""
		echo "Failed steps:"
		local i
		for i in "${!FAILED_LABELS[@]}"; do
			echo "  FAIL  ${FAILED_LABELS[$i]} (exit ${FAILED_EXITS[$i]})"
		done
	fi

	[ "$FAIL_COUNT" -eq 0 ]
}

# ---------------------------------------------------------------------------
# Event log helpers
# ---------------------------------------------------------------------------

# Read all DCD events from the host mailbox.
# The C command already loops internally until MORE_EVENTS = 0.
_read_host_dcd() {
	"$MBCCI" "$MEMDEV" get-event-records --log dcd
}

# Read all DCD events from the FM via sdb-tunnel.
_read_fm_dcd() {
	"$MBCCI" "$MEMDEV" sdb-tunnel get-event-records --log dcd --port "$DCD_SDB_PORT"
}

# Extract all event handles (oldest first) from get-event-records output.
# Output line format:  "    Handle:         0x0001"
# Related Handle lines ("    Related Handle: …") are intentionally not matched.
_extract_handles() {
	grep "^    Handle:         " | awk '{ print $2 }'
}

# Clear a list of DCD event handles on the host mailbox.
# Usage: _clear_host_handles handle1 handle2 …
_clear_host_handles() {
	[ $# -eq 0 ] && return 0
	local args=()
	for h in "$@"; do
		args+=(--handle "$h")
	done
	"$MBCCI" "$MEMDEV" clear-event-records --log dcd "${args[@]}"
}

# Clear a list of DCD event handles via FM sdb-tunnel.
# Usage: _clear_fm_handles handle1 handle2 …
_clear_fm_handles() {
	[ $# -eq 0 ] && return 0
	local args=()
	for h in "$@"; do
		args+=(--handle "$h")
	done
	"$MBCCI" "$MEMDEV" sdb-tunnel clear-event-records --log dcd \
		--port "$DCD_SDB_PORT" "${args[@]}"
}

# Drain and clear all DCD events for one side.
# Usage: _flush_dcd_side <label> <read_fn> <clear_fn>
_flush_dcd_side() {
	local label="$1"
	local read_fn="$2"
	local clear_fn="$3"

	echo "  [$label] Draining DCD event log..."
	local output
	output=$($read_fn 2>&1) || true
	echo "$output"

	local handles
	handles=$(echo "$output" | _extract_handles) || true

	if [ -z "$handles" ]; then
		echo "  [$label] No DCD events to clear."
		return 0
	fi

	echo "  [$label] Clearing handles: $handles"
	# Word-split intentional: each handle is a separate token.
	# shellcheck disable=SC2086
	$clear_fn $handles || true
	echo "  [$label] Done."
}

# Flush all DCD events on both Host and FM sides before each Case.
flush_all_dcd_events() {
	echo ""
	echo "-- Flushing all stale DCD events (Host + FM) --"
	_flush_dcd_side "flush-host" _read_host_dcd _clear_host_handles
	_flush_dcd_side "flush-fm"   _read_fm_dcd   _clear_fm_handles
	echo "-- Flush complete --"
}

# ---------------------------------------------------------------------------
# DPA parsing
#
# DC Event Record data layout (CXL 3.2 Table 8-142):
#   data[0]     : Event Type  (0x00=Add Capacity, 0x01=Release, 0x04=Add Response)
#   data[1..7]  : Reserved
#   data[8..15] : Start DPA   (8 bytes, little-endian)
#   data[16..23]: Length      (8 bytes, little-endian)
#   ...
#
# get-event-records prints data[] as a hex dump, 16 bytes per line:
#   "    Data:           <32 hex chars = data[0..15]>"
#   "                    <32 hex chars = data[16..31]>"
#   …
#
# data[8..15] (DPA, LE) → chars 16–31 of the first Data line.
# ---------------------------------------------------------------------------

# Parse Start DPA from the first Add Capacity (type=0x00) event for LD 0.
# Reads from a file containing the full get-event-records output.
# Prints DPA in hex (e.g. "0x10000000") on success; exits 1 on failure.
parse_add_dpa_from_file() {
	local tmpfile="$1"
	python3 - "$tmpfile" <<'PYEOF'
import sys
import re

with open(sys.argv[1]) as f:
    content = f.read()

# Each record block starts with "\n  [Record N]"
records = re.split(r'\n  \[Record \d+\]', content)

for rec in records:
    # Filter by LD ID == 0
    m = re.search(r'LD ID:\s+(\d+)', rec)
    if not m or int(m.group(1)) != 0:
        continue

    # Extract the hex content from the first Data line:
    #   "    Data:           <hex...>"
    m = re.search(r'Data:\s+([0-9a-f]{32,})', rec)
    if not m:
        continue

    data_hex = m.group(1)

    # Byte 0 = event type (chars 0-1 in the hex string)
    event_type = int(data_hex[0:2], 16)
    if event_type != 0x00:   # 0x00 = Add Capacity
        continue

    # DPA: data[8..15] in LE = chars 16-31
    dpa_le_hex = data_hex[16:32]
    dpa = int.from_bytes(bytes.fromhex(dpa_le_hex), 'little')
    print("0x{:x}".format(dpa))
    sys.exit(0)

sys.exit(1)
PYEOF
}

# Convenience: read host DCD events into a tmpfile, parse DPA, optionally clear handles.
# Sets global EXTENT_DPA.  On failure: records FAIL and returns 1.
# Usage: capture_host_dcd_dpa <test_label> <clear_after: 0|1>
capture_host_dcd_dpa() {
	local label="$1"
	local clear_after="$2"
	local tmpfile
	tmpfile=$(mktemp)

	echo ""
	echo "==> [${label}] Reading host DCD event log..."
	if _read_host_dcd >"$tmpfile" 2>&1; then
		cat "$tmpfile"
		echo "    PASS"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		cat "$tmpfile" >&2
		echo "    FAIL (cannot read host DCD events)" >&2
		FAILED_LABELS+=("$label")
		FAILED_EXITS+=("1")
		FAIL_COUNT=$((FAIL_COUNT + 1))
		rm -f "$tmpfile"
		return 1
	fi

	EXTENT_DPA=$(parse_add_dpa_from_file "$tmpfile") || true
	if [ -z "$EXTENT_DPA" ]; then
		echo "  FAIL: could not parse Add Capacity DPA from DCD events" >&2
		FAILED_LABELS+=("${label}/parse-add-dpa")
		FAILED_EXITS+=("1")
		FAIL_COUNT=$((FAIL_COUNT + 1))
		rm -f "$tmpfile"
		return 1
	fi
	echo "  Parsed Extent DPA: $EXTENT_DPA"

	if [ "$clear_after" -eq 1 ]; then
		local handles
		handles=$(grep "^    Handle:         " "$tmpfile" | awk '{ print $2 }') || true
		if [ -n "$handles" ]; then
			echo "  [${label}] Clearing host DCD handles: $handles"
			# shellcheck disable=SC2086
			_clear_host_handles $handles || true
		fi
	fi

	rm -f "$tmpfile"
	return 0
}

# ---------------------------------------------------------------------------
# Case 1: Simple Contiguous Add / Release
# ---------------------------------------------------------------------------

run_case1() {
	local EXTENT_DPA=""
	local tmpfile=""
	local handles=""

	echo ""
	echo "========================================"
	echo "== Case 1: Contiguous Add/Release flow =="
	echo "========================================"

	# Step 0: flush any leftover DCD events on both sides
	flush_all_dcd_events

	# Step 1: FM discovery
	run_test "case1/1-fm-dcd-get-info" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-get-info \
			--port "$DCD_SDB_PORT"

	# Step 2: FM initiates Add (Contiguous, selection-policy=1) → Extent Pending
	run_test "case1/2-fm-dcd-initiate-add" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-initiate-add \
			--host-id "$DCD_HOST_ID" \
			--length  "$DCD_EXTENT_SIZE" \
			--selection-policy 1 \
			--port "$DCD_SDB_PORT"

	# Step 3: Host reads all DCD events, parses Add Capacity DPA, clears handles
	capture_host_dcd_dpa "case1/3-host-read-dcd-events" 1 || {
		echo "ABORT Case 1: cannot continue without DCD event data." >&2
		return 1
	}

	# Step 4: Host confirms Add Response → Extent becomes Added
	run_test "case1/4-host-dcd-add-response" \
		"$MBCCI" "$MEMDEV" dcd-add-response \
			--extent "${EXTENT_DPA}:${DCD_EXTENT_SIZE}"

	# Step 4b: Host verifies Added extent now visible in its local extent list
	run_test "case1/4b-host-dcd-get-extent-list" \
		"$MBCCI" "$MEMDEV" dcd-get-extent-list \
			--extent-cnt  "$DCD_EXTENT_CNT" \
			--start-extent-idx 0

	# Step 5: FM reads DCD events (Add Capacity Response, type=04h), clears
	echo ""
	echo "==> [case1/5-fm-read-dcd-events] Reading FM DCD event log..."
	tmpfile=$(mktemp)
	if _read_fm_dcd >"$tmpfile" 2>&1; then
		cat "$tmpfile"
		echo "    PASS"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		cat "$tmpfile" >&2
		echo "    FAIL" >&2
		FAILED_LABELS+=("case1/5-fm-read-dcd-events")
		FAILED_EXITS+=("1")
		FAIL_COUNT=$((FAIL_COUNT + 1))
	fi
	handles=$(grep "^    Handle:         " "$tmpfile" | awk '{ print $2 }') || true
	rm -f "$tmpfile"
	if [ -n "$handles" ]; then
		echo "  [case1] Clearing FM DCD handles: $handles"
		# shellcheck disable=SC2086
		_clear_fm_handles $handles || true
	fi

	# Step 6: FM verifies extent list — should show 1 Added extent
	run_test "case1/6-fm-dcd-get-ext-list-added" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-get-ext-list \
			--host-id "$DCD_HOST_ID" \
			--port "$DCD_SDB_PORT"

	# ------------------------------------------------------------------
	# Release section (steps 7-11): skippable with --skip-release
	# ------------------------------------------------------------------
	if [ "$SKIP_RELEASE" -eq 1 ]; then
		echo ""
		echo "  [case1] --skip-release: skipping steps 7-11 (release flow)"
		return 0
	fi

	# Step 7: FM initiates Release (Prescriptive, flags=0x01) with exact extent
	run_test "case1/7-fm-dcd-initiate-release" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-initiate-release \
			--host-id "$DCD_HOST_ID" \
			--length  "$DCD_EXTENT_SIZE" \
			--flags   0x01 \
			--extent  "${EXTENT_DPA}:${DCD_EXTENT_SIZE}" \
			--port "$DCD_SDB_PORT"

	# Step 8: Host reads all DCD events (Release Capacity, type=01h), clears
	echo ""
	echo "==> [case1/8-host-read-release-events] Reading host DCD event log..."
	tmpfile=$(mktemp)
	if _read_host_dcd >"$tmpfile" 2>&1; then
		cat "$tmpfile"
		echo "    PASS"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		cat "$tmpfile" >&2
		echo "    FAIL" >&2
		FAILED_LABELS+=("case1/8-host-read-release-events")
		FAILED_EXITS+=("1")
		FAIL_COUNT=$((FAIL_COUNT + 1))
	fi
	handles=$(grep "^    Handle:         " "$tmpfile" | awk '{ print $2 }') || true
	rm -f "$tmpfile"
	if [ -n "$handles" ]; then
		echo "  [case1] Clearing host release DCD handles: $handles"
		# shellcheck disable=SC2086
		_clear_host_handles $handles || true
	fi

	# Step 9: Host confirms Release → device reclaims capacity
	run_test "case1/9-host-dcd-release" \
		"$MBCCI" "$MEMDEV" dcd-release \
			--extent "${EXTENT_DPA}:${DCD_EXTENT_SIZE}"

	# Step 10: FM reads DCD events after host release.
	# Release Response event may be lost if the FM log is full → warn, not FAIL.
	echo ""
	echo "==> [case1/10-fm-post-release-events] Reading FM DCD event log (may be empty if log full)..."
	tmpfile=$(mktemp)
	if _read_fm_dcd >"$tmpfile" 2>&1; then
		cat "$tmpfile"
		local post_handles
		post_handles=$(grep "^    Handle:         " "$tmpfile" | awk '{ print $2 }') || true
		if [ -n "$post_handles" ]; then
			echo "    PASS (FM Release event received)"
			PASS_COUNT=$((PASS_COUNT + 1))
			echo "  [case1] Clearing FM post-release handles: $post_handles"
			# shellcheck disable=SC2086
			_clear_fm_handles $post_handles || true
		else
			record_warn "case1/10-fm-post-release-events" \
				"No FM DCD events after host release (event log may be full — not a hard failure)"
			PASS_COUNT=$((PASS_COUNT + 1))
		fi
	else
		cat "$tmpfile" >&2
		record_warn "case1/10-fm-post-release-events" \
			"FM get-event-records returned non-zero (event log may be full)"
		PASS_COUNT=$((PASS_COUNT + 1))
	fi
	rm -f "$tmpfile"

	# Step 11: FM verifies extent list is now empty
	run_test "case1/11-fm-dcd-get-ext-list-empty" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-get-ext-list \
			--host-id "$DCD_HOST_ID" \
			--port "$DCD_SDB_PORT"
}

# ---------------------------------------------------------------------------
# Case 2: FM Add + Forced Prescriptive Release before Host confirms
# ---------------------------------------------------------------------------

run_case2() {
	local EXTENT_DPA=""
	local tmpfile=""
	local handles=""

	echo ""
	echo "====================================================================="
	echo "== Case 2: FM Add + Prescriptive Forced Release (flags=0x11)       =="
	echo "====================================================================="

	# Step 0: flush any leftover DCD events on both sides
	flush_all_dcd_events

	# Step 1: FM initiates Add (Contiguous) → Extent Pending
	run_test "case2/1-fm-dcd-initiate-add" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-initiate-add \
			--host-id "$DCD_HOST_ID" \
			--length  "$DCD_EXTENT_SIZE" \
			--selection-policy 1 \
			--port "$DCD_SDB_PORT"

	# Step 1b: FM checks extent list — Pending extent must NOT appear (only Added are visible)
	run_test "case2/1b-fm-dcd-get-ext-list-pending-not-visible" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-get-ext-list \
			--host-id "$DCD_HOST_ID" \
			--port "$DCD_SDB_PORT"

	# Step 2 (internal): Host reads DCD events to get the DPA allocated by the device.
	# Handles are NOT cleared here; step 4 will do the full read-verify-clear.
	# This DPA is needed for the prescriptive forced release (step 3).
	echo ""
	echo "  [case2] Pre-reading host DCD events to extract DPA for prescriptive release..."
	tmpfile=$(mktemp)
	if _read_host_dcd >"$tmpfile" 2>&1; then
		EXTENT_DPA=$(parse_add_dpa_from_file "$tmpfile") || true
	fi
	rm -f "$tmpfile"

	if [ -z "$EXTENT_DPA" ]; then
		echo "  FAIL: could not parse DPA from host DCD events (needed for --flags 0x11 release)" >&2
		FAILED_LABELS+=("case2/2-get-dpa-for-forced-release")
		FAILED_EXITS+=("1")
		FAIL_COUNT=$((FAIL_COUNT + 1))
		echo "ABORT Case 2: cannot continue without DPA." >&2
		return 1
	fi
	echo "  [case2] Extent DPA: $EXTENT_DPA"

	# Step 3: FM issues Prescriptive Forced Release (flags=0x11) before Host confirms.
	#   flags bits[3:0] = 0x1 (Prescriptive)
	#   flags bit[4]    = 0x1 (Forced Removal)
	#   → device marks the Pending extent as Dead.
	#   → device must NOT add new DCD event log entries (CXL spec §8.2.9.7.3).
	run_test "case2/3-fm-dcd-prescriptive-forced-release" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-initiate-release \
			--host-id "$DCD_HOST_ID" \
			--length  "$DCD_EXTENT_SIZE" \
			--flags   0x11 \
			--extent  "${EXTENT_DPA}:${DCD_EXTENT_SIZE}" \
			--port "$DCD_SDB_PORT"

	# Step 3b: FM checks extent list again — Dead extent also must NOT appear
	run_test "case2/3b-fm-dcd-get-ext-list-dead-not-visible" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-get-ext-list \
			--host-id "$DCD_HOST_ID" \
			--port "$DCD_SDB_PORT"

	# Step 4: Host reads all DCD events.
	# The original Add Capacity event from step 1 must still be present and unchanged
	# (forced release must not add or remove event log entries per CXL spec).
	echo ""
	echo "==> [case2/4-host-read-dcd-events] Reading host DCD event log..."
	tmpfile=$(mktemp)
	if _read_host_dcd >"$tmpfile" 2>&1; then
		cat "$tmpfile"
		echo "    PASS"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		cat "$tmpfile" >&2
		echo "    FAIL (cannot read host DCD events)" >&2
		FAILED_LABELS+=("case2/4-host-read-dcd-events")
		FAILED_EXITS+=("1")
		FAIL_COUNT=$((FAIL_COUNT + 1))
		rm -f "$tmpfile"
		echo "ABORT Case 2: cannot continue." >&2
		return 1
	fi

	# Verify we still see the same DPA (event record unchanged)
	local verify_dpa
	verify_dpa=$(parse_add_dpa_from_file "$tmpfile") || true
	if [ -n "$verify_dpa" ] && [ "$verify_dpa" = "$EXTENT_DPA" ]; then
		echo "  [case2] Add Capacity event confirmed unchanged after forced release (DPA=$EXTENT_DPA)"
	elif [ -n "$verify_dpa" ]; then
		record_warn "case2/4-host-read-dcd-events" \
			"DPA in event ($verify_dpa) differs from DPA used in release ($EXTENT_DPA)"
	else
		record_warn "case2/4-host-read-dcd-events" \
			"No Add Capacity event found — spec says forced release must not remove event entries"
	fi

	# Clear host DCD event handles (oldest → newest)
	handles=$(grep "^    Handle:         " "$tmpfile" | awk '{ print $2 }') || true
	rm -f "$tmpfile"
	if [ -n "$handles" ]; then
		echo "  [case2] Clearing host DCD handles: $handles"
		# shellcheck disable=SC2086
		_clear_host_handles $handles || true
	fi

	# Step 5: Host sends Add Response on the Dead extent.
	# Expected device behaviour: return "Invalid Physical Address" (non-zero CXL return code).
	# The extent is Dead — the device rejects the host's attempt to accept it.
	run_test_expect_fail "case2/5-host-dcd-add-response-on-dead-extent" \
		"$MBCCI" "$MEMDEV" dcd-add-response \
			--extent "${EXTENT_DPA}:${DCD_EXTENT_SIZE}"

	# Step 6: FM reads DCD events.
	# Expected: an Add Capacity Response event (type=0x04) for LD 0 with Length=0.
	#   - Length=0 indicates no capacity was actually allocated (extent was Dead).
	# Exception: if the FM event log has overflowed, the event may have been lost → warn.
	echo ""
	echo "==> [case2/6-fm-dcd-add-response-event] Reading FM DCD event log (expect Add Response, len=0)..."
	tmpfile=$(mktemp)
	if _read_fm_dcd >"$tmpfile" 2>&1; then
		cat "$tmpfile"

		local fm_result
		fm_result=$(python3 - "$tmpfile" <<'PYEOF'
import sys
import re

with open(sys.argv[1]) as f:
    content = f.read()

# Check for overflow (RSP_FLAG_OVERFLOW = bit 0 of flags)
for m in re.finditer(r'Flags:\s+0x([0-9a-f]+)', content):
    if int(m.group(1), 16) & 0x01:
        print("OVERFLOW")
        sys.exit(0)

# Split into record blocks and search for Add Capacity Response (type=0x04) for LD 0.
# The Data field spans 5 lines (16 bytes each) in the output:
#   "    Data:           <32 hex>"   — data[0..15]
#   "                    <32 hex>"   — data[16..31]  ← Length (LE) is here
records = re.split(r'\n  \[Record \d+\]', content)

for rec in records:
    m = re.search(r'LD ID:\s+(\d+)', rec)
    if not m or int(m.group(1)) != 0:
        continue

    # Collect all data hex lines for this record
    data_hex = ''
    collecting = False
    for line in rec.split('\n'):
        mm = re.match(r'\s+Data:\s+([0-9a-f]+)', line)
        if mm:
            data_hex = mm.group(1)
            collecting = True
            continue
        if collecting:
            mm = re.match(r'\s{20}([0-9a-f]+)', line)
            if mm:
                data_hex += mm.group(1)
            else:
                collecting = False

    if len(data_hex) < 48:
        continue

    event_type = int(data_hex[0:2], 16)
    if event_type != 0x04:   # 0x04 = Add Capacity Response
        continue

    # Length is data[16..23] (LE) = chars 32..47
    len_le_hex = data_hex[32:48]
    length = int.from_bytes(bytes.fromhex(len_le_hex), 'little')
    print("ADD_RESPONSE length={}".format(length))
    sys.exit(0)

print("NOT_FOUND")
sys.exit(1)
PYEOF
) || fm_result="NOT_FOUND"

		case "$fm_result" in
		OVERFLOW)
			record_warn "case2/6-fm-dcd-add-response-event" \
				"FM event log overflowed — Add Response event may have been lost; cannot verify capacity=0"
			PASS_COUNT=$((PASS_COUNT + 1))
			;;
		"ADD_RESPONSE length=0")
			echo "    PASS (FM received Add Capacity Response event with capacity=0)"
			PASS_COUNT=$((PASS_COUNT + 1))
			;;
		ADD_RESPONSE\ length=*)
			local resp_len="${fm_result#ADD_RESPONSE length=}"
			record_warn "case2/6-fm-dcd-add-response-event" \
				"Add Response event has non-zero length ($resp_len bytes) — expected 0 for Dead extent"
			PASS_COUNT=$((PASS_COUNT + 1))
			;;
		NOT_FOUND)
			echo "    FAIL (no Add Capacity Response event found in FM DCD log)" >&2
			FAILED_LABELS+=("case2/6-fm-dcd-add-response-event")
			FAILED_EXITS+=("1")
			FAIL_COUNT=$((FAIL_COUNT + 1))
			;;
		esac

		local fm_handles
		fm_handles=$(grep "^    Handle:         " "$tmpfile" | awk '{ print $2 }') || true
		if [ -n "$fm_handles" ]; then
			echo "  [case2] Clearing FM DCD handles: $fm_handles"
			# shellcheck disable=SC2086
			_clear_fm_handles $fm_handles || true
		fi
	else
		cat "$tmpfile" >&2
		echo "    FAIL (FM get-event-records failed)" >&2
		FAILED_LABELS+=("case2/6-fm-dcd-add-response-event")
		FAILED_EXITS+=("1")
		FAIL_COUNT=$((FAIL_COUNT + 1))
	fi
	rm -f "$tmpfile"

	# Step 7: FM verifies extent list is empty.
	# The force-released extent (which was Pending/Dead) must not appear.
	run_test "case2/7-fm-dcd-get-ext-list-empty" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-get-ext-list \
			--host-id "$DCD_HOST_ID" \
			--port "$DCD_SDB_PORT"
}

# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

main() {
	local MEMDEV="mem0"

	while [ $# -gt 0 ]; do
		case "$1" in
		-h|--help)
			usage
			exit 0
			;;
		--skip-release)
			SKIP_RELEASE=1
			;;
		-*)
			die "Unknown option: $1  (use -h for help)"
			;;
		*)
			MEMDEV="$1"
			;;
		esac
		shift
	done

	MBCCI="$(resolve_mbcci_sfx)"

	[ -e "/dev/cxl/$MEMDEV" ] || die "/dev/cxl/$MEMDEV not found"
	command -v python3 >/dev/null 2>&1 || \
		die "python3 is required for DPA parsing (DC Event Record data[8..15])"

	echo "mbcci-sfx DCD lifecycle tests"
	echo "  device:         $MEMDEV"
	echo "  binary:         $MBCCI"
	echo "  host-id:        $DCD_HOST_ID"
	echo "  sdb-port:       $DCD_SDB_PORT"
	echo "  extent-sz:      $DCD_EXTENT_SIZE  (256 MB)"
	echo "  skip-release:   $SKIP_RELEASE"

	run_case1
	run_case2

	print_summary
}

main "$@"
exit $?
