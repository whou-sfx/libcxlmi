// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Get FW Info (Opcode 0200h).
 *
 * Sends 0200h directly via the in-band ioctl path provided by libcxlmi
 * and pretty-prints the response payload.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

static void print_fw_rev(const char *label, const char *rev)
{
	char buf[0x10 + 1];
	size_t i;

	memcpy(buf, rev, 0x10);
	buf[0x10] = '\0';
	for (i = 0; i < 0x10; i++) {
		if ((unsigned char)buf[i] < 0x20 || (unsigned char)buf[i] > 0x7e)
			buf[i] = '.';
	}
	printf("%-20s %s\n", label, buf);
}

void print_get_fw_info(const struct cxlmi_cmd_get_fw_info_rsp *fw)
{
	uint8_t active_slot = fw->slot_info & 0x7;
	uint8_t staged_slot = (fw->slot_info >> 3) & 0x7;

	printf("Slots Supported:    %u\n", fw->slots_supported);
	printf("Slot Info:          0x%02x\n", fw->slot_info);
	printf("  Active Slot:      %u\n", active_slot);
	printf("  Staged Slot:      %u\n", staged_slot);
	printf("Capabilities:       0x%02x\n", fw->caps);

	if (fw->slots_supported >= 1)
		print_fw_rev("FW Rev Slot 1:", fw->fw_rev1);
	if (fw->slots_supported >= 2)
		print_fw_rev("FW Rev Slot 2:", fw->fw_rev2);
	if (fw->slots_supported >= 3)
		print_fw_rev("FW Rev Slot 3:", fw->fw_rev3);
	if (fw->slots_supported >= 4)
		print_fw_rev("FW Rev Slot 4:", fw->fw_rev4);
}

int cmd_get_fw_info(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_get_fw_info_rsp fw;
	int rc;

	(void)argc;
	(void)argv;

	memset(&fw, 0, sizeof(fw));
	rc = cxlmi_cmd_get_fw_info(ep, NULL, &fw);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get fw info failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get fw info ioctl failed\n");
		return rc;
	}

	print_get_fw_info(&fw);
	return 0;
}
