#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Run mbcci-sfx Get/read verification for all transport paths:
#   - mailbox
#   - sdb-tunnel --port vdm1
#   - sdb-tunnel --port i3c
#
# Saves full run log and per-command logs under tests/logs/.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
READ_TESTS="$SCRIPT_DIR/mbcci-sfx-read-tests.sh"

LOG_ROOT="${LOG_ROOT:-$SCRIPT_DIR/logs}"
MEMDEV="${1:-mem0}"

usage() {
	cat <<EOF
Usage: $0 [memN]

Run mbcci-sfx read verification and save logs locally.

Arguments:
  memN          CXL device name (default: mem0)

Environment:
  LOG_ROOT      Base directory for logs (default: tests/logs)
  MBCCI_SFX     Path to mbcci-sfx binary (forwarded to read tests)

Output layout:
  \$LOG_ROOT/verification-YYYYMMDD-HHMMSS/
    run.log       full stdout/stderr transcript
    meta.txt      run metadata
    tests/        one log file per command (when supported)
  \$LOG_ROOT/latest -> most recent verification directory

Examples:
  sudo $0
  sudo $0 mem1
  LOG_ROOT=/tmp/mbcci-logs sudo $0
EOF
}

die() {
	echo "ERROR: $*" >&2
	exit 1
}

case "$MEMDEV" in
-h|--help)
	usage
	exit 0
	;;
esac

[ -x "$READ_TESTS" ] || die "read test script not found or not executable: $READ_TESTS"

RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/verification-$RUN_ID"
PER_TEST_LOG_DIR="$LOG_DIR/tests"

mkdir -p "$PER_TEST_LOG_DIR"

{
	echo "started: $(date -Is 2>/dev/null || date)"
	echo "memdev: $MEMDEV"
	echo "log_dir: $LOG_DIR"
	echo "project: $PROJECT_DIR"
	echo "host: $(hostname 2>/dev/null || echo unknown)"
	echo "user: $(whoami 2>/dev/null || echo unknown)"
} > "$LOG_DIR/meta.txt"

echo "mbcci-sfx verification"
echo "  device:   $MEMDEV"
echo "  log dir:  $LOG_DIR"
echo ""

export VERIFICATION_LOG_DIR="$PER_TEST_LOG_DIR"

set +e
"$READ_TESTS" "$MEMDEV" 2>&1 | tee "$LOG_DIR/run.log"
rc=${PIPESTATUS[0]}
set -e

{
	echo "finished: $(date -Is 2>/dev/null || date)"
	echo "exit_code: $rc"
} >> "$LOG_DIR/meta.txt"

ln -sfn "$LOG_DIR" "$LOG_ROOT/latest"

echo ""
echo "Logs saved under: $LOG_DIR"
echo "  run.log   full test output"
echo "  meta.txt  run metadata"
echo "  tests/    per-command logs"
echo "  latest    -> $LOG_ROOT/latest"

exit "$rc"
