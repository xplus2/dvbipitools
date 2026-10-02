/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRIST_CLI_PRIV_H
#define DIPIRIST_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_PROFILE 1000
#define OPT_SECRET 1001
#define OPT_CNAME 1002
#define OPT_BUFFER 1003
#define OPT_COLOR 1004
#define OPT_METRICS 1005
#define OPT_METRICS_ID 1006
#define OPT_METRICS_INTERVAL 1007
#define OPT_METRICS_INSPECT_TS 1012
#define OPT_AL_FEC 1008
#define OPT_AL_FEC_PORT 1009
#define OPT_CONFIG_STRICT 1011
#define OPT_CONFIGTEST 1010
#define OPT_ENCRYPTION_TYPE 1013

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

#define OPT_UNHANDLED 4

typedef struct {
  config_t *cfg;
  int cli_in;
  int cli_out;
} rist_opt_t;

/* ARGS_OK: handled. OPT_UNHANDLED: not this group */
args_status_t rist_opt_general(rist_opt_t *p, int c);
args_status_t rist_opt_rist(rist_opt_t *p, int c);

void rist_print_help(void);
args_status_t rist_cli_check(config_t *cfg);
int rist_parse_endpoint_uri(const char *uri, endpoint_t *e, int is_sink, int *count);

#endif
