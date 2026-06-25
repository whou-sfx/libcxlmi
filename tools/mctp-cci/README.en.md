# mctp-cci

> **Language / 语言**: [English (this file)](README.en.md) · [中文 (Chinese)](README.md)

`mctp-cci` is a command-line tool built on top of [libcxlmi](../..) that sends
CCI (Component Command Interface) commands to CXL (Compute Express Link)
components over MCTP (Management Component Transport Protocol). It is the
Swiss-army knife for `libcxlmi` in **out-of-band** scenarios: BMCs,
firmware, and Fabric Managers can use it to operate on CXL Type3 devices,
CXL Switches, and Multi-Logical-Device / Multi-Headed-Device (MLD/MHD)
topologies without depending on a host OS.

Source layout (`tools/mctp-cci/`):

- Entry point: `main.c`
- Dispatch tables: each `cmd_*.c` exports an `mctp_cci_top`; `main.c` wires
  them into a single static array
- Shared helpers: `util.c`
- Test scripts: `scripts/test_mctp_cci_traverse.sh`,
  `scripts/test_mctp_cci_cel_commands.sh`

---

## 1. Overview

### 1.1 What it does

- Enumerate and open local MCTP endpoints identified by
  `network-id : endpoint-id`
- Issue CCI commands defined by the CXL 3.x spec (default) or the CXL 2.0
  spec (built with `-Dcxl2_0_mode=enabled`)
- Reach downstream Logical Devices through CXL Switch / MLD tunnels
- Parse the CXL Component Effects Log (CEL) and automatically run every
  opcode the device advertises
- Send Vendor-Specific commands (currently the ScaleFlux VU Inject Event
  at `0xCC53`)

### 1.2 Relation to `cxl` (ndctl) and `cxl-fmapi-tests`

| Tool | Transport | Typical user |
|------|-----------|--------------|
| `cxl` (ndctl) | Linux kernel ioctl (in-band mailbox) | Host side |
| `cxl-fmapi-tests` | MCTP (libmctp) | Early FM/BMC scripts |
| **`mctp-cci`** | **MCTP (libcxlmi)** | **Unified BMC/firmware/FM toolchain entry point** |

`mctp-cci` is a one-to-one translation of the `libcxlmi` C API into a CLI,
giving consistent command naming, payload encoding/decoding, and
return-code-to-string conversion.

---

## 2. Building

`mctp-cci` is part of the `libcxlmi` build (see the top-level
[README.md](../../README.md)).

```bash
# At the root of the libcxlmi checkout
meson setup build
meson compile -C build
```

Output binary:

```
build/tools/mctp-cci/mctp-cci
```

No extra dependencies beyond `libcxlmi_dep` (libcxlmi + system MCTP stack).

> **Privileges**: `cxlmi_open_mctp()` requires `CAP_NET_RAW`. Run as root
> or with `sudo` in production.

---

## 3. Usage

### 3.1 Command-line format

```
mctp-cci <nid> <eid> <top> [sub] [args...]
mctp-cci -h | --help
mctp-cci <nid> <eid> <top> --help        # Print <top> help only, no endpoint open
```

- `<nid>`: MCTP network-id (0..0xff), decimal or hex
- `<eid>`: MCTP endpoint-id (0..0xff)
- `<top>`: top-level subcommand (19 total — see table below)

### 3.2 Global tunnel options

These options appear after `<top>` and are stripped by
`parse_tunnel_args()` (util.c:27) before being filled into
`cxlmi_tunnel_info`:

| Option | Meaning | Tunnel level |
|--------|---------|--------------|
| `--tunnel-port N`   | Address through CXL Switch downstream port N | 1 |
| `--tunnel-ld M`     | Address LD M inside an MLD                   | 1 |
| `--port-and-ld N,M` | First through switch port N, then LD M       | 2 |
| `--tunnel-mhd`      | Address the LD Pool CCI in a Multi-Headed Device | 1 |

Mutual exclusion:

- `--port-and-ld` cannot be combined with `--tunnel-port` / `--tunnel-ld`
- `--tunnel-mhd` cannot be combined with any of the other three

### 3.3 Top-level subcommand overview

| top | CCI opcode set | Description |
|-----|----------------|-------------|
| `info`       | 0x00 INFOSTAT         | Identify / BG-Op / Resp Msg Limit / Abort |
| `events`     | 0x01 EVENTS           | Event records, clear, interrupt policy |
| `fw`         | 0x02 FIRMWARE_UPDATE  | FW Info / Transfer / Activate |
| `ts`         | 0x03 TIMESTAMP        | Get / Set Timestamp |
| `logs`       | 0x04 LOGS             | Supported Logs / Get Log / Clear Log |
| `features`   | 0x05 FEATURES         | Supported / Get (Set via vendor path) |
| `identify`   | 0x4000 MEMDEV         | Memory Device Identify |
| `partition`  | 0x41 CCLS             | Get / Set Partition Info |
| `health`     | 0x42 HEALTH_ALERTS    | Health Info / Alert Config |
| `poison`     | 0x43 MEDIA_AND_POISON | List / Inject / Clear |
| `sanitize`   | 0x44 SANITIZE         | Sanitize / Secure Erase |
| `pmem`       | 0x45 PERSISTENT_MEM   | Placeholder — security subcommand lives here |
| `security`   | 0x46 SECURITY         | State / Set-Pass / Unlock / Freeze |
| `qos`        | 0x47 SLD_QOS          | Get/Set Control / Get Status |
| `dcd`        | 0x48 DCD_CONFIG       | Dynamic Capacity Config |
| `switch`     | 0x51 / 0x52           | Physical / Virtual Switch |
| `mld`        | 0x53 / 0x54 / 0x55    | MLD LD-Info / Allocations / Multi-Headed |
| `dcd-mgmt`   | 0x56 DCD_MANAGEMENT   | DCD Info |
| `vendor`     | C000h+                | Vendor-specific (currently VU Inject Event) |

---

## 4. Subcommand reference

> Each entry lists `<top> <sub> [args]`, the matching CCI opcode, the
> payload semantics, and example invocations.
> Convention: all numeric arguments accept decimal or `0x`-prefixed hex.

### 4.1 `info` — 0x00 INFOSTAT

| sub | opcode | Use |
|-----|--------|-----|
| `identify`           | 0x0001 | Return Vendor / Device / Serial / Type / ... |
| `bg-op-status`       | 0x0002 | Query background operation status |
| `get-resp-msg-limit` | 0x0003 | Get the response message limit |
| `abort`              | 0x0004 (the CEL test script uses 0x0005) | Request abort of a background operation |

```bash
mctp-cci 0 8 info identify
mctp-cci 0 8 info bg-op-status
```

### 4.2 `events` — 0x01 EVENTS

| sub | opcode | Usage |
|-----|--------|-------|
| `get`        | 0x0100 | `events get <log>`, log = info/warn/failure/fatal/dcd |
| `clear`      | 0x0101 | `events clear <log> [--all]` |
| `get-policy` | 0x0102 | Read the interrupt policy for all 4 severities |
| `set-policy` | 0x0103 | `set-policy [--info N] [--warn N] [--failure N] [--fatal N]` |

```bash
mctp-cci 0 8 events get warn
mctp-cci 0 8 events clear fatal --all
mctp-cci 0 8 events set-policy --fatal 0x3
```

### 4.3 `fw` — 0x02 FIRMWARE_UPDATE

| sub | opcode | Usage |
|-----|--------|-------|
| `info`     | 0x0200 | Slot count, capacities, FW Rev1..4 |
| `transfer` | 0x0201 | `transfer --input <file> --slot N [--action N] [--offset hex]` |
| `activate` | 0x0202 | `activate [slot]` |

```bash
mctp-cci 0 8 fw info
mctp-cci 0 8 fw transfer --input fw.bin --slot 1 --action 0
mctp-cci 0 8 fw activate 1
```

### 4.4 `ts` — 0x03 TIMESTAMP

| sub | opcode | Usage |
|-----|--------|-------|
| `get` | 0x0300 | Return device timestamp (ns) |
| `set` | 0x0301 | `set [<ns>]`; defaults to host `CLOCK_REALTIME` |

```bash
mctp-cci 0 8 ts get
mctp-cci 0 8 ts set 0
mctp-cci 0 8 ts set                      # use current host time
```

### 4.5 `logs` — 0x04 LOGS

| sub | opcode | Usage |
|-----|--------|-------|
| `supported` | 0x0400 | List all log UUIDs the device exposes, with size and name |
| `get`       | 0x0401 | `get --uuid <hex> [--offset N] [--length N] [--hex]` |
| `clear`     | 0x0402 | `clear <uuid>` (requires the log admin command set to be present) |

UUID format: 32 hex chars, or 8-4-4-4-12.

`logs get` automatically determines `length` from the `supported` response
unless an explicit value is given. If the UUID matches the CEL
(`0da9c0b5-bf41-4b78-8f79-96b1623b3f17`), `cmd_cel.c::print_cel_log`
renders each entry as `(index, opcode, command effect)` instead of raw hex.

```bash
mctp-cci 0 8 logs supported
mctp-cci 0 8 logs get --uuid 0da9c0b5-bf41-4b78-8f79-96b1623b3f17
mctp-cci 0 8 logs get --uuid b3fab4cf-01b6-4332-943e-5e9962f23567 --hex
mctp-cci 0 8 logs clear 5e1819d9-11a9-400c-811f-d60719403d86
```

### 4.6 `features` — 0x05 FEATURES

| sub | opcode | Usage |
|-----|--------|-------|
| `supported` | 0x0500 | `supported [start_idx [count_bytes]]` |
| `get`       | 0x0501 | `get <feature_id>` |

`count` defaults to 48 (= the size of one Supported Feature Entry).

```bash
mctp-cci 0 8 features supported
mctp-cci 0 8 features supported 0 192
mctp-cci 0 8 features get 0x0001     # feature_id is usually a UUID-like string
```

### 4.7 `identify` — 0x4000 MEMDEV

```bash
mctp-cci 0 8 identify memdev
```

Returns FW revision, total / volatile / persistent capacity, partition
alignment, info-event log size, and similar fields.

### 4.8 `partition` — 0x41 CCLS

```bash
mctp-cci 0 8 partition get
mctp-cci 0 8 partition set --next-volatile 8G --flags 0
```

`--next-volatile` accepts `K` / `M` / `G` suffixes (see
`parse_size_with_unit`).

### 4.9 `health` — 0x42 HEALTH_ALERTS

```bash
mctp-cci 0 8 health info
mctp-cci 0 8 health get-alert
mctp-cci 0 8 health set-alert 0x0
```

### 4.10 `poison` — 0x43 MEDIA_AND_POISON

```bash
mctp-cci 0 8 poison list                       # list existing records
mctp-cci 0 8 poison list 0x1000 0x1000          # constrain by address range
mctp-cci 0 8 poison inject 0x10000000          # inject poison
mctp-cci 0 8 poison clear 0x10000000 00        # clear and (optionally) write data
```

The optional second argument to `clear` is a hex string of up to 64 bytes
used as the write-data payload.

### 4.11 `sanitize` — 0x44

```bash
mctp-cci 0 8 sanitize sanitize
mctp-cci 0 8 sanitize secure-erase
```

> ⚠️ Both commands are destructive to device data. Use with care in
> production environments.

### 4.12 `pmem`

Placeholder top — security subcommands are filed under `security`.

### 4.13 `security` — 0x46

```bash
mctp-cci 0 8 security state
mctp-cci 0 8 security set-pass <old> <new>
mctp-cci 0 8 security unlock  <passphrase>
mctp-cci 0 8 security freeze
```

Passphrases are capped at 0x20 bytes.

### 4.14 `qos` — 0x47 SLD_QOS

```bash
mctp-cci 0 8 qos get-ctrl
mctp-cci 0 8 qos set-ctrl 0x1
mctp-cci 0 8 qos get-status
```

### 4.15 `dcd` — 0x48 DCD_CONFIG

```bash
mctp-cci 0 8 dcd config
```

Prints region/extent/tag capacity statistics.

### 4.16 `switch` — 0x51 / 0x52

```bash
mctp-cci 0 8 switch identify
mctp-cci 0 8 switch port-state 3
mctp-cci 0 8 switch vcs-info
```

- `port-state <port>`: state of the named physical port
- `vcs-info`: bulk VCS / vPPB topology (limited to 16 VCS, 32 vPPB)

### 4.17 `mld` — 0x53 / 0x54 / 0x55

```bash
mctp-cci 0 8 mld ld-info
mctp-cci 0 8 mld ld-allocations
mctp-cci 0 8 mld multiheaded
```

Requires `--tunnel-ld M` to address a specific LD. On a Multi-Headed
Device, `multiheaded` also needs `--tunnel-mhd` or `--port-and-ld`.

### 4.18 `dcd-mgmt` — 0x56

```bash
mctp-cci 0 8 dcd-mgmt info
```

Returns host count, DC region count, policy masks, etc. Requires
FM-API to be enabled on the endpoint.

### 4.19 `vendor` — Vendor-Specific (0xCC53)

The current implementation wraps the ScaleFlux VU mailbox:

```bash
mctp-cci 0 8 vendor inject-event --loglvl 1 --intfmask 0x4 --count 1
```

- `loglvl`: 0=Info, 1=Warn, 2=Fail, 3=Fatal
- `intfmask`: interrupt / interface mask (hex)
- `count`: number of events to inject (must be > 0)

The tool automatically performs `vu-unlock` → `inject` → `vu-lock` around
the event injection.

---

## 5. Return values and error handling

```c
rc == 0            // success
rc < 0             // libcxlmi transport / parse error; errno is meaningful
rc > 0             // CCI return code (CXLMI_RET_xxx), converted to a string
                   // via cxlmi_cmd_retcode_tostr()
```

`mctp-cci` collapses these into shell exit codes:

| Internal rc | Shell exit |
|-------------|------------|
| 0           | 0          |
| anything else | 1        |
| 2 (usage error from a subcommand) | 2 (the test scripts distinguish this) |

> `mctp_cci_report_libcxlmi_error()` (util.c:10) translates `errno` into
> a human-readable line on stderr, and is used uniformly by every
> subcommand.

---

## 6. Test scripts

Two scripts live in `scripts/`:

### 6.1 `test_mctp_cci_traverse.sh`

Smoke test — does **not** need a real MCTP endpoint:

- Verifies the output of `mctp-cci --help` and every `<top> --help`
- Exercises argument-parser error paths (invalid nid/eid, unknown top,
  out-of-range values)
- Cross-checks that every entry declared in any `cmd_*.c::cmds[]` is
  also listed in `--help`

```bash
./tools/mctp-cci/scripts/test_mctp_cci_traverse.sh build
```

### 6.2 `test_mctp_cci_cel_commands.sh`

Hardware-in-the-loop test that drives the **full** chain:

1. `logs supported` — fetch the list of supported logs
2. `logs get --uuid <cel>` — pull the CEL
3. Parse each CEL entry, map its opcode to a `mctp-cci` subcommand, and
   run it
4. Classify the result as PASS / FAIL / TIMEOUT / ERROR / SKIP
5. Emit a markdown report

Environment variables:

| Variable | Default | Purpose |
|----------|---------|---------|
| `MCTP_CCI`                 | `build/tools/mctp-cci/mctp-cci` | Path to the `mctp-cci` binary |
| `MCTP_CCI_TEST_DELAY`      | 3.0  | Cooldown between commands (s) |
| `MCTP_CCI_TEST_FAIL_DELAY` | 10.0 | Extra wait after FAIL/TIMEOUT/ERROR (s) |
| `MCTP_CCI_TEST_RETRIES`    | 0    | Number of retries on MCTP timeout |
| `MCTP_CCI_TEST_RETRY_DELAY`| 2.0  | Delay before each retry (s) |

```bash
sudo ./tools/mctp-cci/scripts/test_mctp_cci_cel_commands.sh 0 8 build my-result.md
```

The generated `my-result.md` contains:

- Aggregate statistics (PASS / FAIL / TIMEOUT / ERROR / SKIP)
- Raw `supported` and CEL output
- One row per opcode with the exact command, echoed output, status, and a
  free-form note

The script owns an `OPCODE_NAMES` table and a `map_opcode()` function
(test_mctp_cci_cel_commands.sh:235) that translates each CCI opcode into
`mctp-cci` arguments. Whenever you add a new CCI subcommand to
`mctp-cci`, update this script in lockstep.

---

## 7. Internal architecture

```
                    main.c
                      │
       cxlmi_open_mctp(nid, eid)
                      │
                parse_tunnel_args
                      │
                  find_top / find_cmd
                      │
                  cmd_*.c::fn
                      │
                  libcxlmi (cxlmi_cmd_*)
```

- **Dispatch table**: each `cmd_*.c` exports a `const struct mctp_cci_top
  xxx_top`; `main.c:9` collects them into a single static `tops[]` array.
- **Error convention**: every subcommand funnels libcxlmi failures
  through `mctp_cci_report_libcxlmi_error()` (util.c:10).
- **Argument parsing**: numeric arguments go through
  `strtoul(..., NULL, 0)` (decimal or hex). Sizes with `K`/`M`/`G`
  suffixes go through `parse_size_with_unit()` (util.c:111).
- **CEL parser**: `cmd_cel.c` ships with an 80+ entry opcode → name
  dictionary and renders the Command Effect bits as
  *configuration change*, *background operation*, *security state
  change*, etc.

### 7.1 Adding a new subcommand

1. In a new `cmd_xxx.c`, write `static int do_xxx(...)`.
2. Add the function to the matching `static const struct mctp_cci_cmd
   xxx_cmds[]`.
3. Export `const struct mctp_cci_top xxx_top`.
4. Append `&xxx_top` to `main.c::tops[]`.
5. Add `'cmd_xxx.c'` to `meson.build`.
6. Rebuild with `meson compile -C build`.
7. Keep `test_mctp_cci_cel_commands.sh::map_opcode()` and
   `test_mctp_cci_traverse.sh::expected[]` in sync.

See also: [docs/How-To-Add-A-Command.md](../../docs/How-To-Add-A-Command.md).

---

## 8. Known limitations

- 0x0004 Set Response Message Limit is not yet exposed (libcxlmi level).
- 0x0104 / 0x0105 / 0x0106 MCTP Event subcommands are not exposed.
- 0x0402–0x0405 Log Admin subcommands are not exposed.
- 0x4400 / 0x4401 Sanitize / Secure Erase are exposed but the CEL test
  script skips them by default (destructive).
- 0x4801–0x4803 DCD Extent subcommands are not exposed.
- 0x5300–0x5302 Tunnel Management is not exposed.
- The vendor channel currently implements only the ScaleFlux VU Inject
  Event (`0xCC53`).

---

## 9. See also

- [libcxlmi top-level README](../../README.md)
- [Generic Component Commands](../../docs/Generic-Component-Commands.md)
- [Memory Device Commands](../../docs/Memory-Device-Commands.md)
- [FM-API](../../docs/FM-API.md)
- [Vendor Specific Commands](../../docs/Vendor-Specific-Commands.md)
- [How To Add A Command](../../docs/How-To-Add-A-Command.md)
- [Testing](../../docs/Testing.md)
- CXL r3.x specification (CXL.io / CXL.mem / CXL.cache / CXL Fabric)
- DSP0281 — CXL Type3 CCI over MCTP Binding Spec
- DSP0324 — CXL FM-API over MCTP Binding Spec
