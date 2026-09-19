/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_CONFIG_H
#define DIPIREC_CONFIG_H

#include "args.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipirec.yaml"

void rec_cfg_defaults(config_t *cfg);

int rec_cfg_load(config_t *cfg, const char *path);

int rec_cfg_test(const char *path);

#endif
