#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Traverse every CCI command exposed by the mctp-cci CLI tool.
#
# mctp-cci opens a real MCTP endpoint before executing a subcommand, so
# this script only walks paths that run *before* cxlmi_open_mctp():
#   * `mctp-cci --help`           — full usage list (main.c:76)
#   * `mctp-cci N E <top> --help` — per-top list (main.c:101, returns early)
# plus the shared argument-validation error paths.
#
# Usage: ./tests/scripts/test_mctp_cci_traverse.sh [build_dir]
# Default build_dir is "$repo/build".

set -u

repo="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="${1:-$repo/build}"
mctp_cci="$build_dir/tools/mctp-cci/mctp-cci"
tools_dir="$repo/tools/mctp-cci"

pass=0; fail=0; total=0
ok()     { total=$((total+1)); pass=$((pass+1)); echo "ok $total - $1"; }
not_ok() { total=$((total+1)); fail=$((fail+1)); echo "not ok $total - $1"; }
br()     { echo "---"; }

assert_eq()  { if [ "$1" = "$2" ];  then ok "$3"; else not_ok "$3 (got '$1' want '$2')"; fi; }
assert_neq() { if [ "$1" != "$2" ];  then ok "$3"; else not_ok "$3 (both '$1')"; fi; }
assert_has() { if printf '%s' "$1" | grep -qE "$2"; then ok "$3"; else not_ok "$3 (missing '$2')"; fi; }

if [ ! -x "$mctp_cci" ]; then
    echo "1..1"
    not_ok "$mctp_cci missing — pass a build dir or run 'meson compile -C build'"
    exit 1
fi

echo "TAP version 13"
br

out_help="$("$mctp_cci" --help 2>&1)"; rc=$?
assert_eq "$rc" 0 "--help exits 0"
assert_has "$out_help" '^Usage:' "--help prints Usage line"
assert_has "$out_help" 'Top-level subcommands' "--help lists top-level subcommands"

br
echo "# Per-top enumeration"

# top_name|help_substr|subcmd1|subcmd2|...  (subs empty => no subcommands)
expected=(
    'info|INFOSTAT \(0x00\)|identify|bg-op-status|get-resp-msg-limit|abort'
    'events|EVENTS \(0x01\)|get|clear|get-policy|set-policy'
    'fw|FIRMWARE_UPDATE \(0x02\)|info|transfer|activate'
    'ts|TIMESTAMP \(0x03\)|get|set'
    'logs|LOGS \(0x04\)|supported|get|clear'
    'features|FEATURES \(0x05\)|supported|get'
    'identify|Memory Device Identify \(0x4000\)|memdev'
    'partition|CCLS \(0x41\)|get|set'
    'health|HEALTH_INFO_ALERTS \(0x42\)|info|get-alert|set-alert'
    'poison|MEDIA_AND_POISON \(0x43\)|list|inject|clear'
    'sanitize|SANITIZE \(0x44\)|sanitize|secure-erase'
    'pmem|PERSISTENT_MEM \(0x45\)'
    'security|SECURITY \(0x46\)|state|set-pass|unlock|freeze'
    'qos|SLD_QOS_TELEMETRY \(0x47\)|get-ctrl|set-ctrl|get-status'
    'dcd|DCD_CONFIG \(0x48\)|config'
    'switch|PHYSICAL_SWITCH \(0x51\).*VIRTUAL_SWITCH \(0x52\)|identify|port-state|vcs-info'
    'mld|MLD \(0x53/0x54/0x55\)|ld-info|ld-allocations|multiheaded'
    'dcd-mgmt|DCD_MANAGEMENT \(0x56\)|info'
    'vendor|Vendor-specific|inject-event'
)

for entry in "${expected[@]}"; do
    IFS='|' read -r top help_re subs <<<"$entry"

    if printf '%s' "$out_help" | grep -Eq "^  $top +$help_re"; then
        ok "main --help lists top '$top'"
    else
        not_ok "main --help missing top '$top' (help_re=$help_re)"
    fi

    top_out="$("$mctp_cci" 0 0 "$top" --help 2>&1)"; rc=$?
    assert_eq "$rc" 0 "mctp-cci 0 0 $top --help exits 0"
    assert_has "$top_out" "^  $top " "per-top --help lists '$top' name"

    if [ -z "$subs" ]; then
        if printf '%s' "$top_out" | grep -qE '^    '; then
            not_ok "'$top' should have no subcommands but help shows some"
        else
            ok "'$top' correctly has no subcommands"
        fi
        continue
    fi

    IFS='|' read -ra subarr <<<"$subs"
    for sub in "${subarr[@]}"; do
        if printf '%s' "$top_out" | grep -Eq "^    $sub "; then
            ok "$top $sub listed in per-top help"
        else
            not_ok "$top $sub NOT listed in per-top help (out: $top_out)"
        fi
    done

    got_n=$(printf '%s' "$top_out" | grep -cE '^    [^ ]' || true)
    want_n=${#subarr[@]}
    assert_eq "$got_n" "$want_n" "$top has exactly $want_n subcommand(s)"
done

br
echo "# Error-path coverage"

out_no_args="$("$mctp_cci" 2>&1)"; rc=$?
assert_neq "$rc" 0 "no-args invocation returns non-zero"
assert_has "$out_no_args" 'Usage:' "no-args prints usage to stderr"

out_no_args="$("$mctp_cci" 0 2>&1)"; rc=$?
assert_neq "$rc" 0 "only-nid invocation returns non-zero"

out_no_args="$("$mctp_cci" 0 0 2>&1)"; rc=$?
assert_neq "$rc" 0 "nid-eid-only invocation returns non-zero"

out_bad="$("$mctp_cci" zz 0 info 2>&1)"; rc=$?
assert_neq "$rc" 0 "non-numeric nid rejected"
assert_has "$out_bad" 'invalid <nid>' "error mentions nid"

out_bad="$("$mctp_cci" 0 zz info 2>&1)"; rc=$?
assert_neq "$rc" 0 "non-numeric eid rejected"
assert_has "$out_bad" 'invalid <eid>' "error mentions eid"

out_bad="$("$mctp_cci" 100 info --help 2>&1)"; rc=$?
assert_neq "$rc" 0 "nid > 0xff rejected"

out_bad="$("$mctp_cci" 0 100 info 2>&1)"; rc=$?
assert_neq "$rc" 0 "eid > 0xff rejected"

out_bad="$("$mctp_cci" 0 0 bogus 2>&1)"; rc=$?
assert_neq "$rc" 0 "unknown top returns non-zero"
assert_has "$out_bad" 'Unknown top-level subcommand: bogus' "unknown top error message"

# find_cmd runs AFTER cxlmi_open_mctp succeeds, so unknown-sub dispatch
# requires a real MCTP kernel endpoint — skip on systems without one.
if ls /dev/mctp* >/dev/null 2>&1; then
    out_bad="$("$mctp_cci" 0 0 info bogus-sub 2>&1)"
    if printf '%s' "$out_bad" | grep -qE 'Unknown subcommand|cannot open MCTP endpoint'; then
        ok "MCTP present — subcommand dispatch reaches find_cmd / endpoint_open"
    else
        not_ok "unexpected output for unknown sub: $out_bad"
    fi
else
    ok "# SKIP unknown-sub test requires /dev/mctp[N] device"
fi

br
echo "# Tunnel argument surface (help-documentation only)"
# parse_tunnel_args runs after cxlmi_open_mctp succeeds (main.c:118), so
# without hardware we can only verify --help documents these flags.

assert_has "$out_help" '\-\-tunnel-port' "--help documents --tunnel-port"
assert_has "$out_help" '\-\-tunnel-ld' "--help documents --tunnel-ld"
assert_has "$out_help" '\-\-port-and-ld' "--help documents --port-and-ld"
assert_has "$out_help" '\-\-tunnel-mhd' "--help documents --tunnel-mhd"

br
echo "# Source cross-check: cmd_*.c declared subcommands <-> --help"

# { "name", "help", fn } in dispatch tables must appear in --help.
for cmd_file in "$tools_dir"/cmd_*.c; do
    base="$(basename "$cmd_file" .c)"
    while IFS= read -r name; do
        if grep -qE "^    $name " <<<"$out_help"; then
            ok "$base declares '$name' and it is listed in --help"
        else
            not_ok "$base declares '$name' but it is MISSING from --help"
        fi
    done < <(
        perl -ne 'while (/\{\s*"([^"]+)"\s*,\s*"[^"]+"\s*,\s*\w+\s*\}/g) { print "$1\n" }' "$cmd_file"
    )
done

br
echo "1..$total"
echo "# $pass passed, $fail failed, $total total"

[ "$fail" -eq 0 ]