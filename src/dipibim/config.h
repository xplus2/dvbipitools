/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBIM_CONFIG_H
#define DIPIBIM_CONFIG_H

#include "cli/args.h"
#include "lib/config/yamlcfg.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipibim.yaml"

extern const yamlcfg_key_t bim_cfg_keys[];
extern const size_t bim_cfg_nkeys;

void bim_cfg_defaults(config_t *cfg);

int bim_cfg_load(config_t *cfg, const char *path, int strict);

int bim_cfg_test(const char *path, int strict);

#endif
