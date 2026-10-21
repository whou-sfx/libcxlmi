### 1. DDR5 Error Check Scrub (ECS) Log

**UUID**: `f1720d60-a7a9-4306-a003-11948f9e077c`**总长度**: 38 字节（对应 4 字节头 + 1 个 34 字节条目）

| 偏移 (字节) | 字段名称             | 长度    | 说明                                                         |
| ----------- | -------------------- | ------- | ------------------------------------------------------------ |
| 00h         | **Log Header**       | 4 字节  | 包含 `Total Number of Entries`（10位）和 `Header ID` 等公共信息。 |
| 04h         | **ECS Log Entry #1** | 34 字节 | 针对单个 DRAM 颗粒或 FRU 的 ECS 错误记录，包含错误计数、DRAM 状态及阈值等。 |



#### 1.1 DDR5 Error Check Scrub (ECS) Log 条目解析

该日志由 4 字节头和多个 34 字节的 **ECS Log Entry** 组成。

**ECS Log Entry (34 字节)**：

| 偏移 | 字段名称            | 长度    | 说明                                    |
| ---- | ------------------- | ------- | --------------------------------------- |
| 00h  | **Log Flags**       | 1 字节  | 包含条目有效位、错误溢出位等信息。      |
| 01h  | **DRAM Address**    | 6 字节  | 指向发生 ECS 错误的物理地址或 DPA。     |
| 07h  | **ECS Error Count** | 2 字节  | 记录该地址/区域检测到的纠正性错误总数。 |
| 09h  | **ECS Threshold**   | 2 字节  | 当前配置的报警阈值。                    |
| 0Bh  | **Vendor Specific** | 23 字节 | 供应商特定的详细错误上下文。            |



### 2. Media Test Capability Log

**UUID**: `e6dfa32c-d13e-4a5c-8ca8-99bebbf731a4`**总长度**: 128 字节（对应 16 字节头 + 7 个 16 字节测试条目）

| 偏移 (字节) | 字段名称          | 长度    | 说明                                                         |
| ----------- | ----------------- | ------- | ------------------------------------------------------------ |
| 00h         | **Log Header**    | 16 字节 | 包含 `Number of Supported Tests`（指示设备支持的算法数量）及其它公共属性。 |
| 10h         | **Test Entry #1** | 16 字节 | 描述第一个支持的测试算法，包含算法 ID、每 GB 预期测试时间、是否支持指定地址等。 |
| ...         | ...               | ...     | ...                                                          |
| 70h         | **Test Entry #7** | 16 字节 | 描述第七个支持的测试算法。                                   |



#### 2.1 Media Test Capability Log 条目解析

该日志由 16 字节头和多个 16 字节的 **Test Capability Entry** 组成。

**Test Capability Entry (16 字节，CXL 3.2 Table 8-93)**：

| 偏移 | 字段名称              | 长度   | 说明                                                                 |
| ---- | --------------------- | ------ | -------------------------------------------------------------------- |
| 00h  | **Test ID**           | 2 字节 | Perform Maintenance 里引用的测试 ID。                                |
| 02h  | **Algorithm**         | 1 字节 | 01h=WRC，02h=Checkerboard，03h=MARCH，04h=MATS，05h=MATS+，06h/07h=Walking。 |
| 03h  | **Execution Time**    | 1 字节 | 每 GB 最大耗时。bits[3:0]=scale（1h=10ms … 6h=1000s），bits[7:4]=value。 |
| 04h  | **Capabilities**      | 2 字节 | 地址可配、反相、遇 UE 退出等。                                       |
| 06h  | **Supported Patterns**| 2 字节 | 支持的 64B pattern 位图。                                            |
| 08h  | **PRBS Length**       | 1 字节 | Algorithm 为 03h（Read pattern）时无效。                             |



### 3. Media Test Results Short Log

**UUID**: `2c255522-8ce4-11ec-b909-0242ac120002`**总长度**: 240 字节（对应 16 字节头 + 7 个 32 字节结果条目）

| 偏移 (字节) | 字段名称            | 长度    | 说明                                                         |
| ----------- | ------------------- | ------- | ------------------------------------------------------------ |
| 00h         | **Log Header**      | 16 字节 | 包含当前日志中的结果数量及状态。                             |
| 10h         | **Result Entry #1** | 32 字节 | 记录第一个执行过的测试简要结果（如测试完成状态、是否有错误）。 |
| ...         | ...                 | ...     | ...                                                          |
| D0h         | **Result Entry #7** | 32 字节 | 记录第七个执行过的测试简要结果。                             |





#### 3.1 Media Test Results Short Log 条目解析

该日志由 16 字节头和多个 32 字节的 **Short Result Entry** 组成。

**Short Result Entry (32 字节，CXL 3.2 Table 8-96)**：

| 偏移 | 字段名称                    | 长度   | 说明                                      |
| ---- | --------------------------- | ------ | ----------------------------------------- |
| 00h  | **Test ID**                 | 2 字节 | 测试 ID。                                 |
| 02h  | **Start Time**              | 8 字节 | 自 1970-01-01 UTC 起的纳秒。              |
| 0Ah  | **End Time**                | 8 字节 | 同上。                                    |
| 12h  | **Result**                  | 1 字节 | 00h 成功，01h 失败，02h 被 abort。        |
| 13h  | **Flags**                   | 1 字节 | Bit0：Error Signature List Overflow。     |
| 14h  | **Uncorrectable Error Count** | 4 字节 | 不可纠正错误数。                        |
| 18h  | **Correctable Error Count** | 4 字节 | 可纠正错误数。                            |





### 4. Media Test Results Long Log

**UUID**: `c1fe0b3e-7a00-448e-a24e-a6aabbfe587a`**总长度**: 5472 字节（对应 16 字节头 + 变长测试摘要与错误签名）

16 字节公共头（Table 8-98）之后是变长条目。每条先是 48 字节结果（Table 8-99），再紧跟 N 个 80 字节 Error Signature（Table 8-100）。N 由该条偏移 `28h` 的 Number of Error Signatures 给出。



#### 4.1 Media Test Results Long Log 递归解析

长日志结构最为复杂，包含一个 **Execution Summary** 和多个 **Error Signatures**。

**A. Long Result Entry 固定部分 (48 字节，Table 8-99)**

| 偏移 | 字段名称                 | 长度   | 说明                                      |
| ---- | ------------------------ | ------ | ----------------------------------------- |
| 00h  | **Test ID**              | 2 字节 | 测试 ID。                                 |
| 02h  | **Start Time**           | 8 字节 | 纳秒时间戳。                              |
| 0Ah  | **End Time**             | 8 字节 | 纳秒时间戳。                              |
| 12h  | **Result**               | 1 字节 | 00h 成功，01h 失败，02h 被 abort。        |
| 13h  | **Flags**                | 1 字节 | Bit0：Error Signature List Overflow。     |
| 14h  | **Uncorrectable Count**  | 4 字节 | 不可纠正错误数。                          |
| 18h  | **Correctable Count**    | 4 字节 | 可纠正错误数。                            |
| 20h  | **Capacity Tested**      | 8 字节 | 已测容量，单位 256MB。                    |
| 28h  | **Number of Error Signatures** | 4 字节 | 紧随其后的签名条数。                |

**B. Error Signature (80 字节，Table 8-100)**

| 偏移 | 字段名称            | 长度   | 说明                                                         |
| ---- | ------------------- | ------ | ------------------------------------------------------------ |
| 00h  | **Iteration**       | 2 字节 | 出错时所在的测试迭代。                                       |
| 02h  | **Physical Address**| 8 字节 | bit0=volatile，bits[2:1]=错误类型，bits[63:6]=DPA。         |
| 0Ah  | **Validity Flags**  | 2 字节 | 标明 Channel/Rank/Bank/Row 等字段是否有效。                  |



### 5. Comonent State Dump Log

 (UUID): `b3fab4cf-01b6-4332-943e-5e9962f23567`

5.1 输入载荷 (Input Payload - Table 8-84)

| 字节偏移 (Byte Offset) | 长度 (Bytes) | 字段名称 (Field Name) | 详细描述与取值 (Description & Value)                         |
| ---------------------- | ------------ | --------------------- | ------------------------------------------------------------ |
| **00h**                | `10h` (16B)  | **Log Identifier**    | 目标日志类型的 16 字节 UUID4。对于 Component State Dump Log，固定填入：<br>`b3fab4cf-01b6-4332-943e-5e9962f23567`23。 |
| **10h**                | `4`          | **Offset**            | 欲读取日志数据的**起始字节偏移量**4。首次发起读取操作时必须传入 `0`5。 |
| **14h**                | `4`          | **Length**            | 本次请求读取的**数据长度（字节数）**4。                      |



5.2 输出载荷 (Output Payload - Table 8-85)

| 字节偏移 (Byte Offset) | 长度 (Bytes)  | 字段名称 (Field Name) | 详细描述与取值 (Description & Value)                         |
| ---------------------- | ------------- | --------------------- | ------------------------------------------------------------ |
| **00h**                | 可变 (Varies) | **Log Data**          | 返回指定偏移量处的日志字节数据4。当从 `Offset = 0` 开始读取时，前 64 字节为标准头部格式6。 |



Log Data 内部结构 (Table 8-89 Component State Dump Log Format)

当从 `Offset = 0` 开始读取时，返回的 **Log Data** 前 64 字节（`0x00~0x3F`）为规范定义的标准 Header，`0x40` 偏移之后为厂商自定义的 Dump Payload 数据67：

| 字节偏移 (Byte Offset) | 长度 (Bytes)  | 字段名称 (Field Name)                       | 详细描述与取值 (Description & Value)                         |
| ---------------------- | ------------- | ------------------------------------------- | ------------------------------------------------------------ |
| **00h**                | `4`           | **Component State Dump Data Length**        | `0x40` 偏移处厂商自定义数据（`Component State Dump Data`）的总字节长度6。 |
| **04h**                | `1`           | **Auto Populate Trigger Count Since Clear** | 自上次执行 `Clear Log` 以来，设备因内部触发（如 Crash）自动生成 Dump 的次数8。填 `00h` 表示不支持计数，最大值饱和于 `FFh`8。 |
| **05h**                | `1`           | **Event Log**                               | 指示关联事件记录所在的 Event Log 类型9（`00h`=Informational, `01h`=Warning, `02h`=Failure, `03h`=Fatal, `04h`=Dynamic Capacity）10。若无关联句柄则该字段未定义9。 |
| **06h**                | `2`           | **Associated Event Record Handle**          | 与本次 Auto Populate 触发关联的 Event Record Handle9。无关联事件或事件已被清除时填 `0000h`9。 |
| **08h**                | `8`           | **Timestamp**                               | 生成该 Dump 数据时的设备时间戳（自 UTC 1970-01-01 00:00:00 起经过的无符号纳秒数）711。 |
| **10h**                | `10h` (16B)   | **Component State Dump Format UUID**        | **可选的数据格式标识 UUID**7。用于唯一标识 `Offset 40h` 处厂商自定义数据的具体结构格式；填全 `0` 表示未指定格式7。 |
| **20h**                | `4`           | **Flags**                                   | **标志位**7：<br>• **Bit ** (`Auto Populate Data`)：`1` 表示该数据由 Auto Populate（如硬件/FW Crash 自动触发）生成；`0` 表示由 `Populate Log` 手动拉取生成或数据为空7。<br>• **Bits [31:1]**：保留 (Reserved)7。 |
| **24h**                | `1Ch` (28B)   | **Reserved**                                | 保留字段7。                                                  |
| **40h**                | 可变 (Varies) | **Component State Dump Data**               | **厂商自定义的 Dump 数据载荷**（Vendor Specific）7。         |