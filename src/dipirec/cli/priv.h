/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_CLI_PRIV_H
#define DIPIREC_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_SUB_LEAD 1000
#define OPT_COLOR 1001
#define OPT_RET 1002
#define OPT_NO_RET_MC 1003
#define OPT_RET_MC_PORT 1004
#define OPT_RET_PT 1005
#define OPT_RET_WAIT 1006
#define OPT_STRIP 1007
#define OPT_PACE 1008
#define OPT_TTL 1010
#define OPT_AL_FEC 1030
#define OPT_AL_FEC_PORT 1031
#define OPT_PROFILE 1011
#define OPT_SECRET 1012
#define OPT_CNAME 1013
#define OPT_BUFFER 1014
#define OPT_INSECURE 1015
#define OPT_METRICS 1016
#define OPT_METRICS_ID 1017
#define OPT_METRICS_INTERVAL 1018
#define OPT_METRICS_INSPECT_TS 1034
#define OPT_METRICS_INSPECT_TS_PIDS 1035
#define OPT_PROFILE_IN 1019
#define OPT_SRT_PASSPHRASE_IN 1020
#define OPT_SRT_PBKEYLEN_IN 1021
#define OPT_SRT_STREAMID_IN 1022
#define OPT_SRT_PACKETFILTER_IN 1023
#define OPT_SRT_LATENCY_IN 1024
#define OPT_SRT_PASSPHRASE 1025
#define OPT_SRT_PBKEYLEN 1026
#define OPT_SRT_STREAMID 1027
#define OPT_SRT_PACKETFILTER 1028
#define OPT_SRT_LATENCY 1029
#define OPT_CONFIG_STRICT 1033
#define OPT_CONFIGTEST 1032
#define OPT_ENCRYPTION_TYPE 1036
#define OPT_ENCRYPTION_TYPE_IN 1037

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

#define OPT_UNHANDLED 3

typedef struct {
  config_t *cfg;
  int cli_out;
} rec_opt_t;

/* ARGS_OK: handled. OPT_UNHANDLED: not this group */
args_status_t rec_opt_general(rec_opt_t *p, int c);
args_status_t rec_opt_stream(rec_opt_t *p, int c);
args_status_t rec_opt_rist(rec_opt_t *p, int c);
args_status_t rec_opt_srt(rec_opt_t *p, int c);

void rec_print_help(void);
args_status_t rec_cli_check(config_t *cfg);

int rec_fmt_from_suffix(const char *path, out_fmt_t *f);
int rec_parse_pbkeylen_opt(const char *val, int *out, const char *optname);
int rec_validate_srt_passphrase(const char *passphrase, int pbkeylen, const char *suffix);

#endif
