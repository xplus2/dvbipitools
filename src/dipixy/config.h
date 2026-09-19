/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_CONFIG_H
#define DIPIXY_CONFIG_H

#include "args.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipixy.yaml"

void dixy_cfg_defaults(config_t *cfg);

int dixy_cfg_load(config_t *cfg, const char *path);

int dixy_cfg_test(const char *path);

#endif
