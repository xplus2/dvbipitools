/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_CONFIG_H
#define DIPIFCCRET_CONFIG_H

#include "args.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipifccret.yaml"

void fccret_cfg_defaults(config_t *cfg);

int fccret_cfg_load(config_t *cfg, const char *path);

int fccret_cfg_test(const char *path);

#endif
