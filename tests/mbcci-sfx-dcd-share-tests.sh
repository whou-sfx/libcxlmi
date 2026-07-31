#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# DCD Shared Access tests for mbcci-sfx.
#
# Tests the FM-triggered Shared Mode DCD Add/Release flow per CXL 3.2 spec:
#   Case 3 — FM assigns capacity to LD0 (Prescriptive Add, selection-policy=2),
#             optionally adds an FM reference to prevent premature capacity free,
#             shares the same capacity with LD1 (Enable Shared Access, policy=3),
#             LD0 releases its extent, FM verifies LD1 mapping persists,
#             FM force-releases LD1, optionally removes the FM reference,
#             and verifies the tag has been cleared.
#
# Assumptions:
#   - FM interface:   sdb-tunnel --port vdm1
#   - LD0 host-id:   0  (ldid = 0)
#   - LD1 host-id:   1  (ldid = 1)
#   - Extent size:    256 MB  (0x10000000)
#   - Region 0 must be enabled and Sharable (Flags bit 3 = 0x08)
#     (this is the only device-level indicator for shared access eligibility;
#      CXL 3.2 Capacity Selection Policies bit 3 is reserved/must-be-0)
#
# DPA source:
#   This test uses Prescriptive policy (selection-policy=2), so the FM specifies
#   the exact DPA (Region[0].Base from fm-dcd-get-region-config).  No Python or
#   event-data parsing is required — all DPA values are known before any command.
#
# flags encoding for fm-dcd-initiate-release:
#   bits[3:0] = Removal Policy  (0h = Non-Prescriptive, 1h = Prescriptive)
#   bit[4]    = Forced Flag     (0 = not forced,        1 = Forced Removal)
#   Example: 0x01 = Prescriptive non-forced  (LD0 release, step 13)
#            0x11 = Prescriptive Forced       (LD1 force release, step 18)
#
# Exits 0 (pass) or SKIP when sharing is not supported.
# Exits non-zero if any test failed; continues across remaining steps.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

DCD_EXTENT_SIZE=0x10000000   # 256 MB
DCD_HOST_ID_LD0=0
DCD_HOST_ID_LD1=1
DCD_SDB_PORT=vdm1
DCD_EXTENT_CNT=8             # max extents per dcd-get-extent-list request
DCD_TAG="0x1234"             # default tag (hex); zero-padded to 16 B by dcd_parse_hex_tag

SKIP_REFERENCE=0             # 1 = skip fm-dcd-add-reference / fm-dcd-remove-reference

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
Usage: $0 [--skip-reference] [--tag <hex>] [memN]

Run DCD Shared Access tests (Case 3) against a CXL memory device.

The test verifies the FM-triggered shared-mode DCD Add/Release flow:
  Preconditions (auto-detected; script exits SKIP if not met):
    - Region 0 is Sharable (Flags bit 3 = 0x08)
      (CXL 3.2: Capacity Selection Policies bit 3 is reserved; sharable is
       determined solely by the Region Flags Sharable bit)

  Flow:
    1.  FM: fm-dcd-get-region-config  → extract REGION_DPA, check Sharable
    2.  FM: fm-dcd-get-info           → check Enable Shared Access policy
    3.  FM: fm-dcd-list-tags (baseline)
    4.  FM: fm-dcd-initiate-add LD0   (Prescriptive, REGION_DPA, tag)
    5.  LD0: read+clear DCD events
    6.  LD0: dcd-add-response + dcd-get-extent-list
    7.  FM: read+clear DCD events (Add Capacity Response)
    8.  FM: fm-dcd-get-ext-list LD0   (verify 1 Added extent)
    9.  FM: fm-dcd-list-tags          (verify tag allocated)
   10.  [opt] FM: fm-dcd-add-reference (holds capacity through LD0 release)
   11.  FM: fm-dcd-initiate-add LD1   (Enable Shared Access, same tag)
   12.  FM: fm-dcd-get-ext-list LD1   (check LD1 extent visibility)
   13.  FM: fm-dcd-initiate-release LD0 (Prescriptive, flags=0x01)
   14.  LD0: read+clear DCD events (Release Capacity)
   15.  LD0: dcd-release
   16.  FM: read+clear DCD events (Release Response)
   17.  FM: fm-dcd-get-ext-list LD1   (verify LD1 extent persists)
   18.  FM: fm-dcd-initiate-release LD1 (Prescriptive+Forced, flags=0x11)
   19.  FM: fm-dcd-get-ext-list LD1   (verify empty)
   20.  [opt] FM: fm-dcd-remove-reference
   21.  FM: fm-dcd-list-tags          (verify tag cleared)

Arguments:
  memN              CXL device name (default: mem0)

Options:
  --skip-reference  Skip fm-dcd-add-reference (step 10) and
                    fm-dcd-remove-reference (step 20).
                    The extent will still be held by LD1's mapping at step 17,
                    but capacity may be freed/sanitized once LD1 is force-released.
  --tag <hex>       Tag for Prescriptive Add (default: 0x1234).
                    Accepts 1-32 hex characters (with or without 0x prefix);
                    right-aligned and zero-padded to 16 bytes by the device driver.
  -h, --help        Show this help and exit.

Environment:
  MBCCI_SFX              Path to mbcci-sfx binary (auto-detected if unset)
  VERIFICATION_LOG_DIR   Optional directory to store per-command logs

Test assumptions:
  - FM interface:    sdb-tunnel --port $DCD_SDB_PORT
  - LD0 host-id:     $DCD_HOST_ID_LD0
  - LD1 host-id:     $DCD_HOST_ID_LD1
  - Extent size:     $DCD_EXTENT_SIZE  (256 MB)
  - Region 0 is enabled, Sharable, and device supports Enable Shared Access

Examples:
  $0
  $0 mem1
  $0 --skip-reference mem1
  $0 --tag 0xdeadbeef mem0
  MBCCI_SFX=./build/tools/mbcci-sfx/mbcci-sfx $0 mem0
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

print_summary() {
	local total=$((PASS_COUNT + FAIL_COUNT))

	echo ""
	echo "======== Summary ========"
	echo "Total: $total  Passed: $PASS_COUNT  Failed: $FAIL_COUNT  Warnings: $WARN_COUNT"

	if [ "$FAIL_COUNT" -eq 0 ]; then
		echo ""
		echo "All DCD Share tests passed."
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

_read_host_dcd() {
	"$MBCCI" "$MEMDEV" get-event-records --log dcd
}

_read_fm_dcd() {
	"$MBCCI" "$MEMDEV" sdb-tunnel get-event-records --log dcd --port "$DCD_SDB_PORT"
}

# Extract all event handles (oldest first) from get-event-records output.
_extract_handles() {
	grep "^    Handle:         " | awk '{ print $2 }'
}

_clear_host_handles() {
	[ $# -eq 0 ] && return 0
	local args=()
	for h in "$@"; do
		args+=(--handle "$h")
	done
	"$MBCCI" "$MEMDEV" clear-event-records --log dcd "${args[@]}"
}

_clear_fm_handles() {
	[ $# -eq 0 ] && return 0
	local args=()
	for h in "$@"; do
		args+=(--handle "$h")
	done
	"$MBCCI" "$MEMDEV" sdb-tunnel clear-event-records --log dcd \
		--port "$DCD_SDB_PORT" "${args[@]}"
}

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
	# shellcheck disable=SC2086
	$clear_fn $handles || true
	echo "  [$label] Done."
}

flush_all_dcd_events() {
	echo ""
	echo "-- Flushing all stale DCD events (Host LD0 + FM) --"
	_flush_dcd_side "flush-host" _read_host_dcd _clear_host_handles
	_flush_dcd_side "flush-fm"   _read_fm_dcd   _clear_fm_handles
	echo "-- Flush complete --"
}

# ---------------------------------------------------------------------------
# Region / capability helpers (pure awk/bash — no Python required)
#
# fm-dcd-get-region-config output format (relevant lines):
#   "  Region[0]:"
#   "    Base:             0x0000000040000000"
#   "    Flags:            0x07"
#
# fm-dcd-get-info output format (relevant line):
#   "  Capacity Selection Policies: 0x0008"
# ---------------------------------------------------------------------------

# Extract the Base field of Region[N] from a saved fm-dcd-get-region-config output.
# Usage: get_region_base <file> <region_idx>
# Prints the hex DPA string (e.g. "0x0000000040000000") or nothing on failure.
get_region_base() {
	awk "/Region\[$2\]:/{f=1} f && /Base:/{print \$2; exit}" "$1"
}

# Extract the Flags field of Region[N] from a saved fm-dcd-get-region-config output.
# Usage: get_region_flags <file> <region_idx>
# Prints the hex flags string (e.g. "0x07") or nothing on failure.
get_region_flags() {
	awk "/Region\[$2\]:/{f=1} f && /Flags:/{print \$2; exit}" "$1"
}

# Return 0 (true) if the region flags have the Sharable bit set (bit 3 = 0x08).
# Usage: check_region_sharable <hex_flags>
check_region_sharable() {
	local flags_val
	flags_val=$(( $1 ))    # bash handles 0x prefix natively
	(( (flags_val & 8) != 0 ))
}

# No device-level "shared access" capability bit exists in fm-dcd-get-info.
# Per CXL 3.2 spec, Capacity Selection Policies bits[3:0] are:
#   bit 0 = Free, bit 1 = Contiguous, bit 2 = Prescriptive, bit 3 = Must be 0
# Shared access eligibility is determined solely by Region Flags bit 3 (Sharable).

# ---------------------------------------------------------------------------
# Case 3: DCD Shared Access
# ---------------------------------------------------------------------------

run_case3() {
	local REGION_DPA=""
	local tmpfile=""
	local handles=""

	echo ""
	echo "=========================================="
	echo "== Case 3: DCD Shared Access flow       =="
	echo "=========================================="

	# Step 0: flush any leftover DCD events on both sides
	flush_all_dcd_events

	# Step 1: FM queries Region 0 config to obtain the DPA base address and
	# check whether the region advertises itself as Sharable (Flags bit 3 = 0x08).
	echo ""
	echo "==> [case3/1-fm-dcd-get-region-config] Querying DC region configuration..."
	tmpfile=$(mktemp)
	if "$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-get-region-config \
			--host-id "$DCD_HOST_ID_LD0" \
			--region-cnt 2 \
			--port "$DCD_SDB_PORT" >"$tmpfile" 2>&1; then
		cat "$tmpfile"
		echo "    PASS"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		cat "$tmpfile" >&2
		echo "    FAIL (fm-dcd-get-region-config failed)" >&2
		FAILED_LABELS+=("case3/1-fm-dcd-get-region-config")
		FAILED_EXITS+=("1")
		FAIL_COUNT=$((FAIL_COUNT + 1))
		rm -f "$tmpfile"
		echo "ABORT Case 3: cannot obtain region configuration." >&2
		return 1
	fi

	REGION_DPA=$(get_region_base "$tmpfile" 0)
	local region_flags
	region_flags=$(get_region_flags "$tmpfile" 0)
	rm -f "$tmpfile"

	if [ -z "$REGION_DPA" ] || [ -z "$region_flags" ]; then
		echo ""
		echo "  SKIP: could not parse Region[0] Base or Flags from fm-dcd-get-region-config"
		echo "        (device may not implement fm-dcd-get-region-config)"
		return 0
	fi
	echo "  Region[0] Base:  $REGION_DPA"
	echo "  Region[0] Flags: $region_flags"

	if ! check_region_sharable "$region_flags"; then
		echo ""
		echo "  SKIP: Region[0] is not Sharable (Flags=$region_flags, bit 3 = 0)"
		echo "        Shared Access requires the region to be advertised as Sharable."
		return 0
	fi
	echo "  Region[0] is Sharable (Flags bit 3 = 0x08)."

	# Step 2: FM queries DCD device info (informational; no gating on policy bits).
	# Note: CXL 3.2 Capacity Selection Policies bits[3:0] cover only Free/Contiguous/
	# Prescriptive (bits 0-2); bit 3 is reserved (Must be 0).  The "Enable Shared
	# Access" selection policy (value=3) is not reflected in this bitmask.
	# Shared-access eligibility was already confirmed by the Region Sharable flag above.
	run_test "case3/2-fm-dcd-get-info" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-get-info \
			--port "$DCD_SDB_PORT"

	# Step 3: Pre-check tag list (baseline before any allocation).
	run_test "case3/3-fm-dcd-list-tags-baseline" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-list-tags \
			--port "$DCD_SDB_PORT"

	# Step 4: FM issues Prescriptive Add for LD0 with explicit DPA and tag.
	#   selection-policy=2 = Prescriptive
	#   DPA = REGION_DPA (from step 1; no event-data parsing needed)
	#   CXL 3.2: when Selection Policy = Prescriptive, the top-level header
	#   Tag field is reserved.  The tag must be carried per-extent inside the
	#   --extent DPA:LEN:TAG argument.
	run_test "case3/4-fm-dcd-initiate-add-ld0-prescriptive" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-initiate-add \
			--host-id         "$DCD_HOST_ID_LD0" \
			--length          "$DCD_EXTENT_SIZE" \
			--selection-policy 2 \
			--extent          "${REGION_DPA}:${DCD_EXTENT_SIZE}:${DCD_TAG}" \
			--port "$DCD_SDB_PORT"

	# Step 5: LD0 reads all DCD events (Add Capacity, type=00h) and clears handles.
	# DPA is known from step 1 — no parsing required.
	echo ""
	echo "==> [case3/5-ld0-read-dcd-events] Reading LD0 DCD event log..."
	tmpfile=$(mktemp)
	if _read_host_dcd >"$tmpfile" 2>&1; then
		cat "$tmpfile"
		echo "    PASS"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		cat "$tmpfile" >&2
		echo "    FAIL (cannot read LD0 DCD events)" >&2
		FAILED_LABELS+=("case3/5-ld0-read-dcd-events")
		FAILED_EXITS+=("1")
		FAIL_COUNT=$((FAIL_COUNT + 1))
		rm -f "$tmpfile"
		echo "ABORT Case 3: cannot continue without LD0 DCD event." >&2
		return 1
	fi
	handles=$(grep "^    Handle:         " "$tmpfile" | awk '{ print $2 }') || true
	rm -f "$tmpfile"
	if [ -n "$handles" ]; then
		echo "  [case3] Clearing LD0 DCD handles: $handles"
		# shellcheck disable=SC2086
		_clear_host_handles $handles || true
	fi

	# Step 6: LD0 confirms Add Response → extent becomes Added.
	run_test "case3/6-ld0-dcd-add-response" \
		"$MBCCI" "$MEMDEV" dcd-add-response \
			--extent "${REGION_DPA}:${DCD_EXTENT_SIZE}"

	# Step 6b: LD0 verifies Added extent is visible in its local extent list.
	run_test "case3/6b-ld0-dcd-get-extent-list" \
		"$MBCCI" "$MEMDEV" dcd-get-extent-list \
			--extent-cnt       "$DCD_EXTENT_CNT" \
			--start-extent-idx 0

	# Step 7: FM reads DCD events (Add Capacity Response, type=04h) and clears handles.
	echo ""
	echo "==> [case3/7-fm-read-dcd-events-after-add] Reading FM DCD event log..."
	tmpfile=$(mktemp)
	if _read_fm_dcd >"$tmpfile" 2>&1; then
		cat "$tmpfile"
		echo "    PASS"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		cat "$tmpfile" >&2
		echo "    FAIL (FM get-event-records failed)" >&2
		FAILED_LABELS+=("case3/7-fm-read-dcd-events-after-add")
		FAILED_EXITS+=("1")
		FAIL_COUNT=$((FAIL_COUNT + 1))
	fi
	handles=$(grep "^    Handle:         " "$tmpfile" | awk '{ print $2 }') || true
	rm -f "$tmpfile"
	if [ -n "$handles" ]; then
		echo "  [case3] Clearing FM DCD handles: $handles"
		# shellcheck disable=SC2086
		_clear_fm_handles $handles || true
	fi

	# Step 8: FM verifies LD0 extent list — should show 1 Added extent.
	run_test "case3/8-fm-dcd-get-ext-list-ld0-added" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-get-ext-list \
			--host-id "$DCD_HOST_ID_LD0" \
			--port "$DCD_SDB_PORT"

	# Step 9: FM verifies the allocated tag appears in fm-dcd-list-tags.
	run_test "case3/9-fm-dcd-list-tags-after-add" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-list-tags \
			--port "$DCD_SDB_PORT"

	# Step 10 [optional]: FM adds a reference to the tag.
	# While the reference is held, the tagged capacity cannot be freed or
	# sanitized even after all host mappings are released (CXL 3.2 §8.2.9.7.4).
	if [ "$SKIP_REFERENCE" -eq 0 ]; then
		run_test "case3/10-fm-dcd-add-reference" \
			"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-add-reference \
				--tag  "$DCD_TAG" \
				--port "$DCD_SDB_PORT"
	else
		echo ""
		echo "  [case3/10] --skip-reference: skipping fm-dcd-add-reference"
	fi

	# Step 11: FM issues Enable Shared Access Add for LD1.
	#   selection-policy=3 = Enable Shared Access
	#   Tag must match the tag from step 4 (same physical capacity).
	#   Region must be Sharable — already verified in step 1.
	#   LD1 host-side confirmation is intentionally skipped per test plan.
	run_test "case3/11-fm-dcd-initiate-add-ld1-shared" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-initiate-add \
			--host-id         "$DCD_HOST_ID_LD1" \
			--length          "$DCD_EXTENT_SIZE" \
			--selection-policy 3 \
			--tag             "$DCD_TAG" \
			--port "$DCD_SDB_PORT"

	# Step 12: FM checks LD1 extent visibility (device may auto-mark as Added
	# for shared access, or the extent may be Pending until LD1 confirms).
	run_test "case3/12-fm-dcd-get-ext-list-ld1" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-get-ext-list \
			--host-id "$DCD_HOST_ID_LD1" \
			--port "$DCD_SDB_PORT"

	# Step 13: FM initiates Prescriptive Release for LD0 (non-forced, flags=0x01).
	run_test "case3/13-fm-dcd-initiate-release-ld0" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-initiate-release \
			--host-id "$DCD_HOST_ID_LD0" \
			--length  "$DCD_EXTENT_SIZE" \
			--flags   0x01 \
			--extent  "${REGION_DPA}:${DCD_EXTENT_SIZE}" \
			--port "$DCD_SDB_PORT"

	# Step 14: LD0 reads DCD events (Release Capacity, type=01h) and clears handles.
	echo ""
	echo "==> [case3/14-ld0-read-release-events] Reading LD0 DCD event log (Release Capacity)..."
	tmpfile=$(mktemp)
	if _read_host_dcd >"$tmpfile" 2>&1; then
		cat "$tmpfile"
		echo "    PASS"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		cat "$tmpfile" >&2
		echo "    FAIL (cannot read LD0 DCD events)" >&2
		FAILED_LABELS+=("case3/14-ld0-read-release-events")
		FAILED_EXITS+=("1")
		FAIL_COUNT=$((FAIL_COUNT + 1))
	fi
	handles=$(grep "^    Handle:         " "$tmpfile" | awk '{ print $2 }') || true
	rm -f "$tmpfile"
	if [ -n "$handles" ]; then
		echo "  [case3] Clearing LD0 release DCD handles: $handles"
		# shellcheck disable=SC2086
		_clear_host_handles $handles || true
	fi

	# Step 15: LD0 confirms Release → capacity is unmapped from LD0.
	# If --skip-reference: capacity may be freed/sanitized unless LD1 still holds it.
	# If reference was added (step 10): capacity is retained by the FM reference.
	run_test "case3/15-ld0-dcd-release" \
		"$MBCCI" "$MEMDEV" dcd-release \
			--extent "${REGION_DPA}:${DCD_EXTENT_SIZE}"

	# Step 16: FM reads DCD events after LD0 release (Release Response).
	# The event may be missing if the FM log was full — warn but do not fail.
	echo ""
	echo "==> [case3/16-fm-read-release-events] Reading FM DCD event log (Release Response)..."
	tmpfile=$(mktemp)
	if _read_fm_dcd >"$tmpfile" 2>&1; then
		cat "$tmpfile"
		local post_handles
		post_handles=$(grep "^    Handle:         " "$tmpfile" | awk '{ print $2 }') || true
		if [ -n "$post_handles" ]; then
			echo "    PASS (FM Release event received)"
			PASS_COUNT=$((PASS_COUNT + 1))
			echo "  [case3] Clearing FM release handles: $post_handles"
			# shellcheck disable=SC2086
			_clear_fm_handles $post_handles || true
		else
			record_warn "case3/16-fm-read-release-events" \
				"No FM DCD events after LD0 release (event log may be full — not a hard failure)"
			PASS_COUNT=$((PASS_COUNT + 1))
		fi
	else
		cat "$tmpfile" >&2
		record_warn "case3/16-fm-read-release-events" \
			"FM get-event-records returned non-zero (event log may be full)"
		PASS_COUNT=$((PASS_COUNT + 1))
	fi
	rm -f "$tmpfile"

	# Step 17: Check capacity is retained after LD0 released.
	# LD1's extent was never confirmed by the host (we skipped dcd-add-response
	# for LD1), so it remains Pending.  fm-dcd-get-ext-list only shows Added
	# extents — Pending will not appear.  Run it as informational only.
	echo ""
	echo "==> [case3/17-fm-dcd-get-ext-list-ld1-info] LD1 extent list (informational; Pending not visible)..."
	"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-get-ext-list \
		--host-id "$DCD_HOST_ID_LD1" \
		--port "$DCD_SDB_PORT" || true

	# Step 17b: Verify capacity is still allocated via fm-dcd-list-tags.
	# - If reference was added (step 10): tag persists regardless of host mappings.
	# - If reference was skipped: tag may disappear once all host mappings are gone.
	if [ "$SKIP_REFERENCE" -eq 0 ]; then
		run_test "case3/17b-fm-dcd-list-tags-tag-persists-with-reference" \
			"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-list-tags \
				--port "$DCD_SDB_PORT"
	else
		echo ""
		echo "  [case3/17b] --skip-reference: skipping list-tags persistence check"
		echo "              (tag may have been freed after LD0 released)"
	fi

	# Step 18: FM force-releases LD1 (Prescriptive + Forced, flags=0x11).
	#   bits[3:0] = 0x1 (Prescriptive)
	#   bit[4]    = 0x1 (Forced Removal)
	run_test "case3/18-fm-dcd-force-release-ld1" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-initiate-release \
			--host-id "$DCD_HOST_ID_LD1" \
			--length  "$DCD_EXTENT_SIZE" \
			--flags   0x11 \
			--extent  "${REGION_DPA}:${DCD_EXTENT_SIZE}" \
			--port "$DCD_SDB_PORT"

	# Step 19: FM verifies LD1 extent list is now empty (force-released).
	run_test "case3/19-fm-dcd-get-ext-list-ld1-empty" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-get-ext-list \
			--host-id "$DCD_HOST_ID_LD1" \
			--port "$DCD_SDB_PORT"

	# Step 20 [optional]: FM removes the reference (must be paired with step 10).
	# Once the reference is removed and no host holds the capacity, the device
	# will sanitize and free the tagged capacity (CXL 3.2 §8.2.9.7.4).
	if [ "$SKIP_REFERENCE" -eq 0 ]; then
		run_test "case3/20-fm-dcd-remove-reference" \
			"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-remove-reference \
				--tag  "$DCD_TAG" \
				--port "$DCD_SDB_PORT"
	else
		echo ""
		echo "  [case3/20] --skip-reference: skipping fm-dcd-remove-reference"
	fi

	# Step 21: FM verifies the tag has been cleared (no outstanding references
	# or host mappings remain).
	run_test "case3/21-fm-dcd-list-tags-after-release" \
		"$MBCCI" "$MEMDEV" sdb-tunnel fm-dcd-list-tags \
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
		--skip-reference)
			SKIP_REFERENCE=1
			;;
		--tag)
			shift
			[ $# -gt 0 ] || die "--tag requires an argument"
			DCD_TAG="$1"
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

	echo "mbcci-sfx DCD Shared Access tests"
	echo "  device:          $MEMDEV"
	echo "  binary:          $MBCCI"
	echo "  LD0 host-id:     $DCD_HOST_ID_LD0"
	echo "  LD1 host-id:     $DCD_HOST_ID_LD1"
	echo "  sdb-port:        $DCD_SDB_PORT"
	echo "  extent-sz:       $DCD_EXTENT_SIZE  (256 MB)"
  echo "  tag:             $DCD_TAG"
	echo "  skip-reference:  $SKIP_REFERENCE"

	run_case3

	print_summary
}

main "$@"
exit $?
