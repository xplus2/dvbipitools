/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/config/yamlcfg.h"
#include "../config.h"
#include "priv.h"

int rdh_apply_input(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  const char *uri;
  if (yamlcfg_set_str(&cfg->str_pool, &uri, v, e, n)) return -1;
  if (rdh_add_input(cfg, uri, e, n)) return -1;
  if (cfg->parse_item.input) cfg->parse_item.have_input = 1;
  return 0;
}

int rdh_apply_input_sid(void *c, const char *v, char *e, size_t n) {
  radio_input_t *in = rdh_cur_input(c, e, n);
  return in ? yamlcfg_set_uint(&in->sid, v, 1, 0xFFFF, e, n) : -1;
}

int rdh_apply_input_sdt(void *c, const char *v, char *e, size_t n) {
  radio_input_t *in = rdh_cur_input(c, e, n);
  return in ? rdh_set_buf(in->sdt_text, sizeof in->sdt_text, v, e, n) : -1;
}

int rdh_apply_input_provider(void *c, const char *v, char *e, size_t n) {
  radio_input_t *in = rdh_cur_input(c, e, n);
  return in ? rdh_set_buf(in->provider_text, sizeof in->provider_text, v, e, n) : -1;
}
