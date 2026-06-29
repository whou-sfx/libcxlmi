1. **Table 8-127: Supported Feature Entry for the sPPR Feature**

  此表定义了软件后封装修复（Soft Post Package Repair, sPPR）特性的入口结构1。

  | 字节偏移 | 长度 (字节) | 属性描述                | 值                                                           |
  | -------- | ----------- | ----------------------- | ------------------------------------------------------------ |
  | 00h      | 10h         | **Feature Identifier**  | **892ba475-fad8-474e-9d3e-692c917568bb**                     |
  | 10h      | 2           | Feature Index           | 设备特定 (Device Specific)                                   |
  | 12h      | 2           | Get Feature Size        | 14h (20 字节)                                                |
  | 14h      | 2           | Set Feature Size        | 03h (3 字节)                                                 |
  | 16h      | 4           | **Attribute Flags**     | **Bit 0: 厂商特定 (可更改)**<br>Bits[3:1]: 010b (若支持保存选择则为热复位持久) 或 000b<br>Bit 4: 厂商特定 (固件更新持久性)<br>Bit 5: 1 (支持默认选择)<br>Bit 6: 1 (支持保存选择) |
  | 1Ah      | 1           | Get Feature Version     | 03h                                                          |
  | 1Bh      | 1           | Set Feature Version     | 03h                                                          |
  | 1Ch      | 2           | **Set Feature Effects** | Bit 1: 1 (立即配置变更)<br>Bit 9: 1 (建议值，CEL[11:10] 有效)<br>Bit 10: 0 (常规复位后无配置变更) |

  

  **Table 8-130: Supported Feature Entry for the hPPR Feature**

  此表定义了硬件后封装修复（Hard Post Package Repair, hPPR）特性的入口结构2。

  | 字节偏移 | 长度 (字节) | 属性描述                | 值                                                           |
  | -------- | ----------- | ----------------------- | ------------------------------------------------------------ |
  | 00h      | 10h         | **Feature Identifier**  | **80ea4521-786f-4127-afb1-ec7459fb0e24**                     |
  | 10h      | 2           | Feature Index           | 设备特定                                                     |
  | 12h      | 2           | Get Feature Size        | 14h (20 字节)                                                |
  | 14h      | 2           | Set Feature Size        | 0Ch (12 字节)                                                |
  | 16h      | 4           | **Attribute Flags**     | **Bit 0: 厂商特定**<br>Bits[3:1]: 010b (若支持保存选择) 或 000b<br>Bit 4: 厂商特定<br>Bit 5: 1 (支持默认选择)<br>Bit 6: 厂商特定 (若支持引导时启动则为1) |
  | 1Ah      | 1           | Get Feature Version     | 03h                                                          |
  | 1Bh      | 1           | Set Feature Version     | 03h                                                          |
  | 1Ch      | 2           | **Set Feature Effects** | Bit 1: 1 (立即配置变更)<br>Bit 9: 1 (建议值)<br>Bit 11: 0 (CXL 复位后无配置变更) |

  

  **Table 8-221: Supported Feature Entry for the Device Patrol Scrub Control Feature**

  此表定义了设备巡检清理（Device Patrol Scrub）控制特性的入口结构3。

  | 字节偏移 | 长度 (字节) | 属性描述                | 值                                                           |
  | -------- | ----------- | ----------------------- | ------------------------------------------------------------ |
  | 00h      | 10h         | **Feature Identifier**  | **96dad7d6-fde8-482b-a733-75774e06db8a**                     |
  | 10h      | 2           | Feature Index           | 设备特定                                                     |
  | 12h      | 2           | Get Feature Size        | 4 字节                                                       |
  | 14h      | 2           | Set Feature Size        | 2 字节                                                       |
  | 16h      | 4           | **Attribute Flags**     | **Bit 0: 1 (可更改)**<br>Bits[3:1]: 000b (无持久性)<br>Bit 4: 0 (固件更新不保留)<br>Bit 5: 1 (支持默认选择)<br>Bit 6: 0 (不支持保存选择) |
  | 1Ah      | 1           | Get Feature Version     | 01h                                                          |
  | 1Bh      | 1           | Set Feature Version     | 01h                                                          |
  | 1Ch      | 2           | **Set Feature Effects** | Bit 1: 1 (立即配置变更)<br>Bit 3: 1 (立即策略变更)<br>Bit 9: 1 (CEL[11:10] 有效) |

  

  **Table 8-224: Supported Feature Entry for the DDR5 ECS Control Feature**

  此表定义了 DDR5 错误检查清理（Error Check Scrub, ECS）控制特性的入口结构4。

  | 字节偏移 | 长度 (字节) | 属性描述                | 值                                                           |
  | -------- | ----------- | ----------------------- | ------------------------------------------------------------ |
  | 00h      | 10h         | **Feature Identifier**  | **e5b13f22-2328-4a14-b8ba-b9691e893386**                     |
  | 10h      | 2           | Feature Index           | 设备特定                                                     |
  | 12h      | 2           | Get Feature Size        | 可变 (*n*×4+1)，*n* 为 FRU 数量                              |
  | 14h      | 2           | Set Feature Size        | 可变 (*n*×2+1)                                               |
  | 16h      | 4           | **Attribute Flags**     | **Bit 0: 1 (可更改)**<br>Bits[3:1]: 000b (无持久性)<br>Bit 4: 0<br>Bit 5: 1 (支持默认选择)<br>Bit 6: 0 |
  | 1Ah      | 1           | Get Feature Version     | 01h                                                          |
  | 1Bh      | 1           | Set Feature Version     | 01h                                                          |
  | 1Ch      | 2           | **Set Feature Effects** | Bit 1: 1 (立即配置变更)<br>Bit 9: 1 (CEL[11:10] 有效)        |

  

  **Table 8-227: Supported Feature Entry for the CVME Threshold Feature**

  此表定义了高级可编程纠正性挥发性内存错误阈值（CVME Threshold）特性的入口结构5。

  | 字节偏移 | 长度 (字节) | 属性描述                | 值                                                           |
  | -------- | ----------- | ----------------------- | ------------------------------------------------------------ |
  | 00h      | 10h         | **Feature Identifier**  | **1478ad9d-ce00-4733-9db8-f392a4c2d0cc**                     |
  | 10h      | 2           | Feature Index           | 设备特定                                                     |
  | 12h      | 2           | Get Feature Size        | 20h (32 字节)                                                |
  | 14h      | 2           | Set Feature Size        | 20h (32 字节)                                                |
  | 16h      | 4           | **Attribute Flags**     | **Bit 0: 1 (可更改)**<br>Bits[3:1]: 000b (无持久性)<br>Bit 4: 0<br>Bit 5: 1 (支持默认选择)<br>Bit 6: 0 |
  | 1Ah      | 1           | Get Feature Version     | 01h                                                          |
  | 1Bh      | 1           | Set Feature Version     | 01h                                                          |
  | 1Ch      | 2           | **Set Feature Effects** | Bit 1: 1 (立即配置变更)<br>Bit 9: 1 (CEL[11:10] 有效)        |

  

  **二、 JEDEC-319 (JESD319) Table 35 提取内容**

  根据 JESD319 规范的 **Table 35 (Supported Feature Entries)**，以下是四个主要特性的具体属性6more_horiz：

  **1. Addressing Policy (地址策略)**

  | 属性描述                | 值                                                           |
  | ----------------------- | ------------------------------------------------------------ |
  | **Feature Identifier**  | **f182ccf8-72bd-11ee-b962-0242ac120002**                     |
  | Feature Index           | 设备特定                                                     |
  | Get Feature Size        | 02h                                                          |
  | Set Feature Size        | 02h                                                          |
  | **Attribute Flags**     | Bit 0: 1 (可更改)<br>Bits[3:1]: 100b (冷复位持久)<br>Bit 4: 0 (不跨固件更新持久)<br>Bit 5: 1 (支持默认选择)<br>Bit 6: 1 (支持保存选择) |
  | **Set Feature Effects** | Bit 0: 1 (冷复位后配置变更)<br>Bit 9: 1 (SFE[11:10] 有效)    |

  **2. RAS Features (RAS 特性)**

  | 属性描述                | 值                                                           |
  | ----------------------- | ------------------------------------------------------------ |
  | **Feature Identifier**  | **5174e599-1430-433e-af4b-5772bae6cc91**                     |
  | Feature Index           | 设备特定                                                     |
  | Get Feature Size        | 13h                                                          |
  | Set Feature Size        | 0Ch                                                          |
  | **Attribute Flags**     | Bit 0: 1 (可更改)<br>Bits[3:1]: 100b (冷复位持久)<br>Bit 4: 0<br>Bit 5: 1 (支持默认选择)<br>Bit 6: 1 (支持保存选择) |
  | **Set Feature Effects** | Bit 0: 1 (冷复位后配置变更)<br>Bit 9: 1 (SFE[11:10] 有效)    |

  **3. CMC Refresh (CMC 刷新)**

  | 属性描述                | 值                                                           |
  | ----------------------- | ------------------------------------------------------------ |
  | **Feature Identifier**  | **b44897af-bddb-4e9b-9d74-dbab49062f7b**                     |
  | Feature Index           | 设备特定                                                     |
  | Get Feature Size        | 02h                                                          |
  | Set Feature Size        | 02h                                                          |
  | **Attribute Flags**     | Bit 0: 1 (可更改)<br>Bits[3:1]: 100b (冷复位持久)<br>Bit 4: 0<br>Bit 5: 1 (支持默认选择)<br>Bit 6: 1 (支持保存选择) |
  | **Set Feature Effects** | Bit 0: 1 (冷复位后配置变更)<br>Bit 9: 1 (SFE[11:10] 有效)    |

  **4. Dual Port (双端口)**

  | 属性描述                | 值                                                           |
  | ----------------------- | ------------------------------------------------------------ |
  | **Feature Identifier**  | **b00726e4-de86-4205-b27f-b0bb6825660d**                     |
  | Feature Index           | 设备特定                                                     |
  | Get Feature Size        | 21h                                                          |
  | Set Feature Size        | 21h                                                          |
  | **Attribute Flags**     | Bit 0: 1 (可更改)<br>Bits[3:1]: 100b (冷复位持久)<br>Bit 4: 0<br>Bit 5: 1 (支持默认选择)<br>Bit 6: 1 (支持保存选择) |
  | **Set Feature Effects** | Bit 0: 1 (冷复位后配置变更)<br>Bit 9: 1 (SFE[11:10] 有效)    |

## Table 36: Addressing Policy Feature Readable Attributes

| Description | Byte Offset | Length in Bytes |
|-------------|-------------|-----------------|
| Page Policy: 00h = Open, 01h = Closed, 02h = Adaptive, 03h-FFh = Vendor defined | 00h | 1h |
| Interleave Modes: 00h = Linear, 01h = Open Page, 02h = Closed Page, 03h = 3DS Open Page, 04h = 3DS Closed Page, 05h-FFh = Vendor defined config | 01h | 1h |

---

## Table 37: Addressing Policy Feature Writeable Attributes

| Description | Byte Offset | Length in Bytes |
|-------------|-------------|-----------------|
| Page Policy: 00h = Open, 01h = Closed, 02h = Adaptive, 03h-FFh = Vendor defined | 00h | 1h |
| Interleave Modes: 00h = Linear, 01h = Open Page, 02h = Closed Page, 03h = 3DS Open Page, 04h = 3DS Closed Page, 05h-FFh = Vendor defined config | 01h | 1h |

---

## Table 38: RAS Features Readable Attributes

| Description | Byte Offset | Length in Bytes |
|-------------|-------------|-----------------|
| Informational Event Log Count (00h-FFh: count of 0 to 255) | 00h | 1h |
| Warning Event Log Count (00h-FFh: count of 0 to 255) | 01h | 1h |
| Failure Event Log Count (00h-FFh: count of 0 to 255) | 02h | 1h |
| Fatal Event Log Count (00h-FFh: count of 0 to 255) | 03h | 1h |
| DRAM ECC: 00h = Single bit error detect and correct, 01h = Multi-bit error detect and correct, 02h-FFh = Vendor defined | 04h | 1h |
| Single Device Failure Correction Mode: 00h = Disabled, 01h = Enabled, 02h-FFh = Vendor defined config | 05h | 1h |
| Demand Scrubbing: 00h = Disabled, 01h = Enabled, 02h-FFh = Reserved | 06h | 1h |
| Write CRC Supported: 00h = Not supported, 01h = Supported but disabled, 02h = Illegal, 03h = Supported and enabled, 04h-FFh = Reserved | 07h | 1h |
| Write CRC Max Retries Configured (00h-FFh: Retry Limit of 0 to 255) | 08h | 1h |
| Write CRC Max Retries Supported (00h-FFh: Retry Limit of 0 to 255) | 09h | 1h |
| Read CRC: 00h = Not supported, 01h = Supported disabled, 02h = Illegal, 03h = Supported enabled, 04h-FFh = Reserved | 0Ah | 1h |
| Read CRC Retries Configured (00h-FFh: Retry Limit of 0 to 255) | 0Bh | 1h |
| Read CRC Max Retries Supported (00h-FFh: Retry Limit of 0 to 255) | 0Ch | 1h |
| CA Parity Error Detection: 00h = Not supported, 01h = Supported disabled, 02h = Illegal, 03h = Supported enabled, 04h-FFh = Reserved | 0Dh | 1h |
| CA Parity Consecutive Retries Configured (00h-FFh: Retry Limit of 0 to 255) | 0Eh | 1h |
| CA Parity Max Retries Supported (00h-FFh: Retry Limit of 0 to 255) | 0Fh | 1h |
| Signal_viral_for_fatal_error: 00h = Disabled, 01h = Enabled, 02h-FFh = Reserved | 10h | 1h |
| Write_pscrub_corr_data: 00h = Disabled, 01h = Enabled, 02h-FFh = Reserved | 11h | 1h |
| Write_poison_for_uncorr: 00h = Disabled, 01h = Enabled, 02h-FFh = Reserved | 12h | 1h |

---

## Table 39: RAS Features Writeable Attributes

| Description | Byte Offset | Length in Bytes |
|-------------|-------------|-----------------|
| DRAM ECC: 00h = Single bit error detect and correct, 01h = Multi-bit error detect and correct, 02h-FFh = Vendor defined | 00h | 1h |
| Single Device Failure Correction Mode: 00h = Disable, 01h = Enable, 02h-FFh = Vendor defined config | 01h | 1h |
| Demand Scrubbing: 00h = Disable, 01h = Enable, 02h-FFh = Reserved | 02h | 1h |
| Write CRC: 00h = Disable, 01h = Enable, 02h-FFh = Reserved | 03h | 1h |
| Write CRC Retries (00h-FFh: Retry Limit of 0 to 255) | 04h | 1h |
| Read CRC: 00h = Disable, 01h = Enable, 02h-FFh = Reserved | 05h | 1h |
| Read CRC Retries (00h-FFh: Retry Limit of 0 to 255) | 06h | 1h |
| CA Parity Error Detection: 00h = Disable, 01h = Enable, 02h-FFh = Reserved | 07h | 1h |
| CA Parity Consecutive Retries (00h-FFh: Retry Limit of 0 to 255) | 08h | 1h |
| Signal_viral_for_fatal_error: 00h = Disable, 01h = Enable, 02h-FFh = Reserved | 09h | 1h |
| Write_pscrub_corr_data: 00h = Disable, 01h = Enable, 02h-FFh = Reserved | 0Ah | 1h |
| Write_poison_for_uncorr: 00h = Disable, 01h = Enable, 02h-FFh = Reserved | 0Bh | 1h |

---

## Table 40: CMC Refresh Readable Attributes

| Description | Byte Offset | Length in Bytes |
|-------------|-------------|-----------------|
| DRFM (Directed Refresh Management): 00h = Not supported, 01h = Supported disabled, 02h = Illegal, 03h = Supported enabled, 04h-FFh = Reserved | 00h | 1h |
| PRAC (Per Row Activation Counting): 00h = Disabled, 01h = Enabled, 02h-FFh = Reserved | 01h | 1h |
| Additional Attributes: Place holder for Additional attributes dependent on section Data Integrity Risk Mitigations | 02h | 1h |

---

## Table 41: CMC Refresh Writeable Attributes

| Description | Byte Offset | Length in Bytes |
|-------------|-------------|-----------------|
| DRFM (Directed Refresh Management) Support: 00h = Disable, 01h = Enable, 02h-FFh = Reserved | 00h | 1h |
| PRAC (Per Row Activation Counting): 00h = Disable, 01h = Enable, 02h-FFh = Reserved | 01h | 1h |
| Additional Attributes: Place holder for Additional attributes dependent on section Data Integrity Risk Mitigations | 02h | 1h |

---

## Table 42: Dual Port Readable Attributes

| Description | Byte Offset | Length in Bytes |
|-------------|-------------|-----------------|
| Dual Port: 00h = Divided, 01h = Sectioned, 02h-FFh = Reserved | 00h | 1h |
| Dual Port 0 Base Address (00,000,000h to FF,FFF,FFF: Address_Boundary of 0 to 18,446,744,073,709,551,615) | 01h | 8h |
| Dual Port 0 Address Space Size (00,000,000h to FF,FFF,FFF: Port size of 0 to 18,446,744,073,709,551,616 addresses) | 09h | 8h |
| Dual Port 1 Base Address (00,000,000h to FF,FFF,FFF: Address_Boundary of 0 to 18,446,744,073,709,551,615) | 11h | 8h |
| Dual Port 1 Address Space Size (00,000,000h to FF,FFF,FFF: Port size of 0 to 18,446,744,073,709,551,616 addresses) | 19h | 8h |

---

## Table 43: Dual Port Writeable Attributes

| Description | Byte Offset | Length in Bytes |
|-------------|-------------|-----------------|
| Dual Port: 00h = Divided, 01h = Sectioned, 02h-FFh = Reserved | 00h | 1h |
| Dual Port 0 Base Address (00,000,000h to FF,FFF,FFF: Address_Boundary of 0 to 18,446,744,073,709,551,615) | 01h | 8h |
| Dual Port 0 Address Space Size (00,000,000h to FF,FFF,FFF: Port size of 0 to 18,446,744,073,709,551,616 addresses) | 09h | 8h |
| Dual Port 1 Base Address (00,000,000h to FF,FFF,FFF: Address_Boundary of 0 to 18,446,744,073,709,551,615) | 11h | 8h |
| Dual Port 1 Address Space Size (00,000,000h to FF,FFF,FFF: Port size of 0 to 18,446,744,073,709,551,616 addresses) | 19h | 8h |

