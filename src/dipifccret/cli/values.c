/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"

#include "priv.h"

/* comma-separated CIDR list (IPv4 or IPv6), light validation here, capture.c re-validates at BPF-build time */
static int ranges_parse(const char *s, config_t *cfg) {
  char buf[ARGS_MAX_RANGES * 64];
  char *save = NULL;
  size_t slen = strlen(s);
  if (slen >= sizeof buf) return -1;
  memcpy(buf, s, slen + 1);
  cfg->range_count = 0;
  for (const char *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
    char *slash = strchr(tok, '/');
    int is_v6 = strchr(tok, ':') != NULL;
    struct in_addr a4;
    struct in6_addr a6;
    long prefix;
    char *end;
    size_t tlen;
    if (cfg->range_count >= ARGS_MAX_RANGES) return -1;
    if (!slash) return -1;
    *slash = '\0';
    if (is_v6 ? inet_pton(AF_INET6, tok, &a6) != 1 : inet_pton(AF_INET, tok, &a4) != 1) return -1;
    prefix = strtol(slash + 1, &end, 10);
    if (*end != '\0' || prefix < 0 || prefix > (is_v6 ? 128 : 32)) return -1;
    *slash = '/';
    tlen = strlen(tok);
    if (tlen >= sizeof cfg->ranges[0]) return -1;
    memcpy(cfg->ranges[cfg->range_count], tok, tlen + 1);
    cfg->range_ptrs[cfg->range_count] = cfg->ranges[cfg->range_count];
    cfg->range_count++;
  }
  return cfg->range_count ? 0 : -1;
}

static int cidr_list_parse(const char *s, cidr_t *out, size_t *count, size_t max) {
  char buf[ARGS_MAX_RANGES * 64];
  char *save = NULL;
  size_t slen = strlen(s);
  if (slen >= sizeof buf) return -1;
  memcpy(buf, s, slen + 1);
  *count = 0;
  for (const char *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
    if (*count >= max || cidr_parse(tok, &out[*count]) != 0) return -1;
    (*count)++;
  }
  return *count ? 0 : -1;
}

int fccret_cfg_range(config_t *cfg, const char *s) {
  return ranges_parse(s, cfg);
}

int fccret_cfg_fcc_range(config_t *cfg, const char *s) {
  return cidr_list_parse(s, cfg->fcc_ranges, &cfg->fcc_range_count, ARGS_MAX_RANGES);
}

int fccret_cfg_fcc_client_range(config_t *cfg, const char *s) {
  return cidr_list_parse(s, cfg->fcc_client_ranges, &cfg->fcc_client_range_count, ARGS_MAX_RANGES);
}
