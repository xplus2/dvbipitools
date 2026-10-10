/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_CLI_PRIV_H
#define DIPIFCCRET_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_CHANNEL_IDLE_TIMEOUT 1005
#define OPT_COLOR 1002
#define OPT_NO_RET 1003
#define OPT_NO_MC_RET 1001
#define OPT_MAX_RET_CLIENTS 1020
#define OPT_RET_CLIENT_IDLE_TIMEOUT 1021
#define OPT_NO_RSI 1006
#define OPT_RSI_INTERVAL 1007
#define OPT_RSI_MC_RET 1008
#define OPT_RSI_HOSTNAME 1013
#define OPT_NO_FCC 1004
#define OPT_MAX_BUFFER_FILL_BOUND 1014
#define OPT_FCC_RESOLVE_BY_PORT 1015
#define OPT_FCC_RESOLVE_BASE_PORT 1016
#define OPT_CONGESTION_NACK_THRESHOLD 1017
#define OPT_FCC_RANGE 1018
#define OPT_FCC_CLIENT_RANGE 1019
#define OPT_METRICS 1022
#define OPT_METRICS_ID 1023
#define OPT_METRICS_INTERVAL 1024
#define OPT_METRICS_INSPECT_TS 1027
#define OPT_CONFIG_STRICT 1026
#define OPT_CONFIGTEST 1025
#define OPT_CPU_AFFINITY 1028
#define OPT_RET_CLIENT_RATE 1029
#define OPT_RET_MC_DEDUP 1030
#define OPT_FCC_CLIENT_RATE 1031

#define RATE_MAX 1000000 /* per-source rate cap, keeps burst depth math in range */

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

#define OPT_UNHANDLED 4

typedef struct {
  config_t *cfg;
} fccret_opt_t;

/* ARGS_OK: handled. OPT_UNHANDLED: not this group */
args_status_t fccret_opt_general(fccret_opt_t *p, int c);
args_status_t fccret_opt_fcc(fccret_opt_t *p, int c);

void fccret_print_help(void);
args_status_t fccret_cli_check(config_t *cfg);

#endif
