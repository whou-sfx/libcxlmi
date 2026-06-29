// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: decode Get Feature (0501h) readable attribute payloads.
 *
 * Field layouts from docs/CXL_JEDEC_Tables.md and docs/cxl_features.md.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "mbcci-sfx.h"

static bool field_in_window(uint16_t offset, uint16_t count,
			    uint16_t field_off, uint16_t field_len)
{
	uint16_t field_end = field_off + field_len;
	uint16_t win_end = offset + count;

	return field_off < win_end && field_end > offset;
}

static const uint8_t *field_ptr(const uint8_t *buf, uint16_t offset,
				uint16_t field_off)
{
	return buf + (field_off - offset);
}

static uint8_t field_u8(const uint8_t *buf, uint16_t offset, uint16_t field_off)
{
	return *field_ptr(buf, offset, field_off);
}

static uint16_t field_u16(const uint8_t *buf, uint16_t offset,
			  uint16_t field_off)
{
	const uint8_t *p = field_ptr(buf, offset, field_off);

	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t field_u32(const uint8_t *buf, uint16_t offset,
			  uint16_t field_off)
{
	const uint8_t *p = field_ptr(buf, offset, field_off);

	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t field_u64(const uint8_t *buf, uint16_t offset,
			  uint16_t field_off)
{
	const uint8_t *p = field_ptr(buf, offset, field_off);
	uint64_t v = 0;
	int i;

	for (i = 7; i >= 0; i--)
		v = (v << 8) | p[i];
	return v;
}

static const char *ppr_mode(uint8_t v)
{
	switch (v) {
	case 0: return "Disabled";
	case 1: return "Host Initiated";
	case 2: return "Device Initiated";
	default: return "Reserved";
	}
}

static const char *progress_status(uint8_t v)
{
	switch (v) {
	case 0: return "Not in progress";
	case 1: return "In progress";
	default: return "Reserved";
	}
}

static const char *ppr_result(uint8_t v)
{
	switch (v) {
	case 0: return "Success";
	case 1: return "Failure";
	default: return "Reserved";
	}
}

static const char *disabled_enabled(uint8_t v)
{
	switch (v) {
	case 0: return "Disabled";
	case 1: return "Enabled";
	default: return "Reserved";
	}
}

static const char *support_state(uint8_t v)
{
	switch (v) {
	case 0: return "Not supported";
	case 1: return "Supported disabled";
	case 2: return "Illegal";
	case 3: return "Supported enabled";
	default: return "Reserved";
	}
}

static const char *patrol_interval(uint8_t v)
{
	switch (v) {
	case 0: return "24 hours";
	case 1: return "12 hours";
	case 2: return "6 hours";
	case 3: return "3 hours";
	default: return "Reserved";
	}
}

static const char *capable_state(uint8_t v)
{
	switch (v) {
	case 0: return "Not capable";
	case 1: return "Capable";
	default: return "Reserved";
	}
}

static const char *overflow_state(uint8_t v)
{
	switch (v) {
	case 0: return "No overflow";
	case 1: return "Overflow occurred";
	default: return "Reserved";
	}
}

static const char *threshold_type(uint8_t v)
{
	switch (v) {
	case 0: return "Correctable";
	case 1: return "Uncorrectable";
	default: return "Reserved";
	}
}

static const char *page_policy(uint8_t v)
{
	switch (v) {
	case 0: return "Open";
	case 1: return "Closed";
	case 2: return "Adaptive";
	default: return "Vendor defined";
	}
}

static const char *interleave_mode(uint8_t v)
{
	switch (v) {
	case 0: return "Linear";
	case 1: return "Open Page";
	case 2: return "Closed Page";
	case 3: return "3DS Open Page";
	case 4: return "3DS Closed Page";
	default: return "Vendor defined";
	}
}

static const char *dram_ecc(uint8_t v)
{
	switch (v) {
	case 0: return "Single bit error detect and correct";
	case 1: return "Multi-bit error detect and correct";
	default: return "Vendor defined";
	}
}

static const char *dual_port_mode(uint8_t v)
{
	switch (v) {
	case 0: return "Divided";
	case 1: return "Sectioned";
	default: return "Reserved";
	}
}

void print_feature_payload(uint16_t offset, uint16_t count, const uint8_t *buf)
{
	uint16_t i;

	printf("Raw data:\n");
	for (i = 0; i < count; i++) {
		if (i % 16 == 0)
			printf("%04x: ", offset + i);
		printf("%02x ", buf[i]);
		if ((i + 1) % 16 == 0 || i + 1 == count)
			printf("\n");
	}
}

static void print_sppr(uint16_t offset, uint16_t count, const uint8_t *buf)
{
	printf("Readable attributes (sPPR):\n");
	if (field_in_window(offset, count, 0x00, 1))
		printf("  sPPR Mode:     0x%02x (%s)\n", field_u8(buf, offset, 0x00),
		       ppr_mode(field_u8(buf, offset, 0x00)));
	if (field_in_window(offset, count, 0x01, 1))
		printf("  sPPR Status:   0x%02x (%s)\n", field_u8(buf, offset, 0x01),
		       progress_status(field_u8(buf, offset, 0x01)));
	if (field_in_window(offset, count, 0x02, 1))
		printf("  sPPR Progress: %u%%\n", field_u8(buf, offset, 0x02));
	if (field_in_window(offset, count, 0x03, 1))
		printf("  sPPR Result:   0x%02x (%s)\n", field_u8(buf, offset, 0x03),
		       ppr_result(field_u8(buf, offset, 0x03)));
	if (field_in_window(offset, count, 0x04, 4))
		printf("  sPPR Address:  0x%08x\n", field_u32(buf, offset, 0x04));
	if (field_in_window(offset, count, 0x08, 2))
		printf("  sPPR Row:      0x%04x\n", field_u16(buf, offset, 0x08));
}

static void print_hppr(uint16_t offset, uint16_t count, const uint8_t *buf)
{
	printf("Readable attributes (hPPR):\n");
	if (field_in_window(offset, count, 0x00, 1))
		printf("  hPPR Mode:     0x%02x (%s)\n", field_u8(buf, offset, 0x00),
		       ppr_mode(field_u8(buf, offset, 0x00)));
	if (field_in_window(offset, count, 0x01, 1))
		printf("  hPPR Status:   0x%02x (%s)\n", field_u8(buf, offset, 0x01),
		       progress_status(field_u8(buf, offset, 0x01)));
	if (field_in_window(offset, count, 0x02, 1))
		printf("  hPPR Progress: %u%%\n", field_u8(buf, offset, 0x02));
	if (field_in_window(offset, count, 0x03, 1))
		printf("  hPPR Result:   0x%02x (%s)\n", field_u8(buf, offset, 0x03),
		       ppr_result(field_u8(buf, offset, 0x03)));
	if (field_in_window(offset, count, 0x04, 1))
		printf("  hPPR Bank:     0x%02x\n", field_u8(buf, offset, 0x04));
	if (field_in_window(offset, count, 0x05, 1))
		printf("  hPPR Row:      0x%02x\n", field_u8(buf, offset, 0x05));
}

static void print_partial_scrub(uint16_t offset, uint16_t count,
				const uint8_t *buf)
{
	printf("Readable attributes (Device Patrol Scrub):\n");
	if (field_in_window(offset, count, 0x00, 1))
		printf("  Patrol Scrub Mode:      0x%02x (%s)\n",
		       field_u8(buf, offset, 0x00),
		       disabled_enabled(field_u8(buf, offset, 0x00)));
	if (field_in_window(offset, count, 0x01, 1))
		printf("  Patrol Scrub Interval:  0x%02x (%s)\n",
		       field_u8(buf, offset, 0x01),
		       patrol_interval(field_u8(buf, offset, 0x01)));
	if (field_in_window(offset, count, 0x02, 1))
		printf("  Patrol Scrub Status:    0x%02x (%s)\n",
		       field_u8(buf, offset, 0x02),
		       progress_status(field_u8(buf, offset, 0x02)));
	if (field_in_window(offset, count, 0x03, 1))
		printf("  Real-time Reporting:    0x%02x (%s)\n",
		       field_u8(buf, offset, 0x03),
		       capable_state(field_u8(buf, offset, 0x03)));
}

static void print_ddr5_ecs(uint16_t offset, uint16_t count, const uint8_t *buf)
{
	printf("Readable attributes (DDR5 ECS):\n");
	if (field_in_window(offset, count, 0x00, 1))
		printf("  ECS Mode:         0x%02x (%s)\n",
		       field_u8(buf, offset, 0x00),
		       disabled_enabled(field_u8(buf, offset, 0x00)));
	if (field_in_window(offset, count, 0x01, 1))
		printf("  ECS Interval:     0x%02x (%s)\n",
		       field_u8(buf, offset, 0x01),
		       field_u8(buf, offset, 0x01) == 0 ? "Default" : "Custom");
	if (field_in_window(offset, count, 0x02, 1))
		printf("  ECS Status:       0x%02x (%s)\n",
		       field_u8(buf, offset, 0x02),
		       progress_status(field_u8(buf, offset, 0x02)));
	if (field_in_window(offset, count, 0x03, 1))
		printf("  ECS Log Count:    %u\n", field_u8(buf, offset, 0x03));
	if (field_in_window(offset, count, 0x04, 1))
		printf("  ECS Log Overflow: 0x%02x (%s)\n",
		       field_u8(buf, offset, 0x04),
		       overflow_state(field_u8(buf, offset, 0x04)));
}

static void print_cvme(uint16_t offset, uint16_t count, const uint8_t *buf)
{
	uint8_t flags;

	printf("Readable attributes (CVME Threshold):\n");
	if (field_in_window(offset, count, 0x00, 1))
		printf("  Threshold Enable:   0x%02x (%s)\n",
		       field_u8(buf, offset, 0x00),
		       disabled_enabled(field_u8(buf, offset, 0x00)));
	if (field_in_window(offset, count, 0x01, 1))
		printf("  Threshold Type:     0x%02x (%s)\n",
		       field_u8(buf, offset, 0x01),
		       threshold_type(field_u8(buf, offset, 0x01)));
	if (field_in_window(offset, count, 0x02, 2))
		printf("  Threshold Value:    %u\n",
		       field_u16(buf, offset, 0x02));
	if (field_in_window(offset, count, 0x04, 2))
		printf("  Current Count:      %u\n",
		       field_u16(buf, offset, 0x04));
	if (field_in_window(offset, count, 0x06, 2))
		printf("  Time Window:        %u\n",
		       field_u16(buf, offset, 0x06));
	if (field_in_window(offset, count, 0x08, 1))
		printf("  Leaky Bucket Enable: 0x%02x (%s)\n",
		       field_u8(buf, offset, 0x08),
		       disabled_enabled(field_u8(buf, offset, 0x08)));
	if (field_in_window(offset, count, 0x09, 1))
		printf("  Leaky Bucket Rate:   %u\n",
		       field_u8(buf, offset, 0x09));
	if (field_in_window(offset, count, 0x0a, 2))
		printf("  Leaky Bucket Value:  %u\n",
		       field_u16(buf, offset, 0x0a));
	if (field_in_window(offset, count, 0x0c, 1)) {
		flags = field_u8(buf, offset, 0x0c);
		printf("  Event Flags:         0x%02x", flags);
		if (flags & 0x01)
			printf(" [Threshold reached]");
		if (flags & 0x02)
			printf(" [Overflow]");
		printf("\n");
	}
	if (field_in_window(offset, count, 0x0e, 2))
		printf("  CVME Count at Event: %u\n",
		       field_u16(buf, offset, 0x0e));
}

static void print_address_policy(uint16_t offset, uint16_t count,
				 const uint8_t *buf)
{
	printf("Readable attributes (Addressing Policy):\n");
	if (field_in_window(offset, count, 0x00, 1))
		printf("  Page Policy:      0x%02x (%s)\n",
		       field_u8(buf, offset, 0x00),
		       page_policy(field_u8(buf, offset, 0x00)));
	if (field_in_window(offset, count, 0x01, 1))
		printf("  Interleave Mode:  0x%02x (%s)\n",
		       field_u8(buf, offset, 0x01),
		       interleave_mode(field_u8(buf, offset, 0x01)));
}

static void print_ras(uint16_t offset, uint16_t count, const uint8_t *buf)
{
	printf("Readable attributes (RAS Features):\n");
	if (field_in_window(offset, count, 0x00, 1))
		printf("  Info Event Log Count:    %u\n",
		       field_u8(buf, offset, 0x00));
	if (field_in_window(offset, count, 0x01, 1))
		printf("  Warning Event Log Count: %u\n",
		       field_u8(buf, offset, 0x01));
	if (field_in_window(offset, count, 0x02, 1))
		printf("  Failure Event Log Count: %u\n",
		       field_u8(buf, offset, 0x02));
	if (field_in_window(offset, count, 0x03, 1))
		printf("  Fatal Event Log Count:   %u\n",
		       field_u8(buf, offset, 0x03));
	if (field_in_window(offset, count, 0x04, 1))
		printf("  DRAM ECC:                0x%02x (%s)\n",
		       field_u8(buf, offset, 0x04),
		       dram_ecc(field_u8(buf, offset, 0x04)));
	if (field_in_window(offset, count, 0x05, 1))
		printf("  SDFC Mode:               0x%02x (%s)\n",
		       field_u8(buf, offset, 0x05),
		       field_u8(buf, offset, 0x05) == 1 ?
		       "Enabled" : "Disabled/Vendor defined");
	if (field_in_window(offset, count, 0x06, 1))
		printf("  Demand Scrubbing:        0x%02x (%s)\n",
		       field_u8(buf, offset, 0x06),
		       disabled_enabled(field_u8(buf, offset, 0x06)));
	if (field_in_window(offset, count, 0x07, 1))
		printf("  Write CRC:               0x%02x (%s)\n",
		       field_u8(buf, offset, 0x07),
		       support_state(field_u8(buf, offset, 0x07)));
	if (field_in_window(offset, count, 0x08, 1))
		printf("  Write CRC Retries (cfg): %u\n",
		       field_u8(buf, offset, 0x08));
	if (field_in_window(offset, count, 0x09, 1))
		printf("  Write CRC Retries (max): %u\n",
		       field_u8(buf, offset, 0x09));
	if (field_in_window(offset, count, 0x0a, 1))
		printf("  Read CRC:                0x%02x (%s)\n",
		       field_u8(buf, offset, 0x0a),
		       support_state(field_u8(buf, offset, 0x0a)));
	if (field_in_window(offset, count, 0x0b, 1))
		printf("  Read CRC Retries (cfg):  %u\n",
		       field_u8(buf, offset, 0x0b));
	if (field_in_window(offset, count, 0x0c, 1))
		printf("  Read CRC Retries (max):  %u\n",
		       field_u8(buf, offset, 0x0c));
	if (field_in_window(offset, count, 0x0d, 1))
		printf("  CA Parity Detection:     0x%02x (%s)\n",
		       field_u8(buf, offset, 0x0d),
		       support_state(field_u8(buf, offset, 0x0d)));
	if (field_in_window(offset, count, 0x0e, 1))
		printf("  CA Parity Retries (cfg): %u\n",
		       field_u8(buf, offset, 0x0e));
	if (field_in_window(offset, count, 0x0f, 1))
		printf("  CA Parity Retries (max): %u\n",
		       field_u8(buf, offset, 0x0f));
	if (field_in_window(offset, count, 0x10, 1))
		printf("  Signal viral for fatal:  0x%02x (%s)\n",
		       field_u8(buf, offset, 0x10),
		       disabled_enabled(field_u8(buf, offset, 0x10)));
	if (field_in_window(offset, count, 0x11, 1))
		printf("  Write pscrub corr data:  0x%02x (%s)\n",
		       field_u8(buf, offset, 0x11),
		       disabled_enabled(field_u8(buf, offset, 0x11)));
	if (field_in_window(offset, count, 0x12, 1))
		printf("  Write poison for uncorr: 0x%02x (%s)\n",
		       field_u8(buf, offset, 0x12),
		       disabled_enabled(field_u8(buf, offset, 0x12)));
}

static void print_cmc_refresh(uint16_t offset, uint16_t count,
			      const uint8_t *buf)
{
	printf("Readable attributes (CMC Refresh):\n");
	if (field_in_window(offset, count, 0x00, 1))
		printf("  DRFM:                 0x%02x (%s)\n",
		       field_u8(buf, offset, 0x00),
		       support_state(field_u8(buf, offset, 0x00)));
	if (field_in_window(offset, count, 0x01, 1))
		printf("  PRAC:                 0x%02x (%s)\n",
		       field_u8(buf, offset, 0x01),
		       disabled_enabled(field_u8(buf, offset, 0x01)));
	if (field_in_window(offset, count, 0x02, 1))
		printf("  Additional Attributes: 0x%02x\n",
		       field_u8(buf, offset, 0x02));
}

static void print_dual_port(uint16_t offset, uint16_t count, const uint8_t *buf)
{
	printf("Readable attributes (Dual Port):\n");
	if (field_in_window(offset, count, 0x00, 1))
		printf("  Dual Port Mode:       0x%02x (%s)\n",
		       field_u8(buf, offset, 0x00),
		       dual_port_mode(field_u8(buf, offset, 0x00)));
	if (field_in_window(offset, count, 0x01, 8))
		printf("  Port 0 Base Address:  0x%016llx\n",
		       (unsigned long long)field_u64(buf, offset, 0x01));
	if (field_in_window(offset, count, 0x09, 8))
		printf("  Port 0 Space Size:    0x%016llx\n",
		       (unsigned long long)field_u64(buf, offset, 0x09));
	if (field_in_window(offset, count, 0x11, 8))
		printf("  Port 1 Base Address:  0x%016llx\n",
		       (unsigned long long)field_u64(buf, offset, 0x11));
	if (field_in_window(offset, count, 0x19, 8))
		printf("  Port 1 Space Size:    0x%016llx\n",
		       (unsigned long long)field_u64(buf, offset, 0x19));
}

void print_feature_data(const uint8_t feature_id[16], uint16_t offset,
			uint16_t count, const uint8_t *buf)
{
	switch (mbcci_feature_kind(feature_id)) {
	case MBCCI_FEAT_SPPR:
		print_sppr(offset, count, buf);
		break;
	case MBCCI_FEAT_HPPR:
		print_hppr(offset, count, buf);
		break;
	case MBCCI_FEAT_PARTIAL_SCRUB:
		print_partial_scrub(offset, count, buf);
		break;
	case MBCCI_FEAT_DDR5_ECS:
		print_ddr5_ecs(offset, count, buf);
		break;
	case MBCCI_FEAT_CVME:
		print_cvme(offset, count, buf);
		break;
	case MBCCI_FEAT_ADDRESS_POLICY:
		print_address_policy(offset, count, buf);
		break;
	case MBCCI_FEAT_RAS:
		print_ras(offset, count, buf);
		break;
	case MBCCI_FEAT_CMC_REFRESH:
		print_cmc_refresh(offset, count, buf);
		break;
	case MBCCI_FEAT_DUAL_PORT:
		print_dual_port(offset, count, buf);
		break;
	default:
		printf("Readable attributes: (unknown feature, raw dump)\n");
		break;
	}

	print_feature_payload(offset, count, buf);
}
