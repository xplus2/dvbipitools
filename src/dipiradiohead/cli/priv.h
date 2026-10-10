/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_CLI_PRIV_H
#define DIPIRADIOHEAD_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_TSID 1000
#define OPT_ONID 1001
#define OPT_SID 1002
#define OPT_COLOR 1003
#define OPT_CAS_ALGO 1004
#define OPT_CAS_ECMG 1005
#define OPT_CAS_ECMG_VERSION 1006
#define OPT_CAS_SUPER_ID 1007
#define OPT_CAS_ECM_ID 1008
#define OPT_CAS_ECM_PID 1009
#define OPT_CAS_EMMG_LISTEN 1010
#define OPT_CAS_EMMG_VERSION 1011
#define OPT_CAS_EMMG_MAX_CONNS 1029
#define OPT_CAS_EMMG_REVERSE 1057
#define OPT_CAS_EMM_PID 1012
#define OPT_CAS_CP_DURATION 1013
#define OPT_CAS_RESILIENCE 1014
#define OPT_METRICS 1015
#define OPT_METRICS_ID 1016
#define OPT_METRICS_INTERVAL 1017
#define OPT_METRICS_INSPECT_TS 1061
#define OPT_METRICS_INSPECT_TS_PIDS 1062
#define OPT_JITTER_MS 1063
#define OPT_CAS_REQUIRED 1018
#define OPT_CAS_CWENC_ALGO 1048
#define OPT_CAS_CWENC_AES_MODE 1049
#define OPT_CAS_CWENC_FIXED_KEY 1050
#define OPT_CAS_CWENC_KEY_LIST_A 1051
#define OPT_CAS_CWENC_KEY_LIST_B 1052
#define OPT_CAS_FALLBACK_CLEAR 1019
#define OPT_BISS2_SW 1020
#define OPT_BISS2_EMIT_ESW 1021
#define OPT_BISS1_SW 1022
#define OPT_BISS2_CA_RECEIVERS 1023
#define OPT_BISS2_CA_SESSION_ID 1024
#define OPT_RIST_PROFILE 1025
#define OPT_RIST_SECRET 1026
#define OPT_RIST_CNAME 1027
#define OPT_RIST_BUFFER 1028
#define OPT_RIST_ENCRYPTION_TYPE 1040
#define OPT_SRT_GROUP_MODE 1030
#define OPT_SRT_PASSPHRASE 1031
#define OPT_SRT_PBKEYLEN 1032
#define OPT_SRT_STREAMID 1033
#define OPT_SRT_PACKETFILTER 1034
#define OPT_SRT_LATENCY 1035
#define OPT_DSCP 1053
#define OPT_AL_FEC 1054
#define OPT_AL_FEC_PORT 1055
#define OPT_PROVIDER 1056
#define OPT_DEFAULT_PROVIDER 1058
#define OPT_CONFIG_STRICT 1060
#define OPT_CONFIGTEST 1059

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

#define OPT_UNHANDLED 3

typedef struct {
  config_t *cfg;
  int cli_inputs;
  int cli_vendors;
  int cli_peers;
  int have_mcast;
  int any_cas_flag;
  const char *profile_arg;
  int have_secret;
  const char *srt_group_mode_arg;
} rdh_opt_t;

/* ARGS_OK: handled. OPT_UNHANDLED: not this group */
args_status_t rdh_opt_general(rdh_opt_t *p, int c);
args_status_t rdh_opt_cas(rdh_opt_t *p, int c);
args_status_t rdh_opt_net(rdh_opt_t *p, int c);

void rdh_print_help(void);
args_status_t rdh_cli_check(config_t *cfg, const rdh_opt_t *p);

int rdh_mcast_parse(const char *s, config_t *cfg);
int rdh_id_parse(const char *s, unsigned *out);
int rdh_pid_parse(const char *s, unsigned *out);
cas_vendor_t *rdh_current_cas_vendor(config_t *cfg, int cli_vendors, const char *flag);

#endif
