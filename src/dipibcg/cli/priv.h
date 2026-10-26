/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_CLI_PRIV_H
#define DIPIBCG_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_COLOR 1000
#define OPT_METRICS 1001
#define OPT_METRICS_ID 1002
#define OPT_METRICS_INTERVAL 1003
#define OPT_DSCP 1004
#define OPT_REFRESH 1009
#define OPT_INTERVAL 1007
#define OPT_TIMEOUT 1008
#define OPT_CONFIG_STRICT 1006
#define OPT_CONFIGTEST 1005
#define OPT_PUBLISHER 1010
#define OPT_LANG 1011
#define OPT_INDEX 1012
#define OPT_DVB_LOCATOR 1013
#define OPT_DVB_DURATION 1014
#define OPT_DVB_PUBTIME 1015
#define OPT_ENCODING 1016

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

void bcg_print_help(void);

#endif
