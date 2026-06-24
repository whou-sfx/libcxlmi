// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * Command Effects Log (CEL) parser for mctp-cci.
 *
 * UUID: 0da9c0b5-bf41-4b78-8f79-96b1623b3f17
 * Each entry is 4 bytes: opcode (le16) + command_effect (le16).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <ccan/endian/endian.h>

#include "mctp-cci.h"

static const uint8_t cel_log_uuid[16] = {
	0x0d, 0xa9, 0xc0, 0xb5, 0xbf, 0x41, 0x4b, 0x78,
	0x8f, 0x79, 0x96, 0xb1, 0x62, 0x3b, 0x3f, 0x17
};

struct cel_opcode_name {
	uint16_t     opcode;
	const char  *name;
};

static const struct cel_opcode_name cel_opcodes[] = {
	{ 0x0001, "Identify" },
	{ 0x0002, "Background Operation Status" },
	{ 0x0003, "Get Response Message Limit" },
	{ 0x0004, "Set Response Message Limit" },
	{ 0x0005, "Request Abort Background Operation" },
	{ 0x0100, "Get Event Records" },
	{ 0x0101, "Clear Event Records" },
	{ 0x0102, "Get Event Interrupt Policy" },
	{ 0x0103, "Set Event Interrupt Policy" },
	{ 0x0104, "Get MCTP Event Interrupt Policy" },
	{ 0x0105, "Set MCTP Event Interrupt Policy" },
	{ 0x0106, "Event Notification" },
	{ 0x0200, "Get FW Info" },
	{ 0x0201, "Transfer FW" },
	{ 0x0202, "Activate FW" },
	{ 0x0300, "Get Timestamp" },
	{ 0x0301, "Set Timestamp" },
	{ 0x0400, "Get Supported Logs" },
	{ 0x0401, "Get Log" },
	{ 0x0402, "Get Log Capabilities" },
	{ 0x0403, "Clear Log" },
	{ 0x0404, "Populate Log" },
	{ 0x0405, "Get Supported Logs Sub-List" },
	{ 0x0500, "Get Supported Features" },
	{ 0x0501, "Get Feature" },
	{ 0x0502, "Set Feature" },
	{ 0x4000, "Identify Memory Device" },
	{ 0x4100, "Get Partition Info" },
	{ 0x4101, "Set Partition Info" },
	{ 0x4102, "Get LSA" },
	{ 0x4103, "Set LSA" },
	{ 0x4200, "Get Health Info" },
	{ 0x4201, "Get Alert Configuration" },
	{ 0x4202, "Set Alert Configuration" },
	{ 0x4203, "Get Shutdown State" },
	{ 0x4204, "Set Shutdown State" },
	{ 0x4300, "Get Poison List" },
	{ 0x4301, "Inject Poison" },
	{ 0x4302, "Clear Poison" },
	{ 0x4303, "Get Scan Media Capabilities" },
	{ 0x4304, "Scan Media" },
	{ 0x4305, "Get Scan Media Results" },
	{ 0x4400, "Sanitize" },
	{ 0x4401, "Secure Erase" },
	{ 0x4402, "Media Operations" },
	{ 0x4500, "Get Security State" },
	{ 0x4501, "Set Passphrase" },
	{ 0x4502, "Disable Passphrase" },
	{ 0x4503, "Unlock" },
	{ 0x4504, "Freeze Security State" },
	{ 0x4505, "Passphrase Secure Erase" },
	{ 0x4600, "Security Send" },
	{ 0x4601, "Security Receive" },
	{ 0x4700, "Get SLD QoS Control" },
	{ 0x4701, "Set SLD QoS Control" },
	{ 0x4702, "Get SLD QoS Status" },
	{ 0x4800, "Get Dynamic Capacity Configuration" },
	{ 0x4801, "Get Dynamic Capacity Extent List" },
	{ 0x4802, "Add Dynamic Capacity Response" },
	{ 0x4803, "Release Dynamic Capacity" },
	{ 0x5100, "Identify Switch Device" },
	{ 0x5101, "Get Physical Port State" },
	{ 0x5102, "Physical Port Control" },
	{ 0x5103, "Send PPB CXL.io Configuration Request" },
	{ 0x5104, "Get Domain Validation SV State" },
	{ 0x5105, "Set Domain Validation SV" },
	{ 0x5106, "Get VCS Domain Validation SV State" },
	{ 0x5107, "Get Domain Validation SV" },
	{ 0x5200, "Get Virtual CXL Switch Info" },
	{ 0x5201, "Bind vPPB" },
	{ 0x5202, "Unbind vPPB" },
	{ 0x5300, "Tunnel Management Command" },
	{ 0x5301, "Send LD CXL.io Configuration Request" },
	{ 0x5302, "Send LD CXL.io Memory Request" },
	{ 0x5400, "Get LD Info" },
	{ 0x5401, "Get LD Allocations" },
	{ 0x5402, "Set LD Allocations" },
	{ 0x5403, "Get QoS Control" },
	{ 0x5404, "Set QoS Control" },
	{ 0x5405, "Get QoS Status" },
	{ 0x5406, "Get QoS Allocated BW" },
	{ 0x5407, "Set QoS Allocated BW" },
	{ 0x5408, "Get QoS BW Limit" },
	{ 0x5409, "Set QoS BW Limit" },
	{ 0x5500, "Get Multi-Headed Info" },
	{ 0x5501, "Get Head Info" },
	{ 0x5600, "Get DCD Info" },
	{ 0x5601, "Get Host DC Region Config" },
	{ 0x5602, "Set Host DC Region Config" },
	{ 0x5603, "Get DC Region Extent List" },
	{ 0x5604, "Initiate DC Add" },
	{ 0x5605, "Initiate DC Release" },
	{ 0x5606, "DC Add Reference" },
	{ 0x5607, "DC Remove Reference" },
	{ 0x5608, "DC List Tags" },
};

int cel_uuid_match(const uint8_t uuid[16])
{
	return memcmp(uuid, cel_log_uuid, 16) == 0;
}

static const char *lookup_cel_opcode_name(uint16_t opcode)
{
	size_t i;

	for (i = 0; i < sizeof(cel_opcodes) / sizeof(cel_opcodes[0]); i++) {
		if (cel_opcodes[i].opcode == opcode)
			return cel_opcodes[i].name;
	}
	return NULL;
}

static void print_cel_opcode(uint16_t opcode)
{
	const char *name = lookup_cel_opcode_name(opcode);

	printf("Opcode: 0x%04x", opcode);
	if (name)
		printf(" (%s)", name);
	else if (opcode >= 0xc000)
		printf(" (vendor-specific)");
	else
		printf(" (cmdset=0x%02x cmd=0x%02x)",
		       (opcode >> 8) & 0xff, opcode & 0xff);
	putchar('\n');
}

static void print_cel_command_effect(uint16_t effect)
{
	printf("Command Effect: 0x%04x\n", effect);

	if (effect & (1U << 0))
		printf("  [0] Configuration Change after Cold Reset\n");
	if (effect & (1U << 1))
		printf("  [1] Immediate Configuration Change\n");
	if (effect & (1U << 2))
		printf("  [2] Immediate Data Change\n");
	if (effect & (1U << 3))
		printf("  [3] Immediate Policy Change\n");
	if (effect & (1U << 4))
		printf("  [4] Immediate Log Change\n");
	if (effect & (1U << 5))
		printf("  [5] Security State Change\n");
	if (effect & (1U << 6))
		printf("  [6] Background Operation\n");
	if (effect & (1U << 7))
		printf("  [7] Secondary Mailbox Supported\n");
	if ((effect & (1U << 6)) && (effect & (1U << 8)))
		printf("  [8] Request Abort Background Operation Supported\n");
	if (effect & (1U << 9)) {
		printf("  [9] CEL[11:10] Valid\n");
		if (effect & (1U << 10))
			printf("  [10] Configuration Change after Conventional Reset\n");
		if (effect & (1U << 11))
			printf("  [11] Configuration Change after CXL Reset\n");
	}
	if (effect & 0xf000)
		printf("  [15:12] Reserved (0x%x)\n", (effect >> 12) & 0xf);
}

void print_cel_log(const uint8_t *data, size_t len, uint32_t base_offset)
{
	size_t num_entries = len / 4;
	size_t i;

	if (len == 0) {
		printf("CEL: empty log\n");
		return;
	}

	if (len % 4 != 0)
		fprintf(stderr,
			"CEL: warning: log length %zu is not a multiple of 4, "
			"trailing %zu bytes ignored\n",
			len, len % 4);

	printf("CEL entries: %zu\n", num_entries);

	for (i = 0; i < num_entries; i++) {
		const uint8_t *entry = data + i * 4;
		uint16_t opcode = le16_to_cpu(*(const uint16_t *)&entry[0]);
		uint16_t effect = le16_to_cpu(*(const uint16_t *)&entry[2]);

		printf("\n[%zu] offset 0x%x\n", i, base_offset + (uint32_t)(i * 4));
		print_cel_opcode(opcode);
		print_cel_command_effect(effect);
	}
}
