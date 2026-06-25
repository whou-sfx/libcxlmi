# mctp-cci

> **Language / 语言**: [English](README.en.md) · [中文 (this file)](README.md)

`mctp-cci` 是基于 [libcxlmi](../..) 的命令行工具，用于通过 MCTP (Management
Component Transport Protocol) 通道向 CXL (Compute Express Link) 组件发送
CCI (Component Command Interface) 命令。它是 `libcxlmi` 库在
**带外 (Out-of-Band)** 场景下的瑞士军刀：BMC、固件、Fabric Manager 等
管理者可借助它在不依赖主机 OS 的情况下操控 CXL Type3 设备、CXL Switch
以及多 Logical Device (MLD/MHD) 结构。

源码目录：`tools/mctp-cci/`

- 入口：`main.c`
- 调度表：每个 `cmd_*.c` 导出一个 `mctp_cci_top`，由 `main.c` 串成静态数组
- 共享工具：`util.c`
- 测试脚本：`scripts/test_mctp_cci_traverse.sh`、`scripts/test_mctp_cci_cel_commands.sh`

---

## 1. 概述

### 1.1 它能做什么

- 枚举并打开本地 MCTP 端点 (network-id : endpoint-id)
- 发送 CXL 3.x (默认) 或 CXL 2.0 (`-Dcxl2_0_mode=enabled`) 规范定义的 CCI 命令
- 通过 Switch / MLD 隧道对下游 LD (Logical Device) 寻址
- 解析 CXL Component Effects Log (CEL) 并把每个支持的 opcode 自动跑一遍
- 解析 Vendor-Specific 命令 (如 ScaleFlux VU Inject Event `0xCC53`)

### 1.2 与 `cxl` (ndctl) / `cxl-fmapi-tests` 的关系

| 工具 | 传输路径 | 适用场景 |
|------|----------|----------|
| `cxl` (ndctl) | Linux 内核 ioctl (in-band mailbox) | 主机侧 |
| `cxl-fmapi-tests` | MCTP (libmctp) | FM/BMC 早期脚本 |
| **`mctp-cci`** | **MCTP (libcxlmi)** | **BMC/固件/FM 工具链统一入口** |

`mctp-cci` 把 `libcxlmi` 的 C API 一对一翻译为 CLI，统一了命令命名、
payload 编解码和返回码到字符串的转换。

---

## 2. 编译

`mctp-cci` 是 `libcxlmi` 构建系统的一部分（见顶层 [README.md](../../README.md)）。

```bash
# 在 libcxlmi 根目录
meson setup build
meson compile -C build
```

二进制输出路径：

```
build/tools/mctp-cci/mctp-cci
```

不要求额外依赖；只要 `libcxlmi_dep` 能解析即可（mctp、libcxlmi 系统库）。

> **运行权限**：`cxlmi_open_mctp()` 需要 `CAP_NET_RAW`，实际使用时请以
> root 或 `sudo` 运行。

---

## 3. 用法

### 3.1 命令行格式

```
mctp-cci <nid> <eid> <top> [sub] [args...]
mctp-cci -h | --help
mctp-cci <nid> <eid> <top> --help        # 仅打印 <top> 帮助，不打开端点
```

- `<nid>`：MCTP network-id (0..0xff)，支持十进制/十六进制
- `<eid>`：MCTP endpoint-id (0..0xff)
- `<top>`：一级子命令分组（共 19 个，见下表）

### 3.2 全局隧道选项

下列选项出现在 `<top>` 之后，会被 `parse_tunnel_args()` (util.c:27) 提前
剥离并填入 `cxlmi_tunnel_info`：

| 选项 | 含义 | tunnel level |
|------|------|--------------|
| `--tunnel-port N` | 通过 CXL Switch downstream port N 寻址 | 1 |
| `--tunnel-ld M`   | 寻址 MLD 内的 LD M | 1 |
| `--port-and-ld N,M` | 先经 switch port N，再寻址 LD M | 2 |
| `--tunnel-mhd`    | 寻址 Multi-Headed Device 的 LD Pool CCI | 1 |

互斥规则：

- `--port-and-ld` 不能与 `--tunnel-port` / `--tunnel-ld` 同时使用
- `--tunnel-mhd` 不能与其它三个同时使用

### 3.3 一级子命令总览

| top | CCI opcode 集 | 说明 |
|-----|---------------|------|
| `info`       | 0x00 INFOSTAT         | Identify / BG-Op / Resp Msg Limit / Abort |
| `events`     | 0x01 EVENTS           | 事件记录、清除、中断策略 |
| `fw`         | 0x02 FIRMWARE_UPDATE  | FW Info / Transfer / Activate |
| `ts`         | 0x03 TIMESTAMP        | Get / Set Timestamp |
| `logs`       | 0x04 LOGS             | Supported Logs / Get Log / Clear Log |
| `features`   | 0x05 FEATURES         | Supported / Get / (Set 走 vendor 路径) |
| `identify`   | 0x4000 MEMDEV         | Memory Device Identify |
| `partition`  | 0x41 CCLS             | Get / Set Partition Info |
| `health`     | 0x42 HEALTH_ALERTS    | Health Info / Alert Config |
| `poison`     | 0x43 MEDIA_AND_POISON | List / Inject / Clear |
| `sanitize`   | 0x44 SANITIZE         | Sanitize / Secure Erase |
| `pmem`       | 0x45 PERSISTENT_MEM   | 占位 — security 子命令入口 |
| `security`   | 0x46 SECURITY         | State / Set-Pass / Unlock / Freeze |
| `qos`        | 0x47 SLD_QOS          | Get/Set Control / Get Status |
| `dcd`        | 0x48 DCD_CONFIG       | Dynamic Capacity Config |
| `switch`     | 0x51 / 0x52           | Physical / Virtual Switch |
| `mld`        | 0x53 / 0x54 / 0x55    | MLD LD-Info / Allocations / Multi-Headed |
| `dcd-mgmt`   | 0x56 DCD_MANAGEMENT   | DCD Info |
| `vendor`     | C000h+                | 厂商自定义 (当前实现 VU Inject Event) |

---

## 4. 命令详解

> 以下每条都列出：`<top> <sub> [args]`、对应 CCI opcode、payload 说明与示例。
> 通用约定：所有数值参数均支持十进制或 `0x` 前缀十六进制。

### 4.1 `info` — 0x00 INFOSTAT

| sub | opcode | 用途 |
|-----|--------|------|
| `identify`           | 0x0001 | 返回 Vendor/Device/Serial/Type 等 |
| `bg-op-status`       | 0x0002 | 查询后台命令状态 |
| `get-resp-msg-limit` | 0x0003 | 获取响应消息限值 |
| `abort`              | 0x0004 (注：测试脚本当作 0x0005) | 请求中止后台操作 |

```bash
mctp-cci 0 8 info identify
mctp-cci 0 8 info bg-op-status
```

### 4.2 `events` — 0x01 EVENTS

| sub | opcode | 用法 |
|-----|--------|------|
| `get`        | 0x0100 | `events get <log>`，log = info/warn/failure/fatal/dcd |
| `clear`      | 0x0101 | `events clear <log> [--all]` |
| `get-policy` | 0x0102 | 读 4 类事件的中断策略 |
| `set-policy` | 0x0103 | `set-policy [--info N] [--warn N] [--failure N] [--fatal N]` |

```bash
mctp-cci 0 8 events get warn
mctp-cci 0 8 events clear fatal --all
mctp-cci 0 8 events set-policy --fatal 0x3
```

### 4.3 `fw` — 0x02 FIRMWARE_UPDATE

| sub | opcode | 用法 |
|-----|--------|------|
| `info`     | 0x0200 | 槽位数、容量、FW Rev1..4 |
| `transfer` | 0x0201 | `transfer --input <file> --slot N [--action N] [--offset hex]` |
| `activate` | 0x0202 | `activate [slot]` |

```bash
mctp-cci 0 8 fw info
mctp-cci 0 8 fw transfer --input fw.bin --slot 1 --action 0
mctp-cci 0 8 fw activate 1
```

### 4.4 `ts` — 0x03 TIMESTAMP

| sub | opcode | 用法 |
|-----|--------|------|
| `get` | 0x0300 | 返回设备时间戳 (ns) |
| `set` | 0x0301 | `set [<ns>]`；缺省取 host `CLOCK_REALTIME` |

```bash
mctp-cci 0 8 ts get
mctp-cci 0 8 ts set 0
mctp-cci 0 8 ts set                      # 用主机当前时间
```

### 4.5 `logs` — 0x04 LOGS

| sub | opcode | 用法 |
|-----|--------|------|
| `supported` | 0x0400 | 列出设备所有 log 的 UUID、log_size、名称 |
| `get`       | 0x0401 | `get --uuid <hex> [--offset N] [--length N] [--hex]` |
| `clear`     | 0x0402 | `clear <uuid>` (注意 0x04/0x40 内命令集需设备支持) |

UUID 输入格式：32 hex 或 8-4-4-4-12 形式。

`logs get` 默认会根据 `supported` 自动确定 `length`；如果 UUID 命中 CEL
(`0da9c0b5-bf41-4b78-8f79-96b1623b3f17`)，`cmd_cel.c::print_cel_log` 会
以可读格式打印 (entry index / opcode / effect 字段含义)。

```bash
mctp-cci 0 8 logs supported
mctp-cci 0 8 logs get --uuid 0da9c0b5-bf41-4b78-8f79-96b1623b3f17
mctp-cci 0 8 logs get --uuid b3fab4cf-01b6-4332-943e-5e9962f23567 --hex
mctp-cci 0 8 logs clear 5e1819d9-11a9-400c-811f-d60719403d86
```

### 4.6 `features` — 0x05 FEATURES

| sub | opcode | 用法 |
|-----|--------|------|
| `supported` | 0x0500 | `supported [start_idx [count_bytes]]` |
| `get`       | 0x0501 | `get <feature_id>` |

`count` 缺省为 48 (=1 个 Supported Feature Entry 的大小)。

```bash
mctp-cci 0 8 features supported
mctp-cci 0 8 features supported 0 192
mctp-cci 0 8 features get 0x0001     # 实际 feature_id 通常是 UUID-like 字符串
```

### 4.7 `identify` — 0x4000 MEMDEV

```bash
mctp-cci 0 8 identify memdev
```

返回 FW Revision、Total/Volatile/Persistent Capacity、Partition Alignment
等。

### 4.8 `partition` — 0x41 CCLS

```bash
mctp-cci 0 8 partition get
mctp-cci 0 8 partition set --next-volatile 8G --flags 0
```

`--next-volatile` 接受 `K`/`M`/`G` 后缀（参见 `parse_size_with_unit`）。

### 4.9 `health` — 0x42 HEALTH_ALERTS

```bash
mctp-cci 0 8 health info
mctp-cci 0 8 health get-alert
mctp-cci 0 8 health set-alert 0x0
```

### 4.10 `poison` — 0x43 MEDIA_AND_POISON

```bash
mctp-cci 0 8 poison list                       # 列出已记录
mctp-cci 0 8 poison list 0x1000 0x1000          # 指定地址范围
mctp-cci 0 8 poison inject 0x10000000          # 注入 poison
mctp-cci 0 8 poison clear 0x10000000 00        # 清除并写入数据
```

`clear` 第二个参数为可选的 hex 字符串 (最多 64 字节) 写入值。

### 4.11 `sanitize` — 0x44

```bash
mctp-cci 0 8 sanitize sanitize
mctp-cci 0 8 sanitize secure-erase
```

> ⚠️ 这两条命令对设备数据具有破坏性。生产环境慎用。

### 4.12 `pmem`

占位 top，实际命令在 `security` 下。

### 4.13 `security` — 0x46

```bash
mctp-cci 0 8 security state
mctp-cci 0 8 security set-pass <old> <new>
mctp-cci 0 8 security unlock  <passphrase>
mctp-cci 0 8 security freeze
```

passphrase 长度上限 0x20 字节。

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

输出 region/extent/tag 的容量统计。

### 4.16 `switch` — 0x51 / 0x52

```bash
mctp-cci 0 8 switch identify
mctp-cci 0 8 switch port-state 3
mctp-cci 0 8 switch vcs-info
```

- `port-state <port>`：查询指定物理端口状态
- `vcs-info`：批量返回 VCS / vPPB 拓扑（限制 16 VCS、32 vPPB）

### 4.17 `mld` — 0x53 / 0x54 / 0x55

```bash
mctp-cci 0 8 mld ld-info
mctp-cci 0 8 mld ld-allocations
mctp-cci 0 8 mld multiheaded
```

需要 `--tunnel-ld M` 才能定位到具体 LD；`multiheaded` 在 MHD 上需要
`--tunnel-mhd` 或 `--port-and-ld` 组合。

### 4.18 `dcd-mgmt` — 0x56

```bash
mctp-cci 0 8 dcd-mgmt info
```

返回 host 数、DC region 数、policy mask 等。需要 FM-API 启用。

### 4.19 `vendor` — Vendor-Specific (0xCC53)

当前实现 ScaleFlux 私有 VU mailbox：

```bash
mctp-cci 0 8 vendor inject-event --loglvl 1 --intfmask 0x4 --count 1
```

- `loglvl`：0=Info, 1=Warn, 2=Fail, 3=Fatal
- `intfmask`：接口中断 mask (hex)
- `count`：注入事件数 (必须 > 0)

工具会自动 `vu-unlock` → `inject` → `vu-lock`。

---

## 5. 返回值与错误处理

```c
rc == 0            // 成功
rc < 0             // libcxlmi 传输 / 解析错误，errno 会被 strerror
rc > 0             // CCI 返回码 (CXLMI_RET_xxx)
                   // 通过 cxlmi_cmd_retcode_tostr() 转字符串
```

`mctp-cci` 在退出时统一映射为 shell exit code：

| 内部 rc | shell exit |
|---------|------------|
| 0       | 0          |
| 其它    | 1          |
| 2 (用法) | 2 (test_mctp_cci_*.sh 会区分) |

> `cmd_*.c` 中 `mctp_cci_report_libcxlmi_error()` (util.c:10) 会把
> `errno` 翻译到 stderr 方便排错。

---

## 6. 测试脚本

`scripts/` 下提供两套脚本：

### 6.1 `test_mctp_cci_traverse.sh`

冒烟测试，无需真实 MCTP 端点：

- 验证 `mctp-cci --help` 与每个 `<top> --help` 输出
- 验证参数解析错误路径 (非法 nid/eid、未知 top、范围检查)
- 交叉检查源码中所有 `cmd_*.c::cmds[]` 是否都同步反映到 `--help`

```bash
./tools/mctp-cci/scripts/test_mctp_cci_traverse.sh build
```

### 6.2 `test_mctp_cci_cel_commands.sh`

真机测试，**走完整链路**：

1. `logs supported` → 获取设备支持的 log 列表
2. `logs get --uuid <cel>` → 拉取 CEL
3. 解析 CEL 条目，把每个 opcode 映射到 `mctp-cci` 子命令并执行
4. 分类 PASS/FAIL/TIMEOUT/ERROR/SKIP
5. 生成 markdown 报告

环境变量：

| 变量 | 默认 | 作用 |
|------|------|------|
| `MCTP_CCI`              | `build/tools/mctp-cci/mctp-cci` | 二进制路径 |
| `MCTP_CCI_TEST_DELAY`      | 3.0  | 命令间冷却 (秒) |
| `MCTP_CCI_TEST_FAIL_DELAY` | 10.0 | FAIL/TIMEOUT/ERROR 后额外等待 (秒) |
| `MCTP_CCI_TEST_RETRIES`    | 0    | MCTP 超时重试次数 |
| `MCTP_CCI_TEST_RETRY_DELAY`| 2.0  | 重试前等待 (秒) |

```bash
sudo ./tools/mctp-cci/scripts/test_mctp_cci_cel_commands.sh 0 8 build my-result.md
```

输出 `my-result.md` 包含：

- 总体统计 (PASS / FAIL / TIMEOUT / ERROR / SKIP)
- 原始 `supported` 与 CEL
- 每个 opcode 的命令、回显、状态、备注

测试脚本内部维护一份 `map_opcode()` (test_mctp_cci_cel_commands.sh:235)
把 CCI opcode 翻译成 `mctp-cci` 参数；新增 CCI 命令时需同步更新。

---

## 7. 内部架构

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

- **调度表**：`mctp_cci_top { name, help, cmds[], n_cmds }`，由每个
  `cmd_*.c` 导出常量符号 `xxx_top`，`main.c:9` 用静态数组 `tops[]` 收集
- **错误约定**：`mctp_cci_report_libcxlmi_error()` 是各子命令的
  “统一出口”模式，参见 `util.c:10`
- **payload 解析**：所有数字参数 `strtoul(..., NULL, 0)`，支持 10/16
  进制；带单位字符串走 `parse_size_with_unit()` (util.c:111)
- **CEL 解析器**：`cmd_cel.c` 内含 80+ opcode → 名称的字典，并按
  Command Effect 字段 bit 打印含义 (config change / bg-op 等)

### 7.1 添加新子命令

1. 在 `cmd_xxx.c` 写 `static int do_xxx(...)`
2. 把它加入对应 `static const struct mctp_cci_cmd xxx_cmds[]`
3. 导出 `const struct mctp_cci_top xxx_top`
4. 在 `main.c::tops[]` 末尾加 `&xxx_top`
5. `meson.build` 加 `'cmd_xxx.c'`
6. 重新 `meson compile -C build`
7. 同步 `test_mctp_cci_cel_commands.sh::map_opcode()` 与 `test_mctp_cci_traverse.sh::expected[]`

参考：[docs/How-To-Add-A-Command.md](../../docs/How-To-Add-A-Command.md)

---

## 8. 已知限制

- 暂未实现 0x0004 Set Response Message Limit (CCI 协议层)
- 0x0104/0x0105/0x0106 MCTP Event 子命令未暴露
- 0x0402-0x0405 Log Admin 子命令未暴露
- 0x4400/0x4401 Sanitize / Secure Erase 全部暴露但脚本默认 SKIP (破坏性)
- 0x4801-0x4803 DCD Extent 子命令未暴露
- 0x5300-0x5302 Tunnel Management 未暴露
- Vendor 通道当前只实现 ScaleFlux VU Inject Event (0xCC53)

---

## 9. 相关文档

- [libcxlmi 主 README](../../README.md)
- [Generic Component Commands](../../docs/Generic-Component-Commands.md)
- [Memory Device Commands](../../docs/Memory-Device-Commands.md)
- [FM-API](../../docs/FM-API.md)
- [Vendor Specific Commands](../../docs/Vendor-Specific-Commands.md)
- [How To Add A Command](../../docs/How-To-Add-A-Command.md)
- [Testing](../../docs/Testing.md)
- CXL r3.x 规范 (CXL.io / CXL.mem / CXL.cache / CXL Fabric)
- DSP0281 — CXL Type3 CCI over MCTP Binding Spec
- DSP0324 — CXL FM-API over MCTP Binding Spec
