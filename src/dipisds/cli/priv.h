/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISDS_CLI_PRIV_H
#define DIPISDS_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_COLOR 1000
#define OPT_RET_ADDR 1001
#define OPT_RET_RTX_TIME 1002
#define OPT_RET_RTX_PT 1003
#define OPT_RET_MC 1004
#define OPT_RET_MC_PORT 1005
#define OPT_RET_RSI_MC_RET 1012
#define OPT_FCC_ADDR 1006
#define OPT_FCC_RTX_TIME 1007
#define OPT_FCC_RTX_PT 1008
#define OPT_FCC_RESOLVE_BY_PORT 1013
#define OPT_FCC_RESOLVE_BASE_PORT 1014
#define OPT_FCC_RESOLVE_MAX_CHANNELS 1015
#define OPT_AL_FEC_ADDR 1028
#define OPT_AL_FEC_PT 1029
#define OPT_METRICS 1009
#define OPT_METRICS_ID 1010
#define OPT_METRICS_INTERVAL 1011
#define OPT_PACKAGES 1016
#define OPT_CELLS 1017
#define OPT_RMS_NAME 1018
#define OPT_RMS_LANG 1019
#define OPT_RMS_LOCATION 1020
#define OPT_RMS_LOGO 1021
#define OPT_FUS_NAME 1022
#define OPT_FUS_LANG 1023
#define OPT_FUS_ID 1024
#define OPT_FUS_ANNOUNCE 1025
#define OPT_FUS_LOGO 1026
#define OPT_DSCP 1027
#define OPT_CONFIG_STRICT 1031
#define OPT_CONFIGTEST 1030

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

#define OPT_UNHANDLED 4

typedef struct {
  config_t *cfg;
  int cli_mode;
} sds_opt_t;

/* ARGS_OK: handled. OPT_UNHANDLED: not this group */
args_status_t sds_opt_general(sds_opt_t *p, int c);
args_status_t sds_opt_ret(sds_opt_t *p, int c);
args_status_t sds_opt_rms(sds_opt_t *p, int c);

void sds_print_help(void);
args_status_t sds_cli_check(config_t *cfg);

int sds_mcast_parse(const char *s, config_t *cfg);
int sds_ret_addr_parse(const char *s, char *addr_out, size_t addr_cap, unsigned *port_out);
int sds_has_suffix(const char *s, const char *sfx);

#endif
