/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"

#include "priv.h"

static int fmt_from_name(const char *s, out_fmt_t *f) {
  static const enum_map_t map[] = {{"ts", FMT_TS}, {"mkv", FMT_MKV}, {"mka", FMT_MKA}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v)) return -1;
  *f = (out_fmt_t)v;
  return 0;
}

/* decimal or 0x-hex, PMT pid range 0x0010..0x1FFE */
static int pid_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v < 0x0010 || v > 0x1FFE) return -1;
  *out = (unsigned)v;
  return 0;
}

static int parse_pmt_sel(const char *s, config_t *cfg) {
  if (strcmp(s, "all") == 0) {
    cfg->pmt_sel = PMT_SEL_ALL;
    return 0;
  }
  if (pid_parse(s, &cfg->pmt_pid)) return -1;
  cfg->pmt_sel = PMT_SEL_PID;
  return 0;
}

int dscr_cfg_format(config_t *cfg, const char *s) {
  return fmt_from_name(s, &cfg->format);
}

int dscr_cfg_pmt(config_t *cfg, const char *s) {
  return parse_pmt_sel(s, cfg);
}

int dscr_cfg_token_header(config_t *cfg, const char *s) {
  if (!s[0] || strpbrk(s, ":\r\n ")) return -1;
  cfg->unicast_emm_token_header = s;
  return 0;
}

int dscr_cfg_profile(config_t *cfg, const char *s) {
  static const enum_map_t map[] = {{"simple", 0}, {"main", 1}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v)) return -1;
  cfg->rist_profile_main = v;
  cfg->profile_given = 1;
  return 0;
}
