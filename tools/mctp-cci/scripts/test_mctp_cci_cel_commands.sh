#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Walk the device CEL (Command Effects Log) and exercise each supported CCI
# opcode through mctp-cci. Failures on individual commands do not stop the run.
#
# Usage:
#   ./tests/scripts/test_mctp_cci_cel_commands.sh [nid] [eid] [build_dir] [output.md]
#
# Environment:
#   MCTP_CCI              path to mctp-cci binary
#   MCTP_CCI_TEST_DELAY      seconds between commands (default: 3.0)
#   MCTP_CCI_TEST_FAIL_DELAY seconds to wait after FAIL/TIMEOUT/ERROR (default: 10.0)
#   MCTP_CCI_TEST_RETRIES    extra attempts on transport timeout (default: 0)
#   MCTP_CCI_TEST_RETRY_DELAY  seconds before each retry (default: 2.0)

set -u

repo="$(cd "$(dirname "$0")/../.." && pwd)"
nid="${1:-0}"
eid="${2:-8}"
build_dir="${3:-$repo/build}"
out_md="${4:-mctp-cci-cmds-test-result.md}"
mctp_cci="${MCTP_CCI:-$build_dir/tools/mctp-cci/mctp-cci}"
cel_uuid="0da9c0b5bf414b788f7996b1623b3f17"
vendor_debug_uuid="5e1819d9-11a9-400c-811f-d60719403d86"
run_as=(sudo)

if [ "$(id -u)" -eq 0 ]; then
    run_as=()
fi

die() {
    echo "error: $*" >&2
    exit 1
}

[ -x "$mctp_cci" ] || die "$mctp_cci not found — run: meson compile -C $build_dir tools/mctp-cci/mctp-cci"
command -v python3 >/dev/null || die "python3 is required"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

supported_out="$tmp_dir/supported.txt"
cel_out="$tmp_dir/cel.txt"
results_json="$tmp_dir/results.json"

echo "==> Step 1: get supported log UUIDs"
step1_cmd=("${run_as[@]}" "$mctp_cci" "$nid" "$eid" logs supported)
echo ""
echo "============================================================"
echo "current case: [prep 1/2] Get Supported Logs"
printf 'Command: %q ' "${step1_cmd[@]}"
echo ""
echo "Output:"
if ! "${step1_cmd[@]}" >"$supported_out" 2>"$supported_out.err"; then
    cat "$supported_out.err" >&2
    die "logs supported failed"
fi
cat "$supported_out"

echo "  (prep delay ${MCTP_CCI_TEST_DELAY:-3}s before next step)"
sleep "${MCTP_CCI_TEST_DELAY:-3}"

echo "==> Step 2: get CEL"
step2_cmd=("${run_as[@]}" "$mctp_cci" "$nid" "$eid" logs get --uuid "$cel_uuid")
echo ""
echo "============================================================"
echo "current case: [prep 2/2] Get CEL"
printf 'Command: %q ' "${step2_cmd[@]}"
echo ""
echo "Output:"
if ! "${step2_cmd[@]}" >"$cel_out" 2>"$cel_out.err"; then
    cat "$cel_out.err" >&2
    die "logs get (CEL) failed"
fi
cat "$cel_out"

echo "==> Step 3: run mctp-cci for each CEL opcode"
python3 - "$nid" "$eid" "$mctp_cci" "$cel_uuid" "$vendor_debug_uuid" \
    "$supported_out" "$cel_out" "$results_json" "${run_as[@]}" <<'PY'
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone

nid, eid, mctp_cci, cel_uuid, vendor_debug_uuid = sys.argv[1:6]
supported_path, cel_path, results_path = sys.argv[6:9]
run_as = sys.argv[9:]

cmd_delay = float(os.environ.get("MCTP_CCI_TEST_DELAY", "3.0"))
fail_delay = float(os.environ.get("MCTP_CCI_TEST_FAIL_DELAY", "10.0"))
retry_count = int(os.environ.get("MCTP_CCI_TEST_RETRIES", "0"))
retry_delay = float(os.environ.get("MCTP_CCI_TEST_RETRY_DELAY", "2.0"))

CEL_RE = re.compile(
    r"^\[(\d+)\] offset 0x([0-9a-fA-F]+)\s*\n"
    r"Opcode: 0x([0-9a-fA-F]+)(?: \(([^)]+)\))?\s*\n"
    r"Command Effect: 0x([0-9a-fA-F]+)",
    re.MULTILINE,
)

OPCODE_NAMES = {
    0x0001: "Identify",
    0x0002: "Background Operation Status",
    0x0003: "Get Response Message Limit",
    0x0004: "Set Response Message Limit",
    0x0005: "Request Abort Background Operation",
    0x0100: "Get Event Records",
    0x0101: "Clear Event Records",
    0x0102: "Get Event Interrupt Policy",
    0x0103: "Set Event Interrupt Policy",
    0x0104: "Get MCTP Event Interrupt Policy",
    0x0105: "Set MCTP Event Interrupt Policy",
    0x0106: "Event Notification",
    0x0200: "Get FW Info",
    0x0201: "Transfer FW",
    0x0202: "Activate FW",
    0x0300: "Get Timestamp",
    0x0301: "Set Timestamp",
    0x0400: "Get Supported Logs",
    0x0401: "Get Log",
    0x0402: "Get Log Capabilities",
    0x0403: "Clear Log",
    0x0404: "Populate Log",
    0x0405: "Get Supported Logs Sub-List",
    0x0500: "Get Supported Features",
    0x0501: "Get Feature",
    0x0502: "Set Feature",
    0x4000: "Identify Memory Device",
    0x4100: "Get Partition Info",
    0x4101: "Set Partition Info",
    0x4200: "Get Health Info",
    0x4201: "Get Alert Configuration",
    0x4202: "Set Alert Configuration",
    0x4300: "Get Poison List",
    0x4301: "Inject Poison",
    0x4302: "Clear Poison",
    0x4400: "Sanitize",
    0x4401: "Secure Erase",
    0x4500: "Get Security State",
    0x4700: "Get SLD QoS Control",
    0x4701: "Set SLD QoS Control",
    0x4702: "Get SLD QoS Status",
    0x4800: "Get Dynamic Capacity Configuration",
    0x5100: "Identify Switch Device",
    0x5200: "Get Virtual CXL Switch Info",
    0x5400: "Get LD Info",
    0x5500: "Get Multi-Headed Info",
    0x5600: "Get DCD Info",
}


STATUS_EMOJI = {
    "PASS": "✅",
    "FAIL": "❌",
    "TIMEOUT": "⏱️",
    "ERROR": "⚠️",
    "SKIP": "⏭️",
}


def status_label(status):
    return f"{STATUS_EMOJI.get(status, '•')} **{status}**"


def status_label_plain(status):
    return f"{STATUS_EMOJI.get(status, '•')} {status}"


def cmd_prefix():
    return run_as + [mctp_cci, nid, eid]


def parse_cel(text):
    entries = []
    for m in CEL_RE.finditer(text):
        entries.append({
            "index": int(m.group(1)),
            "offset": int(m.group(2), 16),
            "opcode": int(m.group(3), 16),
            "name": m.group(4) or OPCODE_NAMES.get(int(m.group(3), 16), ""),
            "effect": int(m.group(5), 16),
        })
    if entries:
        return entries

    # Fallback: parse hex dump if user passed --hex output
    hex_bytes = []
    for tok in re.findall(r"[0-9a-fA-F]{2}", text):
        hex_bytes.append(int(tok, 16))
    for i in range(0, len(hex_bytes) - 3, 4):
        opcode = hex_bytes[i] | (hex_bytes[i + 1] << 8)
        effect = hex_bytes[i + 2] | (hex_bytes[i + 3] << 8)
        entries.append({
            "index": i // 4,
            "offset": i,
            "opcode": opcode,
            "name": OPCODE_NAMES.get(opcode, ""),
            "effect": effect,
        })
    return entries


def pick_alt_log(supported_text):
    """Return (uuid_hex_no_dash, log_size) for a non-CEL log entry."""
    cel_norm = cel_uuid.lower().replace("-", "")
    first_zero = None
    for line in supported_text.splitlines():
        m = re.search(
            r"\[[0-9]+\]\s+"
            r"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})"
            r"\s+log_size:\s*(\d+)",
            line,
            re.I,
        )
        if not m:
            continue
        uuid, size = m.group(1), int(m.group(2))
        uuid_hex = uuid.replace("-", "")
        if uuid_hex.lower() == cel_norm:
            continue
        if size > 0:
            return uuid_hex, size
        if first_zero is None:
            first_zero = (uuid_hex, size)
    if first_zero is not None:
        return first_zero
    return vendor_debug_uuid.replace("-", ""), 0


def map_opcode(opcode, alt_log_uuid_hex, alt_log_size):
  # Returns (argv_suffix, skip_reason). argv_suffix excludes mctp-cci nid eid.
    if opcode == 0x0001:
        return ["info", "identify"], None
    if opcode == 0x0002:
        return ["info", "bg-op-status"], None
    if opcode == 0x0003:
        return ["info", "get-resp-msg-limit"], None
    if opcode == 0x0004:
        return None, "Set Response Message Limit (0004h) not exposed in mctp-cci"
    if opcode == 0x0005:
        return ["info", "abort"], None
    if opcode == 0x0100:
        return ["events", "get", "info"], None
    if opcode == 0x0101:
        return ["events", "clear", "info", "--all"], None
    if opcode == 0x0102:
        return ["events", "get-policy"], None
    if opcode == 0x0103:
        return ["events", "set-policy"], None
    if opcode in (0x0104, 0x0105, 0x0106):
        return None, "MCTP event commands not exposed in mctp-cci"
    if opcode == 0x0200:
        return ["fw", "info"], None
    if opcode == 0x0201:
        return None, "Transfer FW requires --input <file>"
    if opcode == 0x0202:
        return None, "Activate FW skipped (destructive / needs valid slot)"
    if opcode == 0x0300:
        return ["ts", "get"], None
    if opcode == 0x0301:
        return ["ts", "set"], None
    if opcode == 0x0400:
        return ["logs", "supported"], None
    if opcode == 0x0401:
        if alt_log_size == 0:
            return None, (
                f"Get Log skipped (non-CEL log {alt_log_uuid_hex} has log_size=0)"
            )
        return ["logs", "get", "--uuid", alt_log_uuid_hex,
                "--length", str(alt_log_size)], None
    if opcode in (0x0402, 0x0403, 0x0404, 0x0405):
        return None, "Log admin commands not exposed in mctp-cci"
    if opcode == 0x0500:
        return ["features", "supported"], None
    if opcode == 0x0501:
        return None, "Get Feature requires <feature_id>"
    if opcode == 0x0502:
        return None, "Set Feature skipped (needs feature_id and may change state)"
    if opcode == 0x4000:
        return ["identify", "memdev"], None
    if opcode == 0x4100:
        return ["partition", "get"], None
    if opcode == 0x4101:
        return None, "Set Partition Info skipped (needs --next-volatile)"
    if opcode == 0x4200:
        return ["health", "info"], None
    if opcode == 0x4201:
        return ["health", "get-alert"], None
    if opcode == 0x4202:
        return ["health", "set-alert", "0"], None
    if opcode == 0x4300:
        return ["poison", "list"], None
    if opcode == 0x4301:
        return None, "Inject Poison skipped (needs phys_addr, side effects)"
    if opcode == 0x4302:
        return None, "Clear Poison skipped (needs phys_addr, side effects)"
    if opcode in (0x4400, 0x4401):
        return None, "Sanitize / Secure Erase skipped (destructive)"
    if opcode == 0x4500:
        return ["security", "state"], None
    if opcode in (0x4501, 0x4502, 0x4503, 0x4504, 0x4505):
        return None, "Security mutation commands skipped (need passphrase / side effects)"
    if opcode == 0x4700:
        return ["qos", "get-ctrl"], None
    if opcode == 0x4701:
        return ["qos", "set-ctrl", "0"], None
    if opcode == 0x4702:
        return ["qos", "get-status"], None
    if opcode == 0x4800:
        return ["dcd", "config"], None
    if opcode in (0x4801, 0x4802, 0x4803):
        return None, "DCD extent commands not exposed in mctp-cci"
    if opcode == 0x5100:
        return ["switch", "identify"], None
    if opcode == 0x5101:
        return ["switch", "port-state", "0"], None
    if opcode == 0x5200:
        return ["switch", "vcs-info"], None
    if opcode == 0x5400:
        return ["mld", "ld-info"], None
    if opcode == 0x5401:
        return ["mld", "ld-allocations"], None
    if opcode == 0x5500:
        return ["mld", "multiheaded"], None
    if opcode == 0x5600:
        return ["dcd-mgmt", "info"], None
    if opcode in (0x53CC, 0xCC53, 0xcc53):
        # CEL stores vendor ID 0xCC53 as little-endian uint16 → 0x53CC
        return None, "Vendor inject-event (0xCC53) skipped (side effects / needs unlock)"
    if opcode >= 0xC000:
        return None, f"Vendor opcode 0x{opcode:04x} has no mctp-cci mapping"
    return None, f"No mctp-cci mapping for opcode 0x{opcode:04x}"


def classify_result(rc, full_out):
    """mctp-cci maps all failures to exit code 1; inspect output for cause."""
    if rc == 0:
        return "PASS"
    text = full_out.lower()
    if "timed out" in text or "timeout on mctp" in text:
        return "TIMEOUT"
    if "libcxlmi:" in full_out:
        return "ERROR"
    if re.search(r": (connection|resource temporarily unavailable|network is down)",
                 full_out, re.I):
        return "TIMEOUT"
    return "FAIL"


def run_cmd_once(argv_suffix):
    cmd = cmd_prefix() + argv_suffix
    proc = subprocess.run(cmd, capture_output=True, text=True)
    full_out = (proc.stdout + proc.stderr).strip()
    note = full_out if len(full_out) <= 500 else full_out[:500] + "..."
    return proc.returncode, " ".join(cmd), full_out, note


def run_cmd(argv_suffix):
    rc, command, full_out, note = run_cmd_once(argv_suffix)
    status = classify_result(rc, full_out)
    attempt = 1
    while status == "TIMEOUT" and attempt <= retry_count:
        print(f"  (timeout, retry {attempt}/{retry_count} after {retry_delay}s)",
              flush=True)
        time.sleep(retry_delay)
        rc, command, full_out, note = run_cmd_once(argv_suffix)
        status = classify_result(rc, full_out)
        attempt += 1
    if attempt > 1 and status == "PASS":
        note = f"passed on retry {attempt - 1}; {note}"
    return rc, command, full_out, note, status


def print_case_header(case_num, total, entry):
    name = entry.get("name") or ""
    print("\n" + "=" * 60, flush=True)
    print(f"current case: [{case_num}/{total}]", flush=True)
    opcode_line = f"Opcode: 0x{entry['opcode']:04x}"
    if name:
        opcode_line += f" ({name})"
    print(opcode_line, flush=True)


def print_status(status, rc=None, extra=None):
    label = status_label_plain(status)
    if rc is not None:
        print(f"Status: {label} (rc={rc})", flush=True)
    else:
        line = f"Status: {label}"
        if extra:
            line += f" — {extra}"
        print(line, flush=True)


supported_text = open(supported_path, encoding="utf-8").read()
cel_text = open(cel_path, encoding="utf-8").read()
entries = parse_cel(cel_text)
if not entries:
    print("failed to parse CEL output", file=sys.stderr)
    sys.exit(1)

alt_log_uuid_hex, alt_log_size = pick_alt_log(supported_text)
results = []
seen = set()
total = len(entries)
case_num = 0

print(f"\n==> Step 3: run mctp-cci for each CEL opcode ({total} entries)", flush=True)
print(f"    inter-command delay={cmd_delay}s, fail delay={fail_delay}s, "
      f"timeout retries={retry_count}", flush=True)
if cmd_delay > 0:
    print(f"    cooldown {cmd_delay}s after prep before first opcode...", flush=True)
    time.sleep(cmd_delay)

for entry in entries:
    case_num += 1
    print_case_header(case_num, total, entry)

    opcode = entry["opcode"]
    if opcode in seen:
        print("Command: (skipped — duplicate opcode in CEL)", flush=True)
        print("Output: (not run)", flush=True)
        print_status("SKIP", extra="duplicate opcode in CEL")
        results.append({**entry, "status": "SKIP", "rc": None,
                        "command": "", "note": "duplicate opcode in CEL"})
        continue
    seen.add(opcode)

    argv_suffix, skip = map_opcode(opcode, alt_log_uuid_hex, alt_log_size)
    if skip:
        print(f"Command: (skipped — {skip})", flush=True)
        print("Output: (not run)", flush=True)
        print_status("SKIP", extra=skip)
        results.append({**entry, "status": "SKIP", "rc": None,
                        "command": "", "note": skip})
        continue

    if case_num > 1 and cmd_delay > 0:
        time.sleep(cmd_delay)

    print(f"Command: {' '.join(cmd_prefix() + argv_suffix)}", flush=True)
    rc, command, full_out, note, status = run_cmd(argv_suffix)
    print("Output:", flush=True)
    if full_out:
        print(full_out, flush=True)
    else:
        print("(no output)", flush=True)

    if status == "FAIL" and "success" in note.lower():
        note = re.sub(r"(?i)success", "CCI return code", note)
    print_status(status, rc=rc)
    results.append({**entry, "status": status, "rc": rc,
                    "command": command, "note": note})

    if status not in ("PASS", "SKIP") and fail_delay > 0:
        print(f"  (failed, waiting {fail_delay}s before next case)", flush=True)
        time.sleep(fail_delay)

payload = {
    "timestamp": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC"),
    "nid": nid,
    "eid": eid,
    "mctp_cci": mctp_cci,
    "cel_uuid": cel_uuid,
    "supported_logs": supported_text.strip(),
    "cel_raw": cel_text.strip(),
    "entries": entries,
    "results": results,
}
with open(results_path, "w", encoding="utf-8") as f:
    json.dump(payload, f, indent=2)

pass_n = sum(1 for r in results if r["status"] == "PASS")
fail_n = sum(1 for r in results if r["status"] == "FAIL")
err_n = sum(1 for r in results if r["status"] == "ERROR")
timeout_n = sum(1 for r in results if r["status"] == "TIMEOUT")
skip_n = sum(1 for r in results if r["status"] == "SKIP")
print("\n" + "=" * 60, flush=True)
print(f"Step 3 done: ✅ PASS={pass_n}  ❌ FAIL={fail_n}  ⏱️ TIMEOUT={timeout_n}  "
      f"⚠️ ERROR={err_n}  ⏭️ SKIP={skip_n}  TOTAL={total}", flush=True)
PY

echo "==> Step 4: write $out_md"
python3 - "$results_json" "$out_md" <<'PY'
import json
import sys

data = json.load(open(sys.argv[1], encoding="utf-8"))
out_path = sys.argv[2]

STATUS_EMOJI = {
    "PASS": "✅",
    "FAIL": "❌",
    "TIMEOUT": "⏱️",
    "ERROR": "⚠️",
    "SKIP": "⏭️",
}


def status_label(status):
    return f"{STATUS_EMOJI.get(status, '•')} **{status}**"

pass_n = sum(1 for r in data["results"] if r["status"] == "PASS")
fail_n = sum(1 for r in data["results"] if r["status"] == "FAIL")
err_n = sum(1 for r in data["results"] if r["status"] == "ERROR")
timeout_n = sum(1 for r in data["results"] if r["status"] == "TIMEOUT")
skip_n = sum(1 for r in data["results"] if r["status"] == "SKIP")
total = len(data["results"])

lines = []
lines.append("# mctp-cci CEL Command Test Results")
lines.append("")
lines.append(f"- **Time:** {data['timestamp']}")
lines.append(f"- **Target:** nid={data['nid']} eid={data['eid']}")
lines.append(f"- **Tool:** `{data['mctp_cci']}`")
lines.append(f"- **CEL UUID:** `{data['cel_uuid']}`")
lines.append("")
lines.append("## Summary")
lines.append("")
lines.append(f"| Metric | Count |")
lines.append(f"|--------|------:|")
lines.append(f"| Total CEL opcodes tested | {total} |")
lines.append(f"| {status_label('PASS')} | {pass_n} |")
lines.append(f"| {status_label('FAIL')} (CCI return code) | {fail_n} |")
lines.append(f"| {status_label('TIMEOUT')} (MCTP no response) | {timeout_n} |")
lines.append(f"| {status_label('ERROR')} (transport/client) | {err_n} |")
lines.append(f"| {status_label('SKIP')} | {skip_n} |")
lines.append("")
lines.append("## Supported Logs")
lines.append("")
lines.append("```")
lines.append(data["supported_logs"])
lines.append("```")
lines.append("")
lines.append("## CEL (Command Effects Log)")
lines.append("")
lines.append("```")
lines.append(data["cel_raw"])
lines.append("```")
lines.append("")
lines.append("## Per-Opcode Test Results")
lines.append("")
lines.append("| # | Opcode | Name | Effect | Status | RC | mctp-cci command | Notes |")
lines.append("|--:|--------|------|--------|--------|---:|------------------|-------|")
for r in data["results"]:
    name = r.get("name") or ""
    rc = "" if r["rc"] is None else str(r["rc"])
    cmd = r.get("command", "").replace("|", "\\|")
    note = r.get("note", "").replace("|", "\\|").replace("\n", " ")
    lines.append(
        f"| {r['index']} | `0x{r['opcode']:04x}` | {name} | `0x{r['effect']:04x}` | "
        f"{status_label(r['status'])} | {rc} | `{cmd}` | {note} |"
    )
lines.append("")
lines.append("## Legend")
lines.append("")
lines.append(f"- {status_label('PASS')} — mctp-cci exited 0 (CCI Success)")
lines.append(f"- {status_label('FAIL')} — CCI non-zero return code")
lines.append(f"- {status_label('TIMEOUT')} — MCTP poll timed out (`Connection timed out`); re-run single command manually to verify")
lines.append(f"- {status_label('ERROR')} — libcxlmi transport/parsing error (wrong response, length mismatch, etc.)")
lines.append(f"- {status_label('SKIP')} — not run (no mctp-cci mapping, destructive, or duplicate opcode)")
lines.append("")
lines.append("> Note: mctp-cci maps all failures to exit code 1; status is inferred from output text.")
lines.append("")

with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
print(f"Wrote {out_path}")
print(f"Summary: ✅ PASS={pass_n}  ❌ FAIL={fail_n}  ⏱️ TIMEOUT={timeout_n}  "
      f"⚠️ ERROR={err_n}  ⏭️ SKIP={skip_n}  TOTAL={total}")
PY
