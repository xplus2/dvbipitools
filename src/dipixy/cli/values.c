/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"

#include "priv.h"

/* "all:<port>" (wildcard, both families) or "<addr>:<port>" / "[<addr6>]:<port>" */
int dixy_cfg_listen(listen_spec_t *out, const char *s) {
  if (strncmp(s, "all:", 4) == 0) {
    unsigned port;
    if (argutil_port_parse(s + 4, &port)) return -1;
    out->scope = LISTEN_ANY;
    out->addr[0] = '\0';
    out->port = port;
    return 0;
  }
  {
    int family;
    unsigned port;
    if (argutil_addrport_parse(s, &family, out->addr, sizeof out->addr, &port)) return -1;
    out->scope = family == AF_INET6 ? LISTEN_V6 : LISTEN_V4;
    out->port = port;
    return 0;
  }
}

/* -1/-2/-3 (that many x core count) or a positive absolute thread count */
int dixy_cfg_workers(int *out, const char *s) {
  char *end;
  long v = strtol(s, &end, 10);
  if (*end != '\0') return -1;
  if (v == -1 || v == -2 || v == -3) {
    *out = (int)v;
    return 0;
  }
  if (v >= 1 && v <= 1024) {
    *out = (int)v;
    return 0;
  }
  return -1;
}

/* -f/--format <list>: comma-separated list.
   listed formats 0, everything else 1. 0 ok, -1 empty or unknown token */
int dixy_cfg_format(config_t *cfg, const char *s) {
  struct {
    const char *name;
    int *flag;
  } items[] = {
      {"ts", &cfg->no_ts},   {"spts", &cfg->no_spts}, {"rawaudio", &cfg->no_rawaudio}, {"mp4", &cfg->no_mp4},
      {"hls", &cfg->no_hls}, {"llhls", &cfg->no_llhls}, {"dash", &cfg->no_dash}, {"lldash", &cfg->no_lldash},
  };
  size_t n_items = sizeof items / sizeof items[0];
  size_t i;
  const char *p = s;
  if (!*s) return -1;
  for (i = 0; i < n_items; i++) *items[i].flag = 1;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    int matched = 0;
    if (!len) return -1;
    for (i = 0; i < n_items; i++) {
      if (strlen(items[i].name) == len && !strncmp(items[i].name, p, len)) {
        *items[i].flag = 0;
        matched = 1;
        break;
      }
    }
    if (!matched) return -1;
    p = comma ? comma + 1 : p + len;
  }
  return 0;
}



int dixy_cfg_set_h3_retry(config_t *cfg, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"auto", H3_RETRY_CFG_AUTO}, {"off", H3_RETRY_CFG_OFF}, {"always", H3_RETRY_CFG_ALWAYS}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &v)) {
    snprintf(err, errsz, "invalid '%s' (off, auto or always)", val);
    return -1;
  }
  cfg->h3_retry = v;
  return 0;
}

int dixy_cfg_set_h3_cc(config_t *cfg, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"cubic", H3_CC_CFG_CUBIC}, {"bbr", H3_CC_CFG_BBR}, {"reno", H3_CC_CFG_RENO}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &v)) {
    snprintf(err, errsz, "invalid '%s' (cubic, bbr or reno)", val);
    return -1;
  }
  cfg->h3_cc = v;
  return 0;
}

int dixy_cfg_set_media_type(config_t *cfg, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"tv", MEDIA_TV}, {"radio", MEDIA_RADIO}};
  media_type_t mt;
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &v)) {
    snprintf(err, errsz, "invalid '%s' (radio or tv)", val);
    return -1;
  }
  mt = (media_type_t)v;
  if (cfg->media_type_seen) {
    bufcpy(err, errsz, "media type given twice for the same input");
    return -1;
  }
  switch (cfg->last_input) {
    case LAST_STDIN:
      cfg->stdin_media_type = mt;
      break;
    case LAST_RIST:
      cfg->rist_media_type = mt;
      break;
    case LAST_SOURCE:
      cfg->sources[cfg->n_sources - 1].media_type = mt;
      break;
    default:
      bufcpy(err, errsz, "media type must directly follow the input it applies to");
      return -1;
  }
  cfg->media_type_seen = 1;
  return 0;
}

