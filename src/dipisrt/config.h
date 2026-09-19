/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISRT_CONFIG_H
#define DIPISRT_CONFIG_H

#include "args.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipisrt.yaml"

void srt_cfg_defaults(config_t *cfg);

int srt_cfg_load(config_t *cfg, const char *path);

int srt_cfg_test(const char *path);

#endif
