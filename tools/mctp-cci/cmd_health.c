// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcxlmi.h>
#include "mctp-cci.h"

static int do_get_health(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                         int argc, char **argv)
{
    struct cxlmi_cmd_memdev_get_health_info_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_memdev_get_health_info(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-health-info");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }

    printf("Health Status               : 0x%x\n", rsp.health_status);
    printf("Media Status                : 0x%x\n", rsp.media_status);
    printf("Additional Status           : 0x%x\n", rsp.additional_status);
    printf("Life Used                   : %u%%\n", rsp.life_used);
    printf("Device Temperature          : %u C\n", rsp.device_temperature);
    printf("Dirty Shutdown Count        : %u\n", rsp.dirty_shutdown_count);
    printf("Corrected Volatile Errors   : %u\n", rsp.corrected_volatile_error_count);
    printf("Corrected Persistent Errors : %u\n", rsp.corrected_persistent_error_count);
    return 0;
}

static int do_get_alert(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                        int argc, char **argv)
{
    struct cxlmi_cmd_memdev_get_alert_config_rsp rsp;
    int rc;
    (void)argc; (void)argv;

    rc = cxlmi_cmd_memdev_get_alert_config(ep, ti, &rsp);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("get-alert-config");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    printf("Valid Alerts                : 0x%x\n", rsp.valid_alerts);
    printf("Programmable Alerts         : 0x%x\n", rsp.programmable_alerts);
    printf("Life Used Critical          : %u%%\n", rsp.life_used_critical_alert_threshold);
    printf("Life Used Warning           : %u%%\n", rsp.life_used_programmable_warning_threshold);
    printf("Over-Temp Critical (C)      : %u\n", rsp.device_over_temperature_critical_alert_threshold);
    printf("Under-Temp Critical (C)     : %u\n", rsp.device_under_temperature_critical_alert_threshold);
    printf("Over-Temp Warning (C)       : %u\n", rsp.device_over_temperature_programmable_warning_threshold);
    printf("Under-Temp Warning (C)      : %u\n", rsp.device_under_temperature_programmable_warning_threshold);
    printf("Corrected Volatile Warning  : %u\n", rsp.corrected_volatile_mem_error_programmable_warning_threshold);
    printf("Corrected Persistent Warning: %u\n", rsp.corrected_persistent_mem_error_programmable_warning_threshold);
    return 0;
}

static int do_set_alert(struct cxlmi_endpoint *ep, struct cxlmi_tunnel_info *ti,
                        int argc, char **argv)
{
    struct cxlmi_cmd_memdev_set_alert_config_req req = {0};
    int rc;

    if (argc >= 1) req.valid_alert_actions = strtoul(argv[0], NULL, 0);
    rc = cxlmi_cmd_memdev_set_alert_config(ep, ti, &req);
    if (rc < 0) return mctp_cci_report_libcxlmi_error("set-alert-config");
    if (rc > 0) { fprintf(stderr, "%s\n", cxlmi_cmd_retcode_tostr(rc)); return 1; }
    return 0;
}

static const struct mctp_cci_cmd health_cmds[] = {
    { "info",      "Get Health Info (4200h)",              do_get_health },
    { "get-alert", "Get Alert Configuration (4201h)",      do_get_alert  },
    { "set-alert", "Set Alert Configuration (4202h) <val>", do_set_alert  },
};

const struct mctp_cci_top health_top = {
    .name = "health",
    .help = "HEALTH_INFO_ALERTS (0x42): health info, alert config",
    .cmds = health_cmds,
    .n_cmds = sizeof(health_cmds) / sizeof(health_cmds[0]),
};
