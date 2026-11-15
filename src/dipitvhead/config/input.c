/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>

#include "lib/config/yamlcfg.h"
#include "lib/mux/fec2022.h"
#include "lib/net/jitbuf.h"
#include "../config.h"
#include "priv.h"
#include "lib/net/rist/ristpeer.h"

int tvh_apply_input(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (tvh_cfg_add_input(cfg, v, e, n)) return -1;
  if (cfg->parse_item.input) cfg->parse_item.have_input = 1;
  return 0;
}

int tvh_apply_input_pmt_pid(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  if (!in) return -1;
  if (argutil_pid_parse(v, 0x10, &in->pmt_pid)) return yamlcfg_bad(e, n, v, "0x0010..0x1FFE");
  return 0;
}

int tvh_apply_input_sid(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_uint(&in->sid, v, 1, 0xFFFF, e, n) : -1;
}

int tvh_apply_input_sdt(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? tvh_set_table(&in->sdt_mode, in->sdt_text, sizeof in->sdt_text, v, e, n) : -1;
}

int tvh_apply_input_provider(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_buf(in->provider_text, sizeof in->provider_text, v, e, n) : -1;
}

int tvh_apply_input_iface(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  dipitvhead_input_t *in = tvh_cur_input(cfg, e, n);
  return in ? yamlcfg_set_str(&cfg->str_pool, &in->iface_in, v, e, n) : -1;
}

int tvh_apply_input_strip_eit(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_bool(&in->strip_eit, v, e, n) : -1;
}

int tvh_apply_input_strip(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  if (!in) return -1;
  if (tvh_cfg_strip(&in->strip_mask, v)) return yamlcfg_bad(e, n, v, "comma list of DATA,ECM, or none");
  return 0;
}

int tvh_apply_input_hbbtv(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  dipitvhead_input_t *in = tvh_cur_input(cfg, e, n);
  return in ? yamlcfg_set_str(&cfg->str_pool, &in->hbbtv_url, v, e, n) : -1;
}

int tvh_apply_input_hbbtv_name(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_buf(in->hbbtv_name, sizeof in->hbbtv_name, v, e, n) : -1;
}

int tvh_apply_input_hbbtv_org_id(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_uint(&in->hbbtv_org_id, v, 1, UINT_MAX, e, n) : -1;
}

int tvh_apply_input_hbbtv_app_id(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_uint(&in->hbbtv_app_id, v, 1, 0xFFFF, e, n) : -1;
}

int tvh_apply_input_rist_profile(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  if (!in) return -1;
  if (rist_prof_parse(v, &in->rist_profile_main)) return yamlcfg_bad(e, n, v, RIST_PROF_HINT);
  return 0;
}

int tvh_apply_input_rist_encryption_type(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_int_by(&in->rist_key_size_in, v, argutil_rist_key_size, ARGUTIL_RIST_KEY_SIZE_HINT, e, n) : -1;
}

int tvh_apply_input_rist_secret(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_buf(in->rist_secret_in, sizeof in->rist_secret_in, v, e, n) : -1;
}

int tvh_apply_input_rist_cname(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_buf(in->rist_cname_in, sizeof in->rist_cname_in, v, e, n) : -1;
}

int tvh_apply_input_rist_buffer(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_uint(&in->rist_buffer_ms_in, v, 1, 60000, e, n) : -1;
}

int tvh_apply_input_srt_passphrase(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_buf(in->srt_passphrase_in, sizeof in->srt_passphrase_in, v, e, n) : -1;
}

int tvh_apply_input_srt_pbkeylen(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_int_by(&in->srt_pbkeylen_in, v, argutil_srt_pbkeylen, ARGUTIL_SRT_PBKEYLEN_HINT, e, n) : -1;
}

int tvh_apply_input_srt_streamid(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_buf(in->srt_streamid_in, sizeof in->srt_streamid_in, v, e, n) : -1;
}

int tvh_apply_input_srt_packetfilter(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_buf(in->srt_packetfilter_in, sizeof in->srt_packetfilter_in, v, e, n) : -1;
}

int tvh_apply_input_al_fec(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  if (!in) return -1;
  if (fec2022_parse_ld(v, &in->al_fec_l_in, &in->al_fec_d_in)) return yamlcfg_bad(e, n, v, "want L:D, L*D<=400, L<=40");
  return 0;
}

int tvh_apply_input_al_fec_port(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_uint(&in->al_fec_port_in, v, 1, 65535, e, n) : -1;
}

int tvh_apply_input_jitter_ms(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_uint(&in->jitter_ms, v, 1, JITBUF_MAX_DELAY_MS, e, n) : -1;
}

int tvh_apply_input_srt_latency(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_uint(&in->srt_latency_in_ms, v, 1, 60000, e, n) : -1;
}
