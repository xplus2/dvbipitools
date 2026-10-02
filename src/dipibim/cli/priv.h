/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBIM_CLI_PRIV_H
#define DIPIBIM_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"

#include "../version.h"

#define OPT_COLOR 1000

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

void bim_print_help(void);

#endif
