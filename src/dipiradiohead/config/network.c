/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/uriparse.h"
#include "lib/mux/fec2022.h"
#include "lib/net/netconnect.h"
#include "../config.h"
#include "priv.h"

int rdh_apply_mcast(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (uriparse_mcast_addrport(v, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port)) {
    snprintf(e, n, "invalid '%s' (multicast addr:port)", v);
    return -1;
  }
  return 0;
}

int rdh_apply_out_iface(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->iface, v, e, n);
}

int rdh_apply_rtp(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->rtp, v, e, n);
}

int rdh_apply_ttl(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->ttl, v, 1, 255, e, n);
}

int rdh_apply_dscp(void *c, const char *v, char *e, size_t n) {
  if (net_dscp_parse(v, &((config_t *)c)->dscp)) {
    snprintf(e, n, "invalid '%s' (video-high|video-low|voice|signalling|best-effort|0..63)", v);
    return -1;
  }
  return 0;
}

int rdh_apply_al_fec(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (fec2022_parse_ld(v, &cfg->al_fec_l, &cfg->al_fec_d)) {
    snprintf(e, n, "invalid '%s' (want L:D, L*D<=400, L<=40)", v);
    return -1;
  }
  return 0;
}

int rdh_apply_al_fec_port(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->al_fec_port, v, 1, 65535, e, n);
}

int rdh_apply_nit(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return rdh_set_buf(cfg->nit_text, sizeof cfg->nit_text, v, e, n);
}

int rdh_apply_default_provider(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return rdh_set_buf(cfg->default_provider_text, sizeof cfg->default_provider_text, v, e, n);
}

int rdh_apply_rist(void *c, const char *v, char *e, size_t n) {
  return rdh_add_peer(c, v, e, n);
}

int rdh_apply_profile(void *c, const char *v, char *e, size_t n) {
  static const enum_map_t map[] = {{"simple", RIST_PROF_SIMPLE}, {"main", RIST_PROF_MAIN}};
  config_t *cfg = c;
  int m;
  if (map_lookup(map, sizeof map / sizeof map[0], v, &m)) {
    snprintf(e, n, "invalid '%s' (simple|main)", v);
    return -1;
  }
  cfg->rist_profile = (rist_profile_sel_t)m;
  cfg->rist_profile_given = 1;
  return 0;
}

int rdh_apply_secret(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return rdh_set_buf(cfg->rist_secret, sizeof cfg->rist_secret, v, e, n);
}

int rdh_apply_cname(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return rdh_set_buf(cfg->rist_cname, sizeof cfg->rist_cname, v, e, n);
}

int rdh_apply_buffer(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->rist_buffer_ms, v, 1, UINT_MAX, e, n);
}

int rdh_apply_srt_group_mode(void *c, const char *v, char *e, size_t n) {
  static const enum_map_t map[] = {{"broadcast", SRT_BOND_BROADCAST}, {"backup", SRT_BOND_BACKUP}};
  int m;
  if (map_lookup(map, sizeof map / sizeof map[0], v, &m)) {
    snprintf(e, n, "invalid '%s' (broadcast|backup)", v);
    return -1;
  }
  ((config_t *)c)->srt_group_mode = (srt_bond_mode_t)m;
  return 0;
}

int rdh_apply_srt_passphrase(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return rdh_set_buf(cfg->srt_passphrase, sizeof cfg->srt_passphrase, v, e, n);
}

int rdh_apply_srt_pbkeylen(void *c, const char *v, char *e, size_t n) {
  return rdh_set_pbkeylen(&((config_t *)c)->srt_pbkeylen, v, e, n);
}

int rdh_apply_srt_streamid(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return rdh_set_buf(cfg->srt_streamid, sizeof cfg->srt_streamid, v, e, n);
}

int rdh_apply_srt_packetfilter(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return rdh_set_buf(cfg->srt_packetfilter, sizeof cfg->srt_packetfilter, v, e, n);
}

int rdh_apply_srt_latency(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->srt_latency_ms, v, 1, 60000, e, n);
}
