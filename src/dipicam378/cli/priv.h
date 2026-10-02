/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPICAM378_CLI_PRIV_H
#define DIPICAM378_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_CAID 1002
#define OPT_ALGO 1001
#define OPT_COLOR 1000
#define OPT_METRICS 1003
#define OPT_METRICS_ID 1004
#define OPT_METRICS_INTERVAL 1005
#define OPT_CONFIG_STRICT 1007
#define OPT_CONFIGTEST 1006

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

void cam378_print_help(void);

#endif
