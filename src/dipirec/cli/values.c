/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"

#include "priv.h"
#include "../filter/ts.h"

long duration_parse(const char *s) {
  if (!s || !*s) return -1;
  if (strchr(s, ':')) {
    long parts[3];
    int n = 0;
    const char *p = s;
    long h = 0, m = 0, sec;
    for (;;) {
      char *end;
      long v;
      if (!isdigit((unsigned char)*p) || n >= 3) return -1;
      v = strtol(p, &end, 10);
      if (v < 0) return -1;
      parts[n++] = v;
      if (*end == '\0') break;
      if (*end != ':') return -1;
      p = end + 1;
    }
    if (n == 2) {
      m = parts[0];
      sec = parts[1];
    } else if (n == 3) {
      h = parts[0];
      m = parts[1];
      sec = parts[2];
    } else return -1;
    if (sec > 59 || (n == 3 && m > 59)) return -1;
    h = h * 3600 + m * 60 + sec;
    return h > 0 ? h : -1;
  }

  if (strpbrk(s, "hms")) {
    const char *p = s;
    long total = 0;
    int last = 0; /* unit rank: h=1 m=2 s=3 */
    while (*p) {
      char *end;
      long v;
      int rank;
      if (!isdigit((unsigned char)*p)) return -1;
      v = strtol(p, &end, 10);
      if (v < 0) return -1;
      switch (*end) {
        case 'h':        rank = 1;        total += v * 3600;   break;
        case 'm':        rank = 2;        total += v * 60;     break;
        case 's':        rank = 3;        total += v;          break;
        default:         return -1;
      }
      if (rank <= last) return -1; /* bad order or duplicate */
      last = rank;
      p = end + 1;
    }
    return total > 0 ? total : -1;
  }

  {
    unsigned v;
    if (argutil_uint_range(s, 1, UINT_MAX, &v)) return -1;
    return (long)v;
  }
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

/* comma-separated STRIP_* tokens, or "none".
   SDT/BAT: keep. hw receivers likely need SDT as much as PAT/PMT. TDT/TOT: both mean "drop pid 0x14" */
static int parse_strip(const char *s, config_t *cfg) {
  static const enum_map_t map[] = {
      {"NUL", STRIP_NUL}, {"NIT", STRIP_NIT}, {"AIT", STRIP_AIT}, {"EIT", STRIP_EIT},
      {"CAT", STRIP_CAT}, {"ECM", STRIP_ECM}, {"EMM", STRIP_EMM}, {"RST", STRIP_RST},
      {"TDT", STRIP_TDT}, {"TOT", STRIP_TOT}, {"INT", STRIP_INT}, {"LCEVC", STRIP_LCEVC}};
  unsigned mask = 0;
  const char *p = s;

  if (strcmp(s, "none") == 0) {
    cfg->strip_mask = 0;
    return 0;
  }
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    char tok[8];
    int v;
    if (len == 0 || len >= sizeof tok) return -1;
    memcpy(tok, p, len);
    tok[len] = '\0';
    if (map_lookup(map, sizeof map / sizeof map[0], tok, &v)) return -1;
    mask |= (unsigned)v;
    p += len;
    if (*p == ',') p++;
  }
  cfg->strip_mask = mask;
  return 0;
}

static int parse_audio(const char *s, config_t *cfg) {
  unsigned v;
  if (strcmp(s, "all") == 0) {
    cfg->audio_all = 1;
    return 0;
  }
  if (argutil_uint_range(s, 1, 65535, &v)) return -1;
  cfg->audio_all = 0;
  cfg->audio_track = v;
  return 0;
}

static int fmt_from_name(const char *s, out_fmt_t *f) {
  static const enum_map_t map[] = {{"raw", FMT_RAW}, {"ts", FMT_TS}, {"mkv", FMT_MKV}, {"mka", FMT_MKA}, {"mp4", FMT_MP4}, {"m4a", FMT_M4A}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v)) return -1;
  *f = (out_fmt_t)v;
  return 0;
}

/* 1 if suffix gave format. no "raw": not a meaningful file extension, name-only via -f */
int rec_fmt_from_suffix(const char *path, out_fmt_t *f) {
  static const enum_map_t map[] = {{"ts", FMT_TS}, {"mkv", FMT_MKV}, {"mka", FMT_MKA}, {"mp4", FMT_MP4}, {"m4a", FMT_M4A}};
  const char *dot = strrchr(path, '.');
  char lower[8];
  size_t i;
  int v;
  if (!dot) return 0;
  dot++;
  for (i = 0; i < sizeof lower - 1 && dot[i]; i++) lower[i] = (char)tolower((unsigned char)dot[i]);
  if (dot[i]) return 0;
  lower[i] = '\0';
  if (map_lookup(map, sizeof map / sizeof map[0], lower, &v)) return 0;
  *f = (out_fmt_t)v;
  return 1;
}


int rec_cfg_audio(config_t *cfg, const char *s) {
  return parse_audio(s, cfg);
}

int rec_cfg_pmt(config_t *cfg, const char *s) {
  return parse_pmt_sel(s, cfg);
}

int rec_cfg_format(config_t *cfg, const char *s) {
  if (fmt_from_name(s, &cfg->format)) return -1;
  cfg->fl.have_format = 1;
  return 0;
}

int rec_cfg_subs(config_t *cfg, const char *s) {
  static const enum_map_t map[] = {{"strip", SUB_STRIP}, {"keep", SUB_KEEP}, {"srt", SUB_SRT}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v)) return -1;
  cfg->subs = (sub_mode_t)v;
  return 0;
}

int rec_cfg_time(config_t *cfg, const char *s) {
  long d = duration_parse(s);
  if (d < 0) return -1;
  cfg->duration_s = d;
  return 0;
}

int rec_cfg_strip(config_t *cfg, const char *s) {
  if (parse_strip(s, cfg)) return -1;
  cfg->fl.have_strip = 1;
  return 0;
}

int rec_cfg_profile(config_t *cfg, const char *s, int is_in) {
  static const enum_map_t map[] = {{"simple", RIST_PROF_SIMPLE}, {"main", RIST_PROF_MAIN}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v)) return -1;
  if (is_in) {
    cfg->rist_profile_in = (rist_profile_sel_t)v;
    cfg->fl.have_profile_in = 1;
  } else {
    cfg->rist_profile = (rist_profile_sel_t)v;
    cfg->fl.have_profile = 1;
  }
  return 0;
}

