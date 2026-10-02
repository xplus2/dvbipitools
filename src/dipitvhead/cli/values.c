/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"

#include "priv.h"
#include "../mux/pmtbuild.h"

int tvh_cfg_profile(config_t *cfg, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"simple", RIST_PROF_SIMPLE}, {"main", RIST_PROF_MAIN}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &v)) {
    snprintf(err, errsz, "invalid '%s' (simple|main)", val);
    return -1;
  }
  cfg->rist_profile = (rist_profile_sel_t)v;
  cfg->rist_profile_given = 1;
  return 0;
}

int tvh_cfg_pcr_mode(config_t *cfg, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"preserve", PCR_MODE_PRESERVE}, {"rebase", PCR_MODE_REBASE}, {"regenerate", PCR_MODE_REGENERATE}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &v)) {
    snprintf(err, errsz, "invalid '%s' (preserve|rebase|regenerate)", val);
    return -1;
  }
  cfg->pcr_mode = (pcr_mode_t)v;
  return 0;
}

int tvh_cfg_pcr_lead_ms(config_t *cfg, const char *val, char *err, size_t errsz) {
  if (argutil_uint_range(val, 1, PCR_LEAD_MS_MAX, &cfg->pcr_lead_ms)) {
    snprintf(err, errsz, "invalid '%s' (1..%d ms)", val, PCR_LEAD_MS_MAX);
    return -1;
  }
  cfg->pcr_lead_ms_given = 1;
  return 0;
}

int tvh_cfg_group_mode(config_t *cfg, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"broadcast", SRT_BOND_BROADCAST}, {"backup", SRT_BOND_BACKUP}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &v)) {
    snprintf(err, errsz, "invalid '%s' (broadcast|backup)", val);
    return -1;
  }
  cfg->srt_group_mode = (srt_bond_mode_t)v;
  return 0;
}

int tvh_id_parse(const char *s, unsigned *out) {
  return argutil_uint_range(s, 1, 0xFFFF, out);
}

/* organisation_id is 32 bits per TS 102 809, unlike application_id's 16 */
int tvh_org_id_parse(const char *s, unsigned *out) {
  return argutil_uint_range(s, 1, 0xFFFFFFFFUL, out);
}

/* decimal or 0x-hex, PMT pid range: 0x0010..0x1FFE (0 = auto, handled by caller) */
int tvh_cfg_pid(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v > 0x1FFE) return -1;
  *out = (unsigned)v;
  return 0;
}

/* pids and/or video/audio/lcevc keywords, e.g. 0x0103,video */
int tvh_cfg_cas_pids(config_t *cfg, const char *s) {
  char buf[512];
  char *save = NULL;
  if (strlen(s) >= sizeof buf) return -1;
  bufcpy(buf, sizeof buf, s);
  cfg->cas_pid_count = 0;
  cfg->cas_pids_video = 0;
  cfg->cas_pids_audio = 0;
  cfg->cas_pids_lcevc = 0;
  for (const char *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
    unsigned pid;
    if (strcmp(tok, "video") == 0) {
      cfg->cas_pids_video = 1;
      continue;
    }
    if (strcmp(tok, "audio") == 0) {
      cfg->cas_pids_audio = 1;
      continue;
    }
    if (strcmp(tok, "lcevc") == 0) {
      cfg->cas_pids_lcevc = 1;
      continue;
    }
    if (cfg->cas_pid_count >= ARGS_MAX_CAS_PIDS) return -1;
    if (tvh_cfg_pid(tok, &pid) || pid == 0) return -1;
    cfg->cas_pids[cfg->cas_pid_count++] = pid;
  }
  return (cfg->cas_pid_count || cfg->cas_pids_video || cfg->cas_pids_audio || cfg->cas_pids_lcevc) ? 0 : -1;
}

/* comma-separated TVSTRIP_* tokens, or "none" (default) */
int tvh_cfg_strip(unsigned *mask, const char *s) {
  static const enum_map_t map[] = {{"DATA", TVSTRIP_DATA}, {"ECM", TVSTRIP_ECM}};
  const char *p = s;

  if (strcmp(s, "none") == 0) {
    *mask = 0;
    return 0;
  }
  *mask = 0;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    char tok[8];
    int v;
    if (len == 0 || len >= sizeof tok) return -1;
    memcpy(tok, p, len);
    tok[len] = '\0';
    if (map_lookup(map, sizeof map / sizeof map[0], tok, &v)) return -1;
    *mask |= (unsigned)v;
    p += len;
    if (*p == ',') p++;
  }
  return 0;
}
