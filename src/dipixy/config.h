/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_CONFIG_H
#define DIPIXY_CONFIG_H

#include "cli/args.h"
#include "lib/config/yamlcfg.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipixy.yaml"

extern const yamlcfg_key_t dixy_cfg_keys[];
extern const size_t dixy_cfg_nkeys;

void dixy_cfg_defaults(config_t *cfg);

int dixy_cfg_load(config_t *cfg, const char *path, int strict);

int dixy_cfg_test(const char *path, int strict);

#endif
