// SPDX-License-Identifier: LGPL-2.1-or-later
#include "mctp-cci.h"
const struct mctp_cci_top pmem_top = {
    .name = "pmem",
    .help = "PERSISTENT_MEM (0x45) — use 'security' for security subcommands",
    .cmds = NULL,
    .n_cmds = 0,
};
