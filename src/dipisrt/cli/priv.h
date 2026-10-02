/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISRT_CLI_PRIV_H
#define DIPISRT_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_GROUP_MODE 1000
#define OPT_RENDEZVOUS 1001
#define OPT_LOCAL 1002
#define OPT_PASSPHRASE 1003
#define OPT_PBKEYLEN 1004
#define OPT_STREAMID 1005
#define OPT_PACKETFILTER 1006
#define OPT_LATENCY 1007
#define OPT_SEND_BUFFER_MULT 1012
#define OPT_AL_FEC 1013
#define OPT_AL_FEC_PORT 1014
#define OPT_COLOR 1008
#define OPT_METRICS 1009
#define OPT_METRICS_ID 1010
#define OPT_METRICS_INTERVAL 1011
#define OPT_METRICS_INSPECT_TS 1017
#define OPT_CONFIG_STRICT 1016
#define OPT_CONFIGTEST 1015

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

#define OPT_UNHANDLED 4

typedef struct {
  config_t *cfg;
  int cli_in;
  int cli_out;
} srt_opt_t;

/* ARGS_OK: handled. OPT_UNHANDLED: not this group */
args_status_t srt_opt_general(srt_opt_t *p, int c);
args_status_t srt_opt_srt(srt_opt_t *p, int c);

void srt_print_help(void);
args_status_t srt_cli_check(config_t *cfg);
int srt_parse_endpoint_uri(const char *uri, endpoint_t *e, int is_sink, int *count);

#endif
