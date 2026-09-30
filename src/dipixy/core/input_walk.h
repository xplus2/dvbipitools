/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_CORE_INPUT_WALK_H
#define DIPIXY_CORE_INPUT_WALK_H

#include "../args.h"

typedef struct {
  unsigned ordinal;
  int is_stdin, is_rist;
  const char *name;
  media_type_t media_type;
  const source_def_t *src;
} config_input_t;

typedef void (*config_input_fn)(void *ctx, const config_input_t *in);

void config_inputs_walk(const config_t *cfg, config_input_fn fn, void *ctx);

#endif
