# mbcci-sfx: Memory Device Commands 扩展计划

本文档描述将 `docs/Supported-Commands.md` 中全部 **Memory Device Commands**（标 ✅ 的命令）
接入 `tools/mbcci-sfx/` 的实现计划。

当前已实现：`identify`（4000h）。

---

## 总体架构

```
tools/mbcci-sfx/
├── meson.build        # 新增每个 cmd_*.c 加入 sources 列表
├── mbcci-sfx.h        # subcmd 表，追加每个新命令的 extern 原型
├── main.c             # 不动，分发框架已就位
├── cmd_identify.c     # 已实现 (4000h)
│
├── cmd_partition.c    # 4100h-4103h: Get/Set Partition Info, Get/Set LSA
├── cmd_health.c       # 4200h-4204h: Get Health Info, Get/Set Alert Config, Get/Set Shutdown State
├── cmd_poison.c       # 4300h-4305h: Get Poison List, Inject/Clear Poison, Scan Media
├── cmd_sanitize.c     # 4400h-4401h: Sanitize, Secure Erase
├── cmd_security.c     # 4500h-4505h: Get Security State, Set/Disable Passphrase,
│                      #              Unlock, Freeze Security State, Passphrase Secure Erase
├── cmd_qos.c          # 4700h-4702h: Get/Set SLD QoS Control, Get SLD QoS Status
└── cmd_dc.c           # 4800h-4803h: Get DC Config, Get DC Extent List,
                       #              Add DC Response, Release DC
```

每个新 `.c` 文件只处理一组紧密相关的命令，共用同一个 `struct cxlmi_endpoint *ep` 入参，
通过顶层 `subcmds[]` 表注册子命令名称。

---

## 命令清单与实现要点

### 1. Capacity Configuration and LSA (`cmd_partition.c`)

| 子命令             | Opcode | libcxlmi API                              | 方向         |
| :----------------- | :----- | :---------------------------------------- | :----------- |
| `get-partition`    | 4100h  | `cxlmi_cmd_memdev_get_partition_info`     | 仅输出       |
| `set-partition`    | 4101h  | `cxlmi_cmd_memdev_set_partition_info`     | 仅输入       |
| `get-lsa`          | 4102h  | `cxlmi_cmd_memdev_get_lsa`                | 输入+raw输出 |
| `set-lsa`          | 4103h  | `cxlmi_cmd_memdev_set_lsa`                | 输入+raw数据 |

**实现要点：**
- `get-partition`：打印 `active_vmem`、`active_pmem`、`next_vmem`、`next_pmem`（单位 256MB 块）。
- `set-partition`：命令行接受 `--next-volatile <MiB>` 与 `--next-persistent <MiB>`，
  CLI 换算成 256MB 块后写入 req，`bp_dirty_shutdown_state` 选项可选。
- `get-lsa`：先调 `memdev_identify` 读取 `lsa_size`，再以该大小分配输出 buffer，
  结果以十六进制逐行打印（或可选 `--output <file>` 写到文件）。
- `set-lsa`：接受 `--offset <n>` 与 `--input <file>`，从文件读取 payload。
  `cxlmi_cmd_memdev_set_lsa` 第三参数为 `data_sz`（整个 req 结构大小含数据）。

---

### 2. Health Info and Alerts (`cmd_health.c`)

| 子命令               | Opcode | libcxlmi API                                | 方向   |
| :------------------- | :----- | :------------------------------------------ | :----- |
| `get-health`         | 4200h  | `cxlmi_cmd_memdev_get_health_info`          | 仅输出 |
| `get-alert-config`   | 4201h  | `cxlmi_cmd_memdev_get_alert_config`         | 仅输出 |
| `set-alert-config`   | 4202h  | `cxlmi_cmd_memdev_set_alert_config`         | 仅输入 |
| `get-shutdown-state` | 4203h  | `cxlmi_cmd_memdev_get_shutdown_state`       | 仅输出 |
| `set-shutdown-state` | 4204h  | `cxlmi_cmd_memdev_set_shutdown_state`       | 仅输入 |

**实现要点：**
- `get-health`：打印 `health_status`（位字段解码）、`media_status`、温度、`life_used`、
  `dirty_shutdown_count` 等全部字段。
- `get-alert-config`：打印 `valid_alerts`、`programmable_alerts` 位字段，及各可编程阈值。
- `set-alert-config`：接受 `--life-used-warn <n>`、`--over-temp-warn <n>` 等选项，
  对应设置 req 结构各阈值字段与 `enable_alert_actions`。
- `get/set-shutdown-state`：`dirty_shutdown_state` 为单字节布尔，CLI 取 `0` 或 `1`。

---

### 3. Media and Poison Management (`cmd_poison.c`)

| 子命令                      | Opcode | libcxlmi API                                          | 方向         |
| :-------------------------- | :----- | :---------------------------------------------------- | :----------- |
| `get-poison-list`           | 4300h  | `cxlmi_cmd_get_poison_list`                           | 输入+输出    |
| `inject-poison`             | 4301h  | `cxlmi_cmd_memdev_inject_poison`                      | 仅输入       |
| `clear-poison`              | 4302h  | `cxlmi_cmd_memdev_clear_poison`                       | 仅输入       |
| `get-scan-media-caps`       | 4303h  | `cxlmi_cmd_memdev_get_scan_media_capabilities`        | 输入+输出    |
| `scan-media`                | 4304h  | `cxlmi_cmd_memdev_scan_media`                         | 仅输入       |
| `get-scan-media-results`    | 4305h  | `cxlmi_cmd_memdev_get_scan_media_results`             | 仅输出       |

**实现要点：**
- `get-poison-list`：接受 `--dpa <hex>` 与 `--length <bytes>`，
  打印每条 `media_err_addr`、`media_err_len` 记录，以及 `overflow_timestamp`、`flags`。
- `inject-poison` / `clear-poison`：接受 `--dpa <hex>`；clear 额外接受
  `--write-data <64-byte-hex>` 或 `--write-data-file <file>`。
- `scan-media`：接受 `--dpa <hex>` 与 `--length <bytes>` 与 `--flags <hex>`；
  命令本身为 background，建议配合 `bg-op-status` 轮询（属 Generic 命令组，后续计划接入）。
- `get-scan-media-results`：打印 `restart_physaddr`、`restart_physaddr_length`、
  `flags`、`media_error_count` 以及每条错误记录。

---

### 4. Sanitize and Media Operations (`cmd_sanitize.c`)

| 子命令         | Opcode | libcxlmi API                                | 方向   |
| :------------- | :----- | :------------------------------------------ | :----- |
| `sanitize`     | 4400h  | `cxlmi_cmd_memdev_sanitize`                 | 无载荷 |
| `secure-erase` | 4401h  | `cxlmi_cmd_memdev_secure_erase`             | 无载荷 |

**实现要点：**
- 两个命令均无 req/rsp 载荷，API 仅 `(ep, ti)`。
- 两者都可能返回 `CXLMI_RET_BACKGROUND`，工具应将此视为成功并打印提示：
  `"Operation started in background"`。
- 建议加 `--no-confirm` 标志，否则默认打印警告并要求用户输入 `yes` 确认，
  防止误操作（sanitize 会清除所有持久化数据）。

---

### 5. Persistent Memory Security (`cmd_security.c`)

| 子命令                  | Opcode | libcxlmi API                                        | 方向   |
| :---------------------- | :----- | :-------------------------------------------------- | :----- |
| `get-security-state`    | 4500h  | `cxlmi_cmd_memdev_get_security_state`               | 仅输出 |
| `set-passphrase`        | 4501h  | `cxlmi_cmd_memdev_set_passphrase`                   | 仅输入 |
| `disable-passphrase`    | 4502h  | `cxlmi_cmd_memdev_disable_passphrase`               | 仅输入 |
| `unlock`                | 4503h  | `cxlmi_cmd_memdev_unlock`                           | 仅输入 |
| `freeze-security-state` | 4504h  | `cxlmi_cmd_memdev_freeze_security_state`            | 无载荷 |
| `passphrase-secure-erase` | 4505h | `cxlmi_cmd_memdev_passphrase_secure_erase`         | 仅输入 |

**实现要点：**
- 所有涉及 passphrase 的命令用 `--passphrase <string>` 接收，工具内部
  以 `memcpy` 写入 req 的固定长度字段（通常 32 字节），CLI 不回显密码。
- `set-passphrase` 额外接受 `--type <user|master>` 区分 user 与 master passphrase。
- `get-security-state` 打印 `security_state` 位字段的各状态位（Enabled/Locked/Frozen/…）。

---

### 6. SLD QoS Telemetry (`cmd_qos.c`)

| 子命令              | Opcode | libcxlmi API                                  | 方向   |
| :------------------ | :----- | :-------------------------------------------- | :----- |
| `get-sld-qos-ctrl`  | 4700h  | `cxlmi_cmd_memdev_get_sld_qos_control`        | 仅输出 |
| `set-sld-qos-ctrl`  | 4701h  | `cxlmi_cmd_memdev_set_sld_qos_control`        | 仅输入 |
| `get-sld-qos-status`| 4702h  | `cxlmi_cmd_memdev_get_sld_qos_status`         | 仅输出 |

**实现要点：**
- `get-sld-qos-ctrl` 打印 `qos_telemetry_control` 位字段。
- `set-sld-qos-ctrl` 接受 `--egress-port-congestion <0|1>` 与 `--temporary-throughput-reduction <0|1>`。
- `get-sld-qos-status` 打印 `backed_pressure_sample_interval` 等状态字段。

---

### 7. Dynamic Capacity (`cmd_dc.c`)

| 子命令               | Opcode | libcxlmi API                                | 方向      |
| :------------------- | :----- | :------------------------------------------ | :-------- |
| `get-dc-config`      | 4800h  | `cxlmi_cmd_memdev_get_dc_config`            | 输入+输出 |
| `get-dc-extent-list` | 4801h  | `cxlmi_cmd_memdev_get_dc_extent_list`       | 输入+输出 |
| `add-dc-response`    | 4802h  | `cxlmi_cmd_memdev_add_dc_response`          | 仅输入    |
| `release-dc`         | 4803h  | `cxlmi_cmd_memdev_release_dc`               | 仅输入    |

**实现要点：**
- `get-dc-config`：接受 `--region <n>` 与 `--num-regions <n>`；打印每个 DC region 的
  `base`、`length`、`block_size`、`dsmad_handle`、`flags`。
- `get-dc-extent-list`：接受 `--region <n>` 与 `--offset <n>` 与 `--limit <n>`；
  打印每个 extent 的 `dpa`、`length`、`tag`、`shared_seq`。
- `add-dc-response`：接受 `--region <n>` 与一组 `--extent dpa:len` 选项。
- `release-dc`：接受 `--region <n>` 与一组 `--extent dpa:len` 选项。

---

## meson.build 修改

在 `tools/mbcci-sfx/meson.build` 的 `sources` 列表中追加：

```meson
executable(
    'mbcci-sfx',
    [
        'main.c',
        'cmd_identify.c',
        'cmd_partition.c',    # 4100h-4103h
        'cmd_health.c',       # 4200h-4204h
        'cmd_poison.c',       # 4300h-4305h
        'cmd_sanitize.c',     # 4400h-4401h
        'cmd_security.c',     # 4500h-4505h
        'cmd_qos.c',          # 4700h-4702h
        'cmd_dc.c',           # 4800h-4803h
    ],
    dependencies: libcxlmi_dep,
    include_directories: [inc]
)
```

---

## mbcci-sfx.h 修改

在 `tools/mbcci-sfx/mbcci-sfx.h` 中追加各新命令的原型：

```c
/* cmd_partition.c */
int cmd_get_partition(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_set_partition(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_lsa(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_set_lsa(struct cxlmi_endpoint *ep, int argc, char **argv);

/* cmd_health.c */
int cmd_get_health(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_alert_config(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_set_alert_config(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_shutdown_state(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_set_shutdown_state(struct cxlmi_endpoint *ep, int argc, char **argv);

/* cmd_poison.c */
int cmd_get_poison_list(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_inject_poison(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_clear_poison(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_scan_media_caps(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_scan_media(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_scan_media_results(struct cxlmi_endpoint *ep, int argc, char **argv);

/* cmd_sanitize.c */
int cmd_sanitize(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_secure_erase(struct cxlmi_endpoint *ep, int argc, char **argv);

/* cmd_security.c */
int cmd_get_security_state(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_set_passphrase(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_disable_passphrase(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_unlock(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_freeze_security_state(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_passphrase_secure_erase(struct cxlmi_endpoint *ep, int argc, char **argv);

/* cmd_qos.c */
int cmd_get_sld_qos_ctrl(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_set_sld_qos_ctrl(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_sld_qos_status(struct cxlmi_endpoint *ep, int argc, char **argv);

/* cmd_dc.c */
int cmd_get_dc_config(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_get_dc_extent_list(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_add_dc_response(struct cxlmi_endpoint *ep, int argc, char **argv);
int cmd_release_dc(struct cxlmi_endpoint *ep, int argc, char **argv);
```

---

## main.c 的 subcmds[] 追加项

```c
static const struct subcmd subcmds[] = {
    /* 已实现 */
    { "identify",              cmd_identify,              "Identify Memory Device (4000h)" },

    /* cmd_partition.c */
    { "get-partition",         cmd_get_partition,         "Get Partition Info (4100h)" },
    { "set-partition",         cmd_set_partition,         "Set Partition Info (4101h)" },
    { "get-lsa",               cmd_get_lsa,               "Get Label Storage Area (4102h)" },
    { "set-lsa",               cmd_set_lsa,               "Set Label Storage Area (4103h)" },

    /* cmd_health.c */
    { "get-health",            cmd_get_health,            "Get Health Info (4200h)" },
    { "get-alert-config",      cmd_get_alert_config,      "Get Alert Configuration (4201h)" },
    { "set-alert-config",      cmd_set_alert_config,      "Set Alert Configuration (4202h)" },
    { "get-shutdown-state",    cmd_get_shutdown_state,    "Get Shutdown State (4203h)" },
    { "set-shutdown-state",    cmd_set_shutdown_state,    "Set Shutdown State (4204h)" },

    /* cmd_poison.c */
    { "get-poison-list",       cmd_get_poison_list,       "Get Poison List (4300h)" },
    { "inject-poison",         cmd_inject_poison,         "Inject Poison (4301h)" },
    { "clear-poison",          cmd_clear_poison,          "Clear Poison (4302h)" },
    { "get-scan-media-caps",   cmd_get_scan_media_caps,   "Get Scan Media Capabilities (4303h)" },
    { "scan-media",            cmd_scan_media,            "Scan Media (4304h)" },
    { "get-scan-media-results",cmd_get_scan_media_results,"Get Scan Media Results (4305h)" },

    /* cmd_sanitize.c */
    { "sanitize",              cmd_sanitize,              "Sanitize (4400h)" },
    { "secure-erase",          cmd_secure_erase,          "Secure Erase (4401h)" },

    /* cmd_security.c */
    { "get-security-state",    cmd_get_security_state,    "Get Security State (4500h)" },
    { "set-passphrase",        cmd_set_passphrase,        "Set Passphrase (4501h)" },
    { "disable-passphrase",    cmd_disable_passphrase,    "Disable Passphrase (4502h)" },
    { "unlock",                cmd_unlock,                "Unlock (4503h)" },
    { "freeze-security-state", cmd_freeze_security_state, "Freeze Security State (4504h)" },
    { "passphrase-secure-erase",cmd_passphrase_secure_erase,"Passphrase Secure Erase (4505h)"},

    /* cmd_qos.c */
    { "get-sld-qos-ctrl",      cmd_get_sld_qos_ctrl,      "Get SLD QoS Control (4700h)" },
    { "set-sld-qos-ctrl",      cmd_set_sld_qos_ctrl,      "Set SLD QoS Control (4701h)" },
    { "get-sld-qos-status",    cmd_get_sld_qos_status,    "Get SLD QoS Status (4702h)" },

    /* cmd_dc.c */
    { "get-dc-config",         cmd_get_dc_config,         "Get DC Configuration (4800h)" },
    { "get-dc-extent-list",    cmd_get_dc_extent_list,    "Get DC Extent List (4801h)" },
    { "add-dc-response",       cmd_add_dc_response,       "Add DC Response (4802h)" },
    { "release-dc",            cmd_release_dc,            "Release DC (4803h)" },
};
```

---

## 实现顺序建议

按照"风险低、常用优先"的顺序分批实现，每批单独可编译验证：

| 批次 | 文件             | 命令数 | 优先原因                          |
| :--- | :--------------- | :----: | :-------------------------------- |
| 1    | `cmd_health.c`   |   5    | 只读为主，结构简单，常用于监控    |
| 2    | `cmd_partition.c`|   4    | 常用于容量配置，LSA 涉及 raw buf  |
| 3    | `cmd_poison.c`   |   6    | Media 管理核心，scan-media 异步   |
| 4    | `cmd_sanitize.c` |   2    | 危险操作，加确认交互              |
| 5    | `cmd_security.c` |   6    | 涉及 passphrase，需安全输入处理   |
| 6    | `cmd_qos.c`      |   3    | 字段简单，快速完成                |
| 7    | `cmd_dc.c`       |   4    | 结构复杂，extent 列表需动态分配   |

---

## 通用约定（所有 cmd_*.c 共同遵守）

1. **返回值**：0 = 成功；`CXLMI_RET_BACKGROUND` 视为成功并打印提示；
   其他 >0 用 `cxlmi_cmd_retcode_tostr()` 打印 CXL 错误描述；-1 打印 ioctl 失败。
2. **动态内存**：仅在必须时使用 `calloc`/`malloc`，配对 `free`，不做多余的深层封装。
3. **参数解析**：在各 `cmd_*()` 内用简单的 `argc`/`argv` 遍历，不引入 `getopt_long`
   之外的依赖（`getopt_long` 已在 glibc 中可用）。
4. **危险命令**：`sanitize`、`passphrase-secure-erase`、`secure-erase` 在无 `--no-confirm`
   时默认要求终端确认（`fprintf(stderr, "Type 'yes' to confirm: ")`）。
5. **不做**：Generic Component Commands（0xxx）、FM-API（5xxx）的接入留给后续计划；
   raw opcode 通道、batch 回放、JSON 输出、tunnel 选项同样留给后续。
