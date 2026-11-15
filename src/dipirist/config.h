/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRIST_CONFIG_H
#define DIPIRIST_CONFIG_H

#include "cli/args.h"
#include "lib/config/yamlcfg.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipirist.yaml"

extern const yamlcfg_key_t dipirist_cfg_keys[];
extern const size_t dipirist_cfg_nkeys;

void dipirist_cfg_defaults(config_t *cfg);

int dipirist_cfg_load(config_t *cfg, const char *path, int strict);

int dipirist_cfg_test(const char *path, int strict);

#endif
