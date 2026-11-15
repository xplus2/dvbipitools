/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"

#include "priv.h"
#include "lib/net/rist/ristpeer.h"

static int fmt_from_name(const char *s, out_fmt_t *f) {
  static const enum_map_t map[] = {{"ts", FMT_TS}, {"mkv", FMT_MKV}, {"mka", FMT_MKA}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v)) return -1;
  *f = (out_fmt_t)v;
  return 0;
}

int dscr_cfg_format(config_t *cfg, const char *s) {
  return fmt_from_name(s, &cfg->format);
}

int dscr_cfg_pmt(config_t *cfg, const char *s) {
  return argutil_pmt_sel_parse(s, &cfg->pmt_sel, &cfg->pmt_pid);
}

int dscr_cfg_token_header(config_t *cfg, const char *s) {
  if (!s[0] || strpbrk(s, ":\r\n ")) return -1;
  cfg->unicast_emm_token_header = s;
  return 0;
}

int dscr_cfg_profile(config_t *cfg, const char *s) {
  int v;
  if (rist_prof_parse(s, &v)) return -1;
  cfg->rist_profile_main = v;
  cfg->profile_given = 1;
  return 0;
}
