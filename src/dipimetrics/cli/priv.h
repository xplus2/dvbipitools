/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIMETRICS_CLI_PRIV_H
#define DIPIMETRICS_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_COLOR 1000
#define OPT_TLS_CERT 1001
#define OPT_TLS_KEY 1002
#define OPT_AUTH 1003
#define OPT_CONFIGTEST 1004
#define OPT_CONFIG_STRICT 1005

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

void metrics_print_help(void);

#endif
