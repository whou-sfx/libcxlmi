1. Perform Maintenance (0600h) 请求载荷 (Input Payload)

该命令的输入载荷格式由通用头部和随后的具体操作参数组成。

| 字节偏移 | 长度 (字节) | 字段名称                             | 说明                                                         |
| -------- | ----------- | ------------------------------------ | ------------------------------------------------------------ |
| 00h      | 1           | **Maintenance Operation Class**      | **维护操作类别**：标识维护操作的类别（见下文类别表）。       |
| 01h      | 1           | **Maintenance Operation Subclass**   | **维护操作子类别**：与类别共同标识具体的维护动作。           |
| 02h      | 变长        | **Maintenance Operation Parameters** | **维护操作参数**：根据不同的类别和子类别，此处包含具体的参数（如 DPA 地址、掩码等）。 |

2. Perform Maintenance (0600h) 响应载荷 (Output Payload)

根据规范定义，该命令**没有输出载荷 (No output payload)**。

**执行结果**：操作的成功或失败状态通过 CCI 消息格式中的 **Return Code** 以及 **Background Command Status Register**（如果是后台操作）来检索。**事件记录**：某些操作（如 Memory Sparing 或 PPR）完成后，设备可能会产生 **Memory Sparing Event Record** 来通知主机更新后的资源可用性。

3. 常见维护操作类别与子类别汇总 (部分)

维护操作的具体参数（Byte 02h 以后）取决于下表中的定义：

| 类别 (Class)             | 子类别 (Subclass) | 描述                | 相关 UUID / 参数定义                                         |
| ------------------------ | ----------------- | ------------------- | ------------------------------------------------------------ |
| **01h (PPR)**            | 00h               | **Soft PPR (sPPR)** | 临时行修复，参数包含 DPA 和 Nibble Mask。                    |
|                          | 01h               | **Hard PPR (hPPR)** | 永久行修复，参数包含 DPA 和 Nibble Mask。                    |
| **02h (Memory Sparing)** | 00h - 03h         | **内存备用**        | 包括 Cacheline、Row、Bank 或 Rank 级别的备用修复。参数详见下表。 |
| **03h (Built-in Test)**  | 00h               | **Media Test**      | 设备内置媒体测试。参数包含测试 ID、迭代次数、模式等。        |

4. 详细参数字段说明 (以 PPR 和 Memory Sparing 为例)

4.1 PPR (Post Package Repair) 请求字段

PPR 分为 **sPPR (Soft PPR, 子类别 00h)** 和 **hPPR (Hard PPR, 子类别 01h)**。两者的参数结构一致。

| 字节偏移 | 长度 (字节) | 字段名称                           | 说明                                                         |
| -------- | ----------- | ---------------------------------- | ------------------------------------------------------------ |
| 00h      | 1           | **Maintenance Operation Class**    | 必须设为 **01h** (PPR)。                                     |
| 01h      | 1           | **Maintenance Operation Subclass** | **00h** 代表 sPPR；**01h** 代表 hPPR。                       |
| 02h      | 1           | **Flags**                          | **Bit 0: Query Resources Flag**。若置 1，设备仅检查是否有可用修复资源而不执行修复。 |
| 03h      | 8           | **DPA**                            | **设备物理地址**。指定需要修复的具体地址。若特性不支持 DPA 参数则忽略。 |
| 0Bh      | 3           | **Nibble Mask**                    | **半字节掩码**。标识内存总线上受影响的一个或多个 Nibble。    |



4.2 MBIST (Media Test) 请求字段

MBIST 归类为 **Device Built-in Test (类别 03h)**，目前主要定义了 **Media Test (子类别 00h)**。由于测试参数可能很长，它支持分片传输。

A. 基础维护头 (Maintenance Header)

| 字节偏移 | 长度 | 字段名称                           | 说明                                                         |
| -------- | ---- | ---------------------------------- | ------------------------------------------------------------ |
| 00h      | 1    | **Maintenance Operation Class**    | 必须设为 **03h**。                                           |
| 01h      | 1    | **Maintenance Operation Subclass** | **00h** 代表 Media Test。                                    |
| 02h      | 1    | **Action**                         | **00h**: 全量传输; **01h**: 启动分片; **02h**: 继续分片; **03h**: 结束分片; **04h**: 中止。 |
| 03h      | 4    | **Offset**                         | 在 Test Parameters 数据中的字节偏移（32 字节倍数）。         |
| 08h      | 变长 | **Test Parameters**                | 包含通用配置和具体的测试条目，详见下表。                     |

**Table 8-122. Test Parameters (测试参数)**

该表格定义了维护操作中“设备内置测试”（Device Built-in Test）的参数结构。

| 字节偏移        | 长度 (字节) | 说明                                                         |
| --------------- | ----------- | ------------------------------------------------------------ |
| 00h             | 20h         | **通用配置参数 (Common Configuration Parameters)**：适用于特定子类别内所有测试的输入配置参数。媒体测试（Media Test）的通用配置定义在 Table 8-123 中。 |
| 20h             | 20h         | **测试 1 参数条目 (Test 1 Parameters Entry)**：测试 1 的输入参数。媒体测试的条目格式定义在 Table 8-124 中。 |
| 40h             | 20h         | **测试 2 参数条目 (Test 2 Parameters Entry)**：测试 2 的输入参数。 |
| ...             | ...         | ...                                                          |
| (20h+20h*(n-1)) | 20h         | **测试 n 参数条目 (Test n Parameters Entry)**：第 n 个测试的输入参数。 |



**Table 8-123. Common Configuration Parameters for Media Test Subclass (媒体测试子类别通用配置参数)**

该表格详述了 Table 8-122 中前 32 字节（00h-1Fh）的媒体测试具体配置。

| 字节偏移 | 长度 (字节) | 字段名称                                                | 说明                                                         |
| -------- | ----------- | ------------------------------------------------------- | ------------------------------------------------------------ |
| 00h      | 1           | **测试数量 (Number of Tests)**                          | 请求执行的测试总数。                                         |
| 01h      | 8           | **起始地址 (Start Address)**                            | 测试的起始 DPA 地址，适用于所有测试条目。                    |
| 09h      | 8           | **长度 (Length)**                                       | 要测试的物理地址范围，以 64 字节为单位，适用于所有测试。     |
| 11h      | 1           | **媒体测试结果配置 (Media Test Results Configuration)** | **Bit 0: 错误签名配置 (Error Signature Configuration)**<br>— 0 = 完整模式：报告所有错误签名。<br>— 1 = 单个错误签名模式：仅报告触发阈值后的首个或遇到的首个错误。 |
| 12h      | 1           | **配置标志 (Configuration Flags)**                      | **Bits[1:0]: ECC 禁用 (ECC Disablement)**<br>— 00b = 数据和元数据 ECC 均开启。<br>— 01b = 数据 ECC 禁用，元数据 ECC 开启。 |
| 13h      | Dh          | **预留 (Reserved)**                                     | 预留位。                                                     |



**Table 8-124. Test Parameters Entry Media Test Subclass (媒体测试子类别测试参数条目)**

该表格详述了 Table 8-122 中每个测试条目（从偏移 20h 开始循环）的具体字段。

| 字节偏移 | 长度 (字节) | 字段名称                                 | 说明                                                         |
| -------- | ----------- | ---------------------------------------- | ------------------------------------------------------------ |
| 00h      | 2           | **测试 ID (Test ID)**                    | 标识具体的测试算法，该值通过媒体测试能力日志（Media Test Capability Log）发现。 |
| 02h      | 1           | **迭代次数 (Number of iterations)**      | 测试重复执行的次数。                                         |
| 03h      | 2           | **标志 (Flags)**                         | **Bit 0**: 启用反转模式测试。<br>**Bit 1**: 遇到首个不可纠正错误时停止测试。<br>**Bit 8**: 发现不可纠正错误时自动更新毒性列表（Poison List）。 |
| 05h      | 2           | **模式类型 (Pattern Type)**              | 选择测试所用的 64B 模式：<br>00h: 用户提供；01h: 厂商特定；02h: PRBS；03h: DPA[63:0] 重复 8 次；04h: 55h；05h: AAh。 |
| 07h      | 1           | **模式值 (Pattern Value)**               | 用户提供的模式值。仅当模式类型为 00h 时有效。                |
| 08h      | 2           | **厂商特定 (Vendor Specific)**           | 当模式类型为 01h 时，由厂商自定义解释。                      |
| 0Ah      | 4           | **PRBS 种子 (PRBS Seed)**                | 用户提供的 PRBS 算法种子。                                   |
| 0Eh      | 2           | **错误计数阈值 (Error Count Threshold)** | 用户可编程的错误计数阈值。                                   |
| 10h      | 10h         | **预留 (Reserved)**                      | 预留位。                                                     |