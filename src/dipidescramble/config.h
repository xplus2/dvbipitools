/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_CONFIG_H
#define DIPIDESCRAMBLE_CONFIG_H

#include "cli/args.h"
#include "lib/config/yamlcfg.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipidescramble.yaml"

extern const yamlcfg_key_t dscr_cfg_keys[];
extern const size_t dscr_cfg_nkeys;

void dscr_cfg_defaults(config_t *cfg);

int dscr_cfg_load(config_t *cfg, const char *path, int strict);

int dscr_cfg_test(const char *path, int strict);

#endif
