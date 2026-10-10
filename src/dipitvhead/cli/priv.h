/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_CLI_PRIV_H
#define DIPITVHEAD_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_STRIP_EIT 1000
#define OPT_HBBTV 1001
#define OPT_HBBTV_ORG_ID 1002
#define OPT_HBBTV_APP_ID 1003
#define OPT_TSID 1004
#define OPT_ONID 1005
#define OPT_SID 1006
#define OPT_COLOR 1007
#define OPT_CAS_ALGO 1008
#define OPT_CAS_ECMG 1009
#define OPT_CAS_ECMG_VERSION 1010
#define OPT_CAS_SUPER_ID 1011
#define OPT_CAS_ECM_ID 1012
#define OPT_CAS_ECM_PID 1013
#define OPT_CAS_EMMG_LISTEN 1014
#define OPT_CAS_EMMG_VERSION 1015
#define OPT_CAS_EMMG_MAX_CONNS 1035
#define OPT_CAS_EMMG_REVERSE 1057
#define OPT_CAS_EMM_PID 1016
#define OPT_CAS_PIDS 1017
#define OPT_CAS_CP_DURATION 1018
#define OPT_CAS_RESILIENCE 1019
#define OPT_METRICS 1020
#define OPT_METRICS_ID 1021
#define OPT_METRICS_INTERVAL 1022
#define OPT_METRICS_INSPECT_TS 1061
#define OPT_METRICS_INSPECT_TS_PIDS 1062
#define OPT_JITTER_MS 1063
#define OPT_PCR_MODE 1064
#define OPT_PCR_LEAD_MS 1065
#define OPT_CAS_REQUIRED 1023
#define OPT_CAS_CWENC_ALGO 1048
#define OPT_CAS_CWENC_AES_MODE 1049
#define OPT_CAS_CWENC_FIXED_KEY 1050
#define OPT_CAS_CWENC_KEY_LIST_A 1051
#define OPT_CAS_CWENC_KEY_LIST_B 1052
#define OPT_CAS_FALLBACK_CLEAR 1024
#define OPT_BISS2_SW 1025
#define OPT_BISS2_EMIT_ESW 1026
#define OPT_BISS1_SW 1027
#define OPT_BISS2_CA_RECEIVERS 1028
#define OPT_BISS2_CA_SESSION_ID 1029
#define OPT_RIST_PROFILE 1030
#define OPT_RIST_SECRET 1031
#define OPT_RIST_CNAME 1032
#define OPT_RIST_BUFFER 1033
#define OPT_STRIP 1034
#define OPT_RIST_PROFILE_IN 1036
#define OPT_DSCP 1053
#define OPT_RIST_ENCRYPTION_TYPE 1066
#define OPT_RIST_ENCRYPTION_TYPE_IN 1067
#define OPT_AL_FEC 1054
#define OPT_AL_FEC_PORT 1055
#define OPT_SRT_PASSPHRASE_IN 1037
#define OPT_SRT_PBKEYLEN_IN 1038
#define OPT_SRT_STREAMID_IN 1039
#define OPT_SRT_PACKETFILTER_IN 1040
#define OPT_SRT_LATENCY_IN 1041
#define OPT_SRT_GROUP_MODE 1042
#define OPT_SRT_PASSPHRASE 1043
#define OPT_SRT_PBKEYLEN 1044
#define OPT_SRT_STREAMID 1045
#define OPT_SRT_PACKETFILTER 1046
#define OPT_SRT_LATENCY 1047
#define OPT_PROVIDER 1056
#define OPT_DEFAULT_PROVIDER 1058
#define OPT_CONFIG_STRICT 1060
#define OPT_CONFIGTEST 1059

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

#define OPT_UNHANDLED 3

#define REQUIRE_INPUT(opt) \
  do { \
    if (!in) { \
      argerr(opt " must follow -i"); \
      return ARGS_ERR; \
    } \
  } while (0)

#define CAS_VENDOR(opt) \
  do { \
    if (!vd) { \
      argerr(opt " must follow --cas-ecmg"); \
      return ARGS_ERR; \
    } \
  } while (0)

#define CHECK(call, opt) \
  do { \
    if (call) { \
      argerr(opt ": %s", err); \
      return ARGS_ERR; \
    } \
  } while (0)

/* option parse state. in/vd: scope targets, NULL until -i / --cas-ecmg seen */
typedef struct {
  config_t *cfg;
  dipitvhead_input_t *in;
  cas_vendor_t *vd;
  int cli_inputs;
  int cli_vendors;
  int cli_peers;
} tvh_opt_t;

/* ARGS_OK: handled. OPT_UNHANDLED: not this group */
args_status_t tvh_opt_input(tvh_opt_t *p, int c);
args_status_t tvh_opt_general(tvh_opt_t *p, int c);
args_status_t tvh_opt_net(tvh_opt_t *p, int c);
args_status_t tvh_opt_cas(tvh_opt_t *p, int c);

void tvh_print_help(void);
void tvh_finalize(config_t *cfg);

int tvh_id_parse(const char *s, unsigned *out);
int tvh_org_id_parse(const char *s, unsigned *out);

#endif
