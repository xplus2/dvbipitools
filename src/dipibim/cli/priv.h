/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBIM_CLI_PRIV_H
#define DIPIBIM_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"

#include "../version.h"

#define OPT_COLOR 1000
#define OPT_CONFIGTEST 1001
#define OPT_CONFIG_STRICT 1002
#define OPT_PROFILE 1003
#define OPT_LEN_MODE 1004
#define OPT_DEFERRED 1005
#define OPT_CAST 1006
#define OPT_DVB_LOCATOR 1007
#define OPT_DVB_DURATION 1008
#define OPT_DVB_PUBTIME 1009
#define OPT_ZBUF_SIZE 1010
#define OPT_ZGATHER 1011
#define OPT_SYNTAX 1012
#define OPT_COMPAT 1013

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

void bim_print_help(void);

#endif
