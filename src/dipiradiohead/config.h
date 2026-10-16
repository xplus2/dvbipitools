/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_CONFIG_H
#define DIPIRADIOHEAD_CONFIG_H

#include "cli/args.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipiradiohead.yaml"

void rdh_cfg_defaults(config_t *cfg);

int rdh_cfg_load(config_t *cfg, const char *path, int strict);

int rdh_cfg_test(const char *path, int strict);

/* ecm/emm pid vs muxer pids. 1: clash, pid_out + is_emm set */
int rdh_cas_pid_clash(const config_t *cfg, unsigned *pid_out, int *is_emm);

#endif
