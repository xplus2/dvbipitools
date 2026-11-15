/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>

#include "lib/config/yamlcfg.h"
#include "lib/mux/fec2022.h"
#include "lib/net/netconnect.h"
#include "../config.h"
#include "priv.h"

int tvh_apply_mcast(void *c, const char *v, char *e, size_t n) {
  if (tvh_cfg_mcast(c, v)) return yamlcfg_bad(e, n, v, "multicast addr:port");
  return 0;
}

int tvh_apply_out_iface(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->iface_out, v, e, n);
}

int tvh_apply_udp(void *c, const char *v, char *e, size_t n) {
  int udp;
  if (yamlcfg_set_bool(&udp, v, e, n)) return -1;
  ((config_t *)c)->rtp = !udp;
  return 0;
}

int tvh_apply_ttl(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->ttl, v, 1, 255, e, n);
}

int tvh_apply_dscp(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_int_by(&((config_t *)c)->dscp, v, net_dscp_parse, NET_DSCP_NAMES, e, n);
}

int tvh_apply_al_fec(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (fec2022_parse_ld(v, &cfg->al_fec_l, &cfg->al_fec_d)) return yamlcfg_bad(e, n, v, "want L:D, L*D<=400, L<=40");
  return 0;
}

int tvh_apply_al_fec_port(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->al_fec_port, v, 1, 65535, e, n);
}

int tvh_apply_rist(void *c, const char *v, char *e, size_t n) {
  return tvh_cfg_add_peer(c, v, e, n);
}

int tvh_apply_profile(void *c, const char *v, char *e, size_t n) {
  return tvh_cfg_profile(c, v, e, n);
}

int tvh_apply_secret(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_buf(cfg->rist_secret, sizeof cfg->rist_secret, v, e, n);
}

int tvh_apply_encryption_type(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_int_by(&((config_t *)c)->rist_key_size, v, argutil_rist_key_size, ARGUTIL_RIST_KEY_SIZE_HINT, e, n);
}

int tvh_apply_cname(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_buf(cfg->rist_cname, sizeof cfg->rist_cname, v, e, n);
}

int tvh_apply_buffer(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->rist_buffer_ms, v, 1, 60000, e, n);
}

int tvh_apply_srt_group_mode(void *c, const char *v, char *e, size_t n) {
  return tvh_cfg_group_mode(c, v, e, n);
}

int tvh_apply_srt_passphrase(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_buf(cfg->srt_passphrase, sizeof cfg->srt_passphrase, v, e, n);
}

int tvh_apply_srt_pbkeylen(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_int_by(&((config_t *)c)->srt_pbkeylen, v, argutil_srt_pbkeylen, ARGUTIL_SRT_PBKEYLEN_HINT, e, n);
}

int tvh_apply_srt_streamid(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_buf(cfg->srt_streamid, sizeof cfg->srt_streamid, v, e, n);
}

int tvh_apply_srt_packetfilter(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_buf(cfg->srt_packetfilter, sizeof cfg->srt_packetfilter, v, e, n);
}

int tvh_apply_srt_latency(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->srt_latency_ms, v, 1, 60000, e, n);
}
