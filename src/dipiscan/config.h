/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISCAN_CONFIG_H
#define DIPISCAN_CONFIG_H

#include "args.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipiscan.yaml"

int scan_cfg_default_exists(void);

void scan_cfg_defaults(config_t *cfg);

int scan_cfg_load(config_t *cfg, const char *path);

int scan_cfg_test(const char *path);

#endif
