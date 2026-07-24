# SPDX-License-Identifier: LGPL-2.1-or-later
#
# This file is part of libcxlmi.
#
executable(
    'mbcci-sfx',
    [
        'main.c',
        'cmd_identify.c',
        'cmd_partition.c',
        'cmd_fw.c',
        'cmd_health.c',
        'cmd_poison.c',
        'cmd_scan_media.c',
        'cmd_media_operations.c',
        'cmd_qos.c',
        'cmd_fm.c',
        'cmd_fm_qos.c',
        'cmd_events.c',
        'cmd_vu_evtadd.c',
        'cmd_vu_dlcfg.c',
        'cmd_vu_getdevcfg.c',
        'cmd_vu_sys_cfg.c',
        'cmd_logs.c',
        'cmd_features.c',
        'cmd_feature_decode.c',
        'cmd_cel.c',
        'cmd_timestamp.c',
        'cmd_sdb_tunnel.c',
    ],
    dependencies: libcxlmi_dep,
    include_directories: [inc]
)
