/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_CONFIG_H
#define DIPIBCG_CONFIG_H

#include "cli/args.h"
#include "lib/config/yamlcfg.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipibcg.yaml"

extern const yamlcfg_key_t bcg_cfg_keys[];
extern const size_t bcg_cfg_nkeys;

void bcg_cfg_defaults(config_t *cfg);

/* --index names dipibcg collects. 0 ok, -1 unknown or not collected */
#define BCG_INDEX_NAMES "pi-crid|pi-title|schedule-time|schedule-title"

int bcg_cfg_load(config_t *cfg, const char *path, int strict);

int bcg_cfg_test(const char *path, int strict);

#endif
