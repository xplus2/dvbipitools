/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>

#include "lib/config/yamlcfg.h"
#include "../config.h"
#include "priv.h"

int tvh_apply_input(void *c, const char *v, char *e, size_t n) {
  if (tvh_cfg_add_input(c, v, e, n)) return -1;
  if (tvh_item.input) tvh_item.have_input = 1;
  return 0;
}

int tvh_apply_input_pmt_pid(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  if (!in) return -1;
  if (tvh_cfg_pid(v, &in->pmt_pid) || in->pmt_pid == 0) {
    snprintf(e, n, "invalid '%s' (0x0010..0x1FFE)", v);
    return -1;
  }
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
  return in ? tvh_set_buf(in->provider_text, sizeof in->provider_text, v, e, n) : -1;
}

int tvh_apply_input_iface(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_str(&in->iface_in, v, e, n) : -1;
}

int tvh_apply_input_strip_eit(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_bool(&in->strip_eit, v, e, n) : -1;
}

int tvh_apply_input_strip(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  if (!in) return -1;
  if (tvh_cfg_strip(&in->strip_mask, v)) {
    snprintf(e, n, "invalid '%s' (comma list of DATA,ECM, or none)", v);
    return -1;
  }
  return 0;
}

int tvh_apply_input_hbbtv(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_str(&in->hbbtv_url, v, e, n) : -1;
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
  static const enum_map_t map[] = {{"simple", 0}, {"main", 1}};
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  if (!in) return -1;
  if (map_lookup(map, sizeof map / sizeof map[0], v, &in->rist_profile_main)) {
    snprintf(e, n, "invalid '%s' (simple|main)", v);
    return -1;
  }
  return 0;
}

int tvh_apply_input_srt_passphrase(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? tvh_set_buf(in->srt_passphrase_in, sizeof in->srt_passphrase_in, v, e, n) : -1;
}

int tvh_apply_input_srt_pbkeylen(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? tvh_set_pbkeylen(&in->srt_pbkeylen_in, v, e, n) : -1;
}

int tvh_apply_input_srt_streamid(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? tvh_set_buf(in->srt_streamid_in, sizeof in->srt_streamid_in, v, e, n) : -1;
}

int tvh_apply_input_srt_packetfilter(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? tvh_set_buf(in->srt_packetfilter_in, sizeof in->srt_packetfilter_in, v, e, n) : -1;
}

int tvh_apply_input_srt_latency(void *c, const char *v, char *e, size_t n) {
  dipitvhead_input_t *in = tvh_cur_input(c, e, n);
  return in ? yamlcfg_set_uint(&in->srt_latency_in_ms, v, 1, 60000, e, n) : -1;
}
