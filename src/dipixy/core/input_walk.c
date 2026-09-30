/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "input_walk.h"

#include <string.h>

void config_inputs_walk(const config_t *cfg, config_input_fn fn, void *ctx) {
  int max_ord = cfg->stdin_ordinal;
  int si = 0;
  if (cfg->rist_ordinal > max_ord) max_ord = cfg->rist_ordinal;
  if (cfg->n_sources > 0 && cfg->sources[cfg->n_sources - 1].ordinal > max_ord) max_ord = cfg->sources[cfg->n_sources - 1].ordinal;
  for (int ord = 1; ord <= max_ord; ord++) {
    config_input_t in;
    memset(&in, 0, sizeof in);
    in.ordinal = (unsigned)ord;
    if (ord == cfg->stdin_ordinal) {
      in.is_stdin = 1;
      in.name = cfg->stdin_name;
      in.media_type = cfg->stdin_media_type;
      fn(ctx, &in);
    } else if (ord == cfg->rist_ordinal) {
      in.is_rist = 1;
      in.name = cfg->rist_name;
      in.media_type = cfg->rist_media_type;
      fn(ctx, &in);
    } else if (si < cfg->n_sources && cfg->sources[si].ordinal == ord) {
      in.src = &cfg->sources[si];
      in.name = cfg->sources[si].name;
      in.media_type = cfg->sources[si].media_type;
      fn(ctx, &in);
      si++;
    }
  }
}
