/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPICAM378_CONFIG_H
#define DIPICAM378_CONFIG_H

#include "args.h"

#define ARGS_DEFAULT_PORT 27500u
#define ARGS_DEFAULT_PASSWORD TOOL_NAME
#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipicam378.yaml"

void cam378_cfg_defaults(config_t *cfg);

int cam378_cfg_load(config_t *cfg, const char *path, int strict);

int cam378_cfg_test(const char *path, int strict);

int cam378_cfg_caid(const char *p, unsigned *out);

#endif
