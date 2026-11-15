/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_CONFIG_H
#define DIPIFCCRET_CONFIG_H

#include "cli/args.h"
#include "lib/config/yamlcfg.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipifccret.yaml"

#define RATE_MAX 1000000 /* per-source rate cap, keeps burst depth math in range */

extern const yamlcfg_key_t fccret_cfg_keys[];
extern const size_t fccret_cfg_nkeys;

void fccret_cfg_defaults(config_t *cfg);

int fccret_cfg_load(config_t *cfg, const char *path, int strict);

int fccret_cfg_test(const char *path, int strict);

#endif
