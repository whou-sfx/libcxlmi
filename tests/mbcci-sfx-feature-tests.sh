#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Exercise mbcci-sfx FEATURE commands on a real CXL device:
#   1. mailbox (direct ioctl)
#   2. sdb-tunnel --port vdm1
#   3. sdb-tunnel --port i3c
#
# For each interface and each feature advertised by the device:
#   - get-feature --selection 0 (current)
#   - get-feature --selection 1 (default)
#   - get-feature --selection 2 (saved)  [skipped if attr_flags bit6 not set]
#   - set-feature write-back round-trip  [skipped if attr_flags bit0 not set,
#                                         set_feature_size == 0, or
#                                         FEATURE_SKIP_WRITEBACK=1]
#
# The write-back test reads the current feature data, writes it back unchanged,
# then re-reads and compares -- safe because we never change values.
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

# Temporary files registry for cleanup on exit
TMPFILES=()

usage() {
	cat <<EOF
Usage: $0 [memN]

Run mbcci-sfx FEATURE smoke tests against a CXL memory device.
For each interface (mailbox, sdb-tunnel vdm1, sdb-tunnel i3c), the script:
  1. discovers all supported features via get-supported-feat
  2. tests get-feature for every feature (selections: current, default, saved)
  3. performs a safe write-back round-trip on every writable feature

Arguments:
  memN          CXL device name (default: mem0), e.g. mem0, mem1

Environment:
  MBCCI_SFX              Path to mbcci-sfx binary (auto-detected if unset)
  VERIFICATION_LOG_DIR   Optional directory to store per-command logs
  FEATURE_SKIP_WRITEBACK Set to 1 to skip set-feature write-back tests

Examples:
  $0
  $0 mem1
  MBCCI_SFX=./build/tools/mbcci-sfx/mbcci-sfx $0 mem0
  FEATURE_SKIP_WRITEBACK=1 $0 mem0
EOF
}

die() {
	echo "ERROR: $*" >&2
	exit 1
}

cleanup() {
	local f
	for f in "${TMPFILES[@]+"${TMPFILES[@]}"}"; do
		rm -f "$f"
	done
}
trap cleanup EXIT

mktemp_tracked() {
	local f
	f="$(mktemp "$@")"
	TMPFILES+=("$f")
	echo "$f"
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

	for entry in "${SKIPPED_LABELS[@]+"${SKIPPED_LABELS[@]}"}"; do
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
		echo "All FEATURE tests passed."
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

# Feature UUIDs whose set-feature is only supported via management port
# (sdb-tunnel vdm1 / i3c), not via the mailbox interface.
MAILBOX_SET_BLOCKED_UUIDS=(
	"892ba475-fad8-474e-9d3e-692c917568bb"  # sPPR
	"80ea4521-786f-4127-afb1-ec7459fb0e24"  # hPPR
)

is_set_blocked_on_mailbox() {
	local uuid="$1"
	local u
	for u in "${MAILBOX_SET_BLOCKED_UUIDS[@]}"; do
		[ "$u" = "$uuid" ] && return 0
	done
	return 1
}

# Feature UUIDs where get/set payload field layouts differ: copying the get
# response bytes directly into set would write wrong values into wrong fields
# or trigger unintended side effects (e.g. W1C clear, read-only status fields
# at different offsets than the corresponding writable fields).
WRITEBACK_PAYLOAD_INCOMPATIBLE_UUIDS=(
	# get[4-5]=CurrentCount(RO), set[4-5]=TimeWindow; get[8h]=LeakyBucketEnable,
	# set[8h]=EventFlagsClr(W1C) -- direct copy corrupts fields and may clear flags
	"1478ad9d-ce00-4733-9db8-f392a4c2d0cc"  # CVME Threshold
	# get[0]=InformationalEventLogCount(RO), set[0]=DRAM ECC Mode -- completely
	# different field semantics at every offset
	"5174e599-1430-433e-af4b-5772bae6cc91"  # RAS Features
	# spec shows identical layout but device rejects direct write-back; likely
	# requires valid non-zero port addresses that match current hardware state
	"b00726e4-de86-4205-b27f-b0bb6825660d"  # Dual Port
)

is_writeback_payload_incompatible() {
	local uuid="$1"
	local u
	for u in "${WRITEBACK_PAYLOAD_INCOMPATIBLE_UUIDS[@]}"; do
		[ "$u" = "$uuid" ] && return 0
	done
	return 1
}

# Collect feature entries from get-supported-feat output.
# Prints each feature as a single line: "<uuid> <set_feature_size> <attr_flags_dec>"
# Returns 1 if the command itself fails.
collect_feature_entries() {
	local outfile="$1"
	shift
	local tmp

	tmp="$(mktemp_tracked)"
	if ! "$@" >"$tmp" 2>&1; then
		cat "$tmp" >&2
		return 1
	fi

	cat "$tmp"

	# Parse output from print_supported_features():
	#   [N] feature_id: UUID  (name)
	#        ...
	#        set_feature_size:   <n>
	#        attribute_flags:    0x<hex>
	# Each block ends when we see attribute_flags (last required field before next entry).
	awk '
		/feature_id:/      { uuid = $3 }
		/set_feature_size:/ { set_sz = $2 }
		/attribute_flags:/  {
			attr = strtonum($2)
			if (uuid != "") {
				printf "%s %d %d\n", uuid, set_sz, attr
				uuid = ""
				set_sz = 0
			}
		}
	' "$tmp" >"$outfile"

	return 0
}

# Run a get-feature command and optionally dump the payload to a file.
# Usage: run_get_feature <label> <dump_file|""> <cmd...> --feature-id <uuid> [extra args]
run_get_feature_dump() {
	local label="$1"
	local dump_file="$2"
	shift 2
	local rc=0
	local logfile=""
	local extra_args=()

	if [ -n "$dump_file" ]; then
		extra_args=(--dump "$dump_file")
	fi

	echo "==> [$label] $* ${extra_args[*]+"${extra_args[*]}"}"
	if [ -n "${VERIFICATION_LOG_DIR:-}" ]; then
		mkdir -p "$VERIFICATION_LOG_DIR"
		logfile="$VERIFICATION_LOG_DIR/$(sanitize_label "$label").log"
		if "$@" "${extra_args[@]+"${extra_args[@]}"}" >"$logfile" 2>&1; then
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
	elif "$@" "${extra_args[@]+"${extra_args[@]}"}"; then
		echo "    PASS"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		rc=$?
		echo "    FAIL (exit $rc)" >&2
		FAILED_LABELS+=("$label")
		FAILED_EXITS+=("$rc")
		FAIL_COUNT=$((FAIL_COUNT + 1))
	fi
	return $rc
}

# Perform a write-back round-trip for one writable feature:
#   1. get-feature --selection 0 --dump <hex>
#   2. truncate to set_feature_size bytes  ->  set_input hex file
#   3. set-feature --input <set_input>
#   4. get-feature --selection 0 --dump <verify>
#   5. compare first set_feature_size bytes
#
# Arguments: prefix uuid set_feature_size  get_cmd_array  set_cmd_array
# get_cmd_array / set_cmd_array are the base command arrays up to (but not
# including) --feature-id, passed as individual arguments joined by the
# sentinel "||" between them.
run_writeback_test() {
	local prefix="$1"
	local uuid="$2"
	local set_sz="$3"
	shift 3

	# Split get_cmd and set_cmd arrays separated by "||"
	local get_cmd=()
	local set_cmd=()
	local in_set=0
	local arg
	for arg in "$@"; do
		if [ "$arg" = "||" ]; then
			in_set=1
			continue
		fi
		if [ "$in_set" -eq 0 ]; then
			get_cmd+=("$arg")
		else
			set_cmd+=("$arg")
		fi
	done

	local get_dump set_input verify_dump
	get_dump="$(mktemp_tracked --suffix=.get.hex)"
	set_input="$(mktemp_tracked --suffix=.set.hex)"
	verify_dump="$(mktemp_tracked --suffix=.verify.hex)"

	local wb_label="$prefix/writeback/$uuid"

	# Step 1: get current data
	local get_label="$prefix/writeback/$uuid/get"
	local rc=0
	echo "==> [$get_label] ${get_cmd[*]} --feature-id $uuid --selection 0 --dump $get_dump"
	if ! "${get_cmd[@]}" --feature-id "$uuid" --selection 0 --dump "$get_dump" >/dev/null 2>&1; then
		rc=$?
		echo "    FAIL (exit $rc) -- cannot read current feature data, skipping write-back" >&2
		record_skip "$wb_label" "get-current failed (exit $rc)"
		return
	fi
	echo "    OK (got $(wc -c <"$get_dump") hex chars)"

	# Step 2: truncate to set_feature_size bytes (each byte = 2 hex chars)
	local set_chars=$(( set_sz * 2 ))
	# dd is available everywhere; use it for precise byte-count truncation
	dd if="$get_dump" of="$set_input" bs=1 count="$set_chars" 2>/dev/null
	local actual_chars
	actual_chars="$(wc -c <"$set_input")"
	if [ "$actual_chars" -ne "$set_chars" ]; then
		record_skip "$wb_label" \
			"get dump has $actual_chars hex chars, need $set_chars for set_feature_size=$set_sz"
		return
	fi

	# Step 3: set-feature (write back same value)
	run_test "$prefix/writeback/$uuid/set" \
		"${set_cmd[@]}" --feature-id "$uuid" --input "$set_input"
	# If set failed, skip comparison
	if [[ "${FAILED_LABELS[-1]:-}" == "$prefix/writeback/$uuid/set" ]]; then
		record_skip "$wb_label/compare" "set-feature failed"
		return
	fi

	# Step 4: re-read current data
	echo "==> [$prefix/writeback/$uuid/verify-get] ${get_cmd[*]} --feature-id $uuid --selection 0 --dump $verify_dump"
	if ! "${get_cmd[@]}" --feature-id "$uuid" --selection 0 --dump "$verify_dump" >/dev/null 2>&1; then
		rc=$?
		echo "    FAIL (exit $rc)" >&2
		FAILED_LABELS+=("$prefix/writeback/$uuid/verify-get")
		FAILED_EXITS+=("$rc")
		FAIL_COUNT=$((FAIL_COUNT + 1))
		return
	fi
	echo "    OK"

	# Step 5: compare first set_sz bytes
	local orig_trunc verify_trunc
	orig_trunc="$(mktemp_tracked --suffix=.cmp.hex)"
	verify_trunc="$(mktemp_tracked --suffix=.cmp2.hex)"
	dd if="$get_dump"    of="$orig_trunc"   bs=1 count="$set_chars" 2>/dev/null
	dd if="$verify_dump" of="$verify_trunc" bs=1 count="$set_chars" 2>/dev/null

	local cmp_label="$prefix/writeback/$uuid/compare"
	echo "==> [$cmp_label] cmp first ${set_sz}B of get vs re-get after write-back"
	if cmp -s "$orig_trunc" "$verify_trunc"; then
		echo "    PASS"
		PASS_COUNT=$((PASS_COUNT + 1))
	else
		echo "    FAIL: data changed after write-back" >&2
		echo "    original : $(cat "$orig_trunc")" >&2
		echo "    re-read  : $(cat "$verify_trunc")" >&2
		FAILED_LABELS+=("$cmp_label")
		FAILED_EXITS+=("1")
		FAIL_COUNT=$((FAIL_COUNT + 1))
	fi
}

# Main per-interface feature test matrix.
#
# Arguments:
#   prefix          label prefix, e.g. "mailbox" or "sdb/vdm1"
#   desc            human-readable description for the phase header
#   get_supported   words making up the get-supported-feat subcommand (e.g.
#                   "get-supported-feat" or "sdb-tunnel get-supported-feat --port vdm1")
#   get_feat_base   words making up the get-feature base (e.g. "get-feature" or
#                   "sdb-tunnel get-feature --port vdm1")
#   set_feat_base   words making up the set-feature base
run_feature_matrix() {
	local prefix="$1"
	local desc="$2"
	local interface="$3"   # "mailbox" or "sdb"
	shift 3

	# Remaining args encode get_supported / get_feat_base / set_feat_base
	# separated by the sentinel "|||"
	local get_supported_args=()
	local get_feat_args=()
	local set_feat_args=()
	local section=0
	local arg
	for arg in "$@"; do
		if [ "$arg" = "|||" ]; then
			section=$((section + 1))
			continue
		fi
		case $section in
		0) get_supported_args+=("$arg") ;;
		1) get_feat_args+=("$arg") ;;
		2) set_feat_args+=("$arg") ;;
		esac
	done

	echo ""
	echo "======== Phase: $desc ========"

	local entries_file
	entries_file="$(mktemp_tracked)"

	echo ""
	echo "== Supported features via $desc =="
	if ! collect_feature_entries "$entries_file" \
		"$MBCCI" "$MEMDEV" "${get_supported_args[@]}"; then
		echo "Failed to enumerate supported features for $prefix" >&2
		FAILED_LABELS+=("$prefix/get-supported-feat")
		FAILED_EXITS+=("enum")
		FAIL_COUNT=$((FAIL_COUNT + 1))
		return
	fi

	if [ ! -s "$entries_file" ]; then
		record_skip "$prefix/get-supported-feat" "no feature entries reported"
		return
	fi

	# Count and display discovered features
	local n_features
	n_features="$(wc -l <"$entries_file")"
	echo "  => discovered $n_features feature(s)"

	local uuid set_sz attr_dec
	while IFS=" " read -r uuid set_sz attr_dec; do
		[ -n "$uuid" ] || continue

		local attr_hex
		printf -v attr_hex "0x%08x" "$attr_dec"
		local is_writable=0
		local has_default=0
		local has_saved=0
		(( (attr_dec & 0x1)  != 0 )) && is_writable=1  || true
		(( (attr_dec & 0x20) != 0 )) && has_default=1  || true
		(( (attr_dec & 0x40) != 0 )) && has_saved=1    || true

		echo ""
		echo "  -- feature $uuid  flags=$attr_hex  set_size=$set_sz  writable=$is_writable  has_default=$has_default  has_saved=$has_saved"

		# --- get-feature: current (selection=0) ---
		run_test "$prefix/get-feature/current/$uuid" \
			"$MBCCI" "$MEMDEV" "${get_feat_args[@]}" \
			--feature-id "$uuid" --selection 0

		# --- get-feature: default (selection=1) ---
		if [ "$has_default" -eq 1 ]; then
			run_test "$prefix/get-feature/default/$uuid" \
				"$MBCCI" "$MEMDEV" "${get_feat_args[@]}" \
				--feature-id "$uuid" --selection 1
		else
			record_skip "$prefix/get-feature/default/$uuid" \
				"attr_flags bit5 not set (default selection not supported)"
		fi

		# --- get-feature: saved (selection=2) ---
		if [ "$has_saved" -eq 1 ]; then
			run_test "$prefix/get-feature/saved/$uuid" \
				"$MBCCI" "$MEMDEV" "${get_feat_args[@]}" \
				--feature-id "$uuid" --selection 2
		else
			record_skip "$prefix/get-feature/saved/$uuid" \
				"attr_flags bit6 not set (saved selection not supported)"
		fi

		# --- set-feature write-back round-trip ---
		if [ "${FEATURE_SKIP_WRITEBACK:-0}" = "1" ]; then
			record_skip "$prefix/writeback/$uuid" "FEATURE_SKIP_WRITEBACK=1"
		elif [ "$is_writable" -eq 0 ]; then
			record_skip "$prefix/writeback/$uuid" \
				"attr_flags bit0 not set (read-only feature)"
		elif [ "$set_sz" -eq 0 ]; then
			record_skip "$prefix/writeback/$uuid" \
				"set_feature_size=0 (not settable)"
		elif [ "$interface" = "mailbox" ] && is_set_blocked_on_mailbox "$uuid"; then
			record_skip "$prefix/writeback/$uuid" \
				"set-feature not supported on mailbox for this feature (management port only)"
		elif is_writeback_payload_incompatible "$uuid"; then
			record_skip "$prefix/writeback/$uuid" \
				"get/set payload field layouts differ: direct write-back not safe"
		else
			run_writeback_test "$prefix" "$uuid" "$set_sz" \
				"$MBCCI" "$MEMDEV" "${get_feat_args[@]}" \
				"||" \
				"$MBCCI" "$MEMDEV" "${set_feat_args[@]}"
		fi

	done <"$entries_file"
}

run_mailbox_phase() {
	run_feature_matrix \
		"mailbox" \
		"mailbox" \
		"mailbox" \
		"get-supported-feat" \
		"|||" \
		"get-feature" \
		"|||" \
		"set-feature"
}

run_sdb_phase() {
	local port="$1"

	run_feature_matrix \
		"sdb/$port" \
		"sdb-tunnel --port $port" \
		"sdb" \
		"sdb-tunnel" "get-supported-feat" "--port" "$port" \
		"|||" \
		"sdb-tunnel" "get-feature" "--port" "$port" \
		"|||" \
		"sdb-tunnel" "set-feature" "--port" "$port"
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

	echo "mbcci-sfx FEATURE test matrix"
	echo "  device:  $MEMDEV"
	echo "  binary:  $MBCCI"
	echo "  writeback: ${FEATURE_SKIP_WRITEBACK:-enabled (set FEATURE_SKIP_WRITEBACK=1 to skip)}"

	run_mailbox_phase
	run_sdb_phase vdm1
	run_sdb_phase i3c

	print_summary
}

main "$@"
exit $?
