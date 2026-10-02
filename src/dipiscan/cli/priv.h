/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISCAN_CLI_PRIV_H
#define DIPISCAN_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_COLOR 1001
#define OPT_CONFIG_STRICT 1003
#define OPT_CONFIGTEST 1002

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

void scan_print_help(void);

int scan_mcast_range_parse(const char *s, int *family, unsigned char *start, unsigned char *end, unsigned *total);
int scan_port_range_parse(const char *s, unsigned *lo, unsigned *hi);
int scan_http_proxy_parse(const char *s, config_t *cfg);
int scan_http_path_tmpl_valid(const char *t);
int scan_fmt_from_name(const char *s, out_fmt_t *f);

#endif
