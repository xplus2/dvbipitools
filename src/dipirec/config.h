/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_CONFIG_H
#define DIPIREC_CONFIG_H

#include "cli/args.h"
#include "lib/config/yamlcfg.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipirec.yaml"

extern const yamlcfg_key_t rec_cfg_keys[];
extern const size_t rec_cfg_nkeys;

void rec_cfg_defaults(config_t *cfg);

int rec_cfg_load(config_t *cfg, const char *path, int strict);

int rec_cfg_test(const char *path, int strict);

#endif
