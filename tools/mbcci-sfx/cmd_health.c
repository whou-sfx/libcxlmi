// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * mbcci-sfx: Get Health Info (Opcode 4200h).
 *
 * Sends 4200h directly via the in-band ioctl path provided by libcxlmi
 * and pretty-prints the response payload.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <libcxlmi.h>

#include "mbcci-sfx.h"

void print_memdev_health_info(
	const struct cxlmi_cmd_memdev_get_health_info_rsp *hi)
{
	printf("Health Status:                 0x%02x\n", hi->health_status);
	printf("Media Status:                  0x%02x\n", hi->media_status);
	printf("Additional Status:             0x%02x\n", hi->additional_status);
	printf("Life Used:                       %u%%\n", hi->life_used);
	printf("Device Temperature:              %d C\n",
	       (int16_t)hi->device_temperature);
	printf("Dirty Shutdown Count:            %u\n", hi->dirty_shutdown_count);
	printf("Corrected Volatile Error Count:  %u\n",
	       hi->corrected_volatile_error_count);
	printf("Corrected Persistent Error Count:%u\n",
	       hi->corrected_persistent_error_count);
}

int cmd_get_health_info(struct cxlmi_endpoint *ep, int argc, char **argv)
{
	struct cxlmi_cmd_memdev_get_health_info_rsp hi;
	int rc;

	(void)argc;
	(void)argv;

	memset(&hi, 0, sizeof(hi));
	rc = cxlmi_cmd_memdev_get_health_info(ep, NULL, &hi);
	if (rc) {
		if (rc > 0)
			fprintf(stderr, "get health info failed: %s\n",
				cxlmi_cmd_retcode_tostr(rc));
		else
			fprintf(stderr, "get health info ioctl failed\n");
		return rc;
	}

	print_memdev_health_info(&hi);
	return 0;
}
