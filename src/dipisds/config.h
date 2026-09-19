/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISDS_CONFIG_H
#define DIPISDS_CONFIG_H

#include "args.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipisds.yaml"

void sds_cfg_defaults(config_t *cfg);

int sds_cfg_load(config_t *cfg, const char *path, int strict);

int sds_cfg_test(const char *path, int strict);

#endif
