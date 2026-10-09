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