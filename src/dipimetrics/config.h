/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIMETRICS_CONFIG_H
#define DIPIMETRICS_CONFIG_H

#include <stddef.h>

#include "args.h"

#define DEFAULT_LISTEN_ADDR "127.0.0.1"
#define DEFAULT_LISTEN_PORT 9109
#define DEFAULT_EXPIRY_S 30
#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipimetrics.yaml"

void metrics_cfg_defaults(config_t *cfg);

int metrics_cfg_load(config_t *cfg, const char *path, int strict);

int metrics_cfg_test(const char *path, int strict);

const char *metrics_cfg_conflict(const config_t *cfg);

int metrics_cfg_auth(const char *val, char *out, size_t outsz, char *err, size_t errsz);

#endif
