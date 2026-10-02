/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_CLI_PRIV_H
#define DIPIDESCRAMBLE_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_INSECURE 1003
#define OPT_TOKEN_HEADER 1010
#define OPT_COLOR 1000
#define OPT_BISS2_SW 1004
#define OPT_BISS2_ESW 1005
#define OPT_BISS2_ID 1006
#define OPT_BISS1_SW 1007
#define OPT_BISS2_CA_KEY 1008
#define OPT_ECM_PROFILE 1009
#define OPT_METRICS 1011
#define OPT_METRICS_ID 1012
#define OPT_METRICS_INTERVAL 1013
#define OPT_METRICS_INSPECT_TS 1029
#define OPT_METRICS_INSPECT_TS_PIDS 1030
#define OPT_MAX_SERVICES 1014
#define OPT_PROFILE 1015
#define OPT_ENCRYPTION_TYPE 1031
#define OPT_SRT_PASSPHRASE_IN 1016
#define OPT_SRT_PBKEYLEN_IN 1017
#define OPT_SRT_STREAMID_IN 1018
#define OPT_SRT_PACKETFILTER_IN 1019
#define OPT_SRT_LATENCY_IN 1020
#define OPT_SRT_PASSPHRASE 1021
#define OPT_SRT_PBKEYLEN 1022
#define OPT_SRT_STREAMID 1023
#define OPT_SRT_PACKETFILTER 1024
#define OPT_SRT_LATENCY 1025
#define OPT_STRIP_LCEVC 1026
#define OPT_CONFIG_STRICT 1028
#define OPT_CONFIGTEST 1027

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

#define OPT_UNHANDLED 4

typedef struct {
  config_t *cfg;
  int cli_out;
} dscr_opt_t;

/* ARGS_OK: handled. OPT_UNHANDLED: not this group */
args_status_t dscr_opt_general(dscr_opt_t *p, int c);
args_status_t dscr_opt_biss(dscr_opt_t *p, int c);
args_status_t dscr_opt_net(dscr_opt_t *p, int c);

void dscr_print_help(void);
args_status_t dscr_cli_check(config_t *cfg);

#endif
