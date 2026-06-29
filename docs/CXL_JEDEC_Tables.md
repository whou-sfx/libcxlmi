# CXL and JEDEC-319 Specification Tables

## Source Documents
- CXL Specification Revision 3.2, Version 1.0 (October 2, 2024)
- JEDEC Standard No. 319 - Memory Controller Standard for Compute Express Link (CXL) (September 2024)

---


# CXL Specification Tables

## Table 8-127: Supported Feature Entry for the sPPR Feature

| Field | Description |
|-------|-------------|
| Feature Identifier | UUID for sPPR Feature |
| Feature Index | 0Ah |
| Get Feature Size | 0Ah |
| Set Feature Size | 0Ah |

### sPPR Feature Readable Attributes (Table 8-128)

| Byte Offset | Length | Description |
|-------------|--------|-------------|
| 00h | 1h | sPPR Mode: 00h = Disabled, 01h = Host Initiated, 02h = Device Initiated, 03h-FFh = Reserved |
| 01h | 1h | sPPR Status: 00h = Not in progress, 01h = In progress, 02h-FFh = Reserved |
| 02h | 1h | sPPR Progress: Percentage complete (0-100) |
| 03h | 1h | sPPR Result: 00h = Success, 01h = Failure, 02h-FFh = Reserved |
| 04h | 4h | sPPR Address: DPA of the address being repaired |
| 08h | 2h | sPPR Row: Row address being repaired |

### sPPR Feature Writable Attributes (Table 8-129)

| Byte Offset | Length | Description |
|-------------|--------|-------------|
| 00h | 1h | sPPR Mode: 00h = Disable, 01h = Host Initiated, 02h = Device Initiated |
| 01h | 1h | sPPR Command: 00h = No operation, 01h = Start sPPR, 02h = Abort sPPR |
| 02h | 4h | sPPR Address: DPA of the address to repair |
| 06h | 2h | sPPR Row: Row address to repair |

---

## Table 8-130: Supported Feature Entry for the hPPR Feature

| Field | Description |
|-------|-------------|
| Feature Identifier | UUID for hPPR Feature |
| Feature Index | 0Bh |
| Get Feature Size | 06h |
| Set Feature Size | 06h |

### hPPR Feature Readable Attributes (Table 8-131)

| Byte Offset | Length | Description |
|-------------|--------|-------------|
| 00h | 1h | hPPR Mode: 00h = Disabled, 01h = Host Initiated, 02h = Device Initiated, 03h-FFh = Reserved |
| 01h | 1h | hPPR Status: 00h = Not in progress, 01h = In progress, 02h-FFh = Reserved |
| 02h | 1h | hPPR Progress: Percentage complete (0-100) |
| 03h | 1h | hPPR Result: 00h = Success, 01h = Failure, 02h-FFh = Reserved |
| 04h | 1h | hPPR Bank: Bank address being repaired |
| 05h | 1h | hPPR Row: Row address being repaired |

### hPPR Feature Writable Attributes (Table 8-132)

| Byte Offset | Length | Description |
|-------------|--------|-------------|
| 00h | 1h | hPPR Mode: 00h = Disable, 01h = Host Initiated, 02h = Device Initiated |
| 01h | 1h | hPPR Command: 00h = No operation, 01h = Start hPPR, 02h = Abort hPPR |
| 02h | 1h | hPPR Bank: Bank address to repair |
| 03h | 1h | hPPR Row: Row address to repair |
| 04h | 2h | Reserved |

---

## Table 8-221: Supported Feature Entry for the Device Patrol Scrub Control Feature

| Field | Description |
|-------|-------------|
| Feature Identifier | UUID for Device Patrol Scrub Control Feature |
| Feature Index | 0Ch |
| Get Feature Size | 04h |
| Set Feature Size | 04h |

### Device Patrol Scrub Control Feature Readable Attributes (Table 8-222)

| Byte Offset | Length | Description |
|-------------|--------|-------------|
| 00h | 1h | Patrol Scrub Mode: 00h = Disabled, 01h = Enabled, 02h-FFh = Reserved |
| 01h | 1h | Patrol Scrub Interval: 00h = 24 hours, 01h = 12 hours, 02h = 6 hours, 03h = 3 hours, 04h-FFh = Reserved |
| 02h | 1h | Patrol Scrub Status: 00h = Not in progress, 01h = In progress, 02h-FFh = Reserved |
| 03h | 1h | Patrol Scrub Real-time Reporting Capable: 00h = Not capable, 01h = Capable |

### Device Patrol Scrub Control Feature Writable Attributes (Table 8-223)

| Byte Offset | Length | Description |
|-------------|--------|-------------|
| 00h | 1h | Patrol Scrub Mode: 00h = Disable, 01h = Enable |
| 01h | 1h | Patrol Scrub Interval: 00h = 24 hours, 01h = 12 hours, 02h = 6 hours, 03h = 3 hours |
| 02h | 2h | Reserved |

---

## Table 8-224: Supported Feature Entry for the DDR5 ECS Control Feature

| Field | Description |
|-------|-------------|
| Feature Identifier | UUID for DDR5 ECS Control Feature |
| Feature Index | 0Dh |
| Get Feature Size | 06h |
| Set Feature Size | 06h |

### DDR5 ECS Control Feature Readable Attributes (Table 8-225)

| Byte Offset | Length | Description |
|-------------|--------|-------------|
| 00h | 1h | ECS Mode: 00h = Disabled, 01h = Enabled, 02h-FFh = Reserved |
| 01h | 1h | ECS Interval: 00h = Default, 01h-FFh = Custom interval |
| 02h | 1h | ECS Status: 00h = Not in progress, 01h = In progress, 02h-FFh = Reserved |
| 03h | 1h | ECS Log Count: Number of ECS events logged |
| 04h | 1h | ECS Log Overflow: 00h = No overflow, 01h = Overflow occurred |
| 05h | 1h | Reserved |

### DDR5 ECS Control Feature Writable Attributes (Table 8-226)

| Byte Offset | Length | Description |
|-------------|--------|-------------|
| 00h | 1h | ECS Mode: 00h = Disable, 01h = Enable |
| 01h | 1h | ECS Interval: 00h = Default, 01h-FFh = Custom interval |
| 02h | 4h | Reserved |

---

## Table 8-227: Supported Feature Entry for the Advanced Programmable Corrected Volatile Memory Error Threshold Feature

| Field | Description |
|-------|-------------|
| Feature Identifier | UUID for Advanced Programmable CVME Threshold Feature |
| Feature Index | 0Eh |
| Get Feature Size | 10h |
| Set Feature Size | 10h |

### Advanced Programmable CVME Threshold Feature Readable Attributes (Table 8-228)

| Byte Offset | Length | Description |
|-------------|--------|-------------|
| 00h | 1h | Threshold Enable: 00h = Disabled, 01h = Enabled |
| 01h | 1h | Threshold Type: 00h = Correctable, 01h = Uncorrectable |
| 02h | 2h | Threshold Value: 16-bit threshold count |
| 04h | 2h | Current Count: Current error count |
| 06h | 2h | Time Window: Time window for threshold evaluation |
| 08h | 1h | Leaky Bucket Enable: 00h = Disabled, 01h = Enabled |
| 09h | 1h | Leaky Bucket Rate: Decay rate for leaky bucket |
| 0Ah | 2h | Leaky Bucket Value: Current leaky bucket value |
| 0Ch | 1h | Event Flags: Bit 0 = Threshold reached, Bit 1 = Overflow |
| 0Dh | 1h | Reserved |
| 0Eh | 2h | CVME Count at Event: Error count when threshold event occurred |

### Advanced Programmable CVME Threshold Feature Writable Attributes (Table 8-229)

| Byte Offset | Length | Description |
|-------------|--------|-------------|
| 00h | 1h | Threshold Enable: 00h = Disable, 01h = Enable |
| 01h | 1h | Threshold Type: 00h = Correctable, 01h = Uncorrectable |
| 02h | 2h | Threshold Value: 16-bit threshold count |
| 04h | 2h | Time Window: Time window for threshold evaluation |
| 06h | 1h | Leaky Bucket Enable: 00h = Disable, 01h = Enable |
| 07h | 1h | Leaky Bucket Rate: Decay rate for leaky bucket |
| 08h | 1h | Event Flags Clear: Write 1 to clear flags |
| 09h | 7h | Reserved |


---


# JEDEC Standard No. 319 Tables

## Table 35: Supported Feature Entries

### Addressing Policy Feature

| Attribute | Value |
|-----------|-------|
| Feature Identifier | f182ccf8-72bd-11ee-b962-0242ac120002 |
| Feature Index | 02h |
| Get Feature Size | 02h |
| Set Feature Size | 02h |

**Attribute Flags:**
- Bit[0]: 1 - Feature attributes can be changed
- Bits[3:1]: 100b - Cold reset
- Bit[4]: 0 – Does not persist across firmware update
- Bit[5]: 1 - Default selection value
- Bit[6]: 1 - Saved selection supported
- Bits[31:7]: Reserved

**Get Feature Version:** 0
**Set Feature Version:** 0

**Set Feature Effects (SFE):**
- Bit[0]: 1 (Configuration Change after Cold Reset)
- Bit[1]: 0 (Immediate Configuration Change)
- Bit[2]: 0 (Immediate Data Change)
- Bit[3]: 0 (Immediate Policy Change)
- Bit[4]: Vendor-specific value (Immediate Log Change)
- Bit[5]: 0 (Security State Change)
- Bit[6]: 0 (Background Operation)
- Bit[7]: Vendor-specific value (Secondary Mailbox Supported)
- Bit[8]: 0 (Request Abort Background Operation Supported)
- Bit[9]: 1 (SFE[11:10] Valid)
- Bit[10]: 0 (Configuration Change after Conventional Reset)
- Bit[11]: 0 (Configuration Change after CXL Reset)

---

### CMC Refresh Feature

| Attribute | Value |
|-----------|-------|
| Feature Identifier | 5174e599-1430-433e-af4b-5772bae6cc91 |
| Feature Index | 13h |
| Get Feature Size | 0Ch |
| Set Feature Size | 0Ch |

**Attribute Flags:**
- Bit[0]: 1 - Feature attributes can be changed
- Bits[3:1]: 100b - Cold reset
- Bit[4]: 0 – Does not persist across firmware update
- Bit[5]: 1 - Default selection value
- Bit[6]: 1 - Saved selection supported
- Bits[31:7]: Reserved

**Get Feature Version:** 0
**Set Feature Version:** 0

**Set Feature Effects (SFE):**
- Bit[0]: 1 (Configuration Change after Cold Reset)
- Bit[1]: 0 (Immediate Configuration Change)
- Bit[2]: 0 (Immediate Data Change)
- Bit[3]: 0 (Immediate Policy Change)
- Bit[4]: Vendor-specific value (Immediate Log Change)
- Bit[5]: 0 (Security State Change)
- Bit[6]: 0 (Background Operation)
- Bit[7]: Vendor-specific value (Secondary Mailbox Supported)
- Bit[8]: 0 (Request Abort Background Operation Supported)
- Bit[9]: 1 (SFE[11:10] Valid)
- Bit[10]: 0 (Configuration Change after Conventional Reset)
- Bit[11]: 0 (Configuration Change after CXL Reset)

---

### RAS Features

| Attribute | Value |
|-----------|-------|
| Feature Identifier | b00726e4-de86-4205-b27f-b0bb6825660d |
| Feature Index | 21h |
| Get Feature Size | 21h |
| Set Feature Size | 21h |

**Attribute Flags:**
- Bit[0]: 1 - Feature attributes can be changed
- Bits[3:1]: 100b - Cold reset
- Bit[4]: 0 – Does not persist across firmware update
- Bit[5]: 1 - Default selection value
- Bit[6]: 1 - Saved selection supported
- Bits[31:7]: Reserved

**Get Feature Version:** 0
**Set Feature Version:** 0

**Set Feature Effects (SFE):**
- Bit[0]: 1 (Configuration Change after Cold Reset)
- Bit[1]: 0 (Immediate Configuration Change)
- Bit[2]: 0 (Immediate Data Change)
- Bit[3]: 0 (Immediate Policy Change)
- Bit[4]: Vendor-specific value (Immediate Log Change)
- Bit[5]: 0 (Security State Change)
- Bit[6]: 0 (Background Operation)
- Bit[7]: Vendor-specific value (Secondary Mailbox Supported)
- Bit[8]: 0 (Request Abort Background Operation Supported)
- Bit[9]: 1 (SFE[11:10] Valid)
- Bit[10]: 0 (Configuration Change after Conventional Reset)
- Bit[11]: 0 (Configuration Change after CXL Reset)

---

### Dual Port Feature

| Attribute | Value |
|-----------|-------|
| Feature Identifier | b44897af-bddb-4e9b-9d74-dbab49062f7b |
| Feature Index | 02h |
| Get Feature Size | 02h |
| Set Feature Size | 02h |

**Attribute Flags:**
- Bit[0]: 1 - Feature attributes can be changed
- Bits[3:1]: 100b - Cold reset
- Bit[4]: 0 – Does not persist across firmware update
- Bit[5]: 1 - Default selection value
- Bit[6]: 1 - Saved selection supported
- Bits[31:7]: Reserved

**Get Feature Version:** 0
**Set Feature Version:** 0

**Set Feature Effects (SFE):**
- Bit[0]: 1 (Configuration Change after Cold Reset)
- Bit[1]: 0 (Immediate Configuration Change)
- Bit[2]: 0 (Immediate Data Change)
- Bit[3]: 0 (Immediate Policy Change)
- Bit[4]: Vendor-specific value (Immediate Log Change)
- Bit[5]: 0 (Security State Change)
- Bit[6]: 0 (Background Operation)
- Bit[7]: Vendor-specific value (Secondary Mailbox Supported)
- Bit[8]: 0 (Request Abort Background Operation Supported)
- Bit[9]: 1 (SFE[11:10] Valid)
- Bit[10]: 0 (Configuration Change after Conventional Reset)
- Bit[11]: 0 (Configuration Change after CXL Reset)

---

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
