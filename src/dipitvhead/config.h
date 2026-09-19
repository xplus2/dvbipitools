/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_CONFIG_H
#define DIPITVHEAD_CONFIG_H

#include <stddef.h>

#include "args.h"

#define DEFAULT_CONFIG_PATH "/etc/dvbipitools/dipitvhead.yaml"

typedef void (*tvh_report_fn)(void *ud, int fatal, const char *msg);

void tvh_cfg_defaults(config_t *cfg);

int tvh_cfg_load(config_t *cfg, const char *path);

int tvh_cfg_test(const char *path);

int tvh_cfg_mcast(config_t *cfg, const char *s);
int tvh_cfg_pid(const char *s, unsigned *out);
int tvh_cfg_source(source_t *dst, const char *uri, char *err, size_t errsz);
int tvh_cfg_add_input(config_t *cfg, const char *uri, char *err, size_t errsz);
int tvh_cfg_cas_pids(config_t *cfg, const char *s);
int tvh_cfg_strip(unsigned *mask, const char *s);
int tvh_cfg_add_peer(config_t *cfg, const char *uri, char *err, size_t errsz);
int tvh_cfg_profile(config_t *cfg, const char *val, char *err, size_t errsz);
int tvh_cfg_group_mode(config_t *cfg, const char *val, char *err, size_t errsz);
int tvh_cfg_check(config_t *cfg, int partial, tvh_report_fn rep, void *ud);

#endif
