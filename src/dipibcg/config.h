/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_CONFIG_H
#define DIPIBCG_CONFIG_H

#include "args.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipibcg.yaml"

void bcg_cfg_defaults(config_t *cfg);

int bcg_cfg_load(config_t *cfg, const char *path);

int bcg_cfg_test(const char *path);

#endif
