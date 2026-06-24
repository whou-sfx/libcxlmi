# SPDX-License-Identifier: LGPL-2.1-or-later
#
# mctp-cci: a libcxlmi-based MCTP CLI for sending CCI commands.
#
executable(
    'mctp-cci',
    [
        'main.c',
        'util.c',
        'cmd_info.c',
        'cmd_events.c',
        'cmd_fw.c',
        'cmd_timestamp.c',
        'cmd_logs.c',
        'cmd_cel.c',
        'cmd_features.c',
        'cmd_identify.c',
        'cmd_partition.c',
        'cmd_health.c',
        'cmd_poison.c',
        'cmd_sanitize.c',
        'cmd_pmem.c',
        'cmd_security.c',
        'cmd_qos.c',
        'cmd_dcd.c',
        'cmd_switch.c',
        'cmd_mld.c',
        'cmd_dcd_mgmt.c',
        'cmd_vendor.c',
    ],
    dependencies: libcxlmi_dep,
    include_directories: [inc]
)
