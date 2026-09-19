/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRIST_CONFIG_H
#define DIPIRIST_CONFIG_H

#include "args.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipirist.yaml"

void rist_cfg_defaults(config_t *cfg);

int rist_cfg_load(config_t *cfg, const char *path, int strict);

int rist_cfg_test(const char *path, int strict);

#endif
