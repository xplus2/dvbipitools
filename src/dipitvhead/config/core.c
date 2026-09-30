/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/ioutil.h"
#include "lib/net/netconnect.h"
#include "../config.h"
#include "priv.h"

void tvh_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->rtp = 1;
  cfg->dscp = NET_DSCP_VIDEO_HIGH;
  cfg->tsid = 1;
  cfg->onid = 1;
  cfg->cas_cp_duration_ms = 10000;
}

int tvh_set_buf(char *dst, size_t sz, const char *v, char *e, size_t n) {
  if (strlen(v) >= sz) {
    snprintf(e, n, "too long (max %zu)", sz - 1);
    return -1;
  }
  bufcpy(dst, sz, v);
  return 0;
}

int tvh_set_pbkeylen(int *dst, const char *v, char *e, size_t n) {
  unsigned u;
  if (argutil_uint_range(v, 16, 32, &u) || (u != 16 && u != 24 && u != 32)) {
    snprintf(e, n, "invalid '%s' (16|24|32)", v);
    return -1;
  }
  *dst = (int)u;
  return 0;
}

int tvh_set_table(table_mode_t *mode, char *text, size_t sz, const char *v, char *e, size_t n) {
  if (!strcmp(v, "-")) {
    *mode = TABLE_DROP;
    return 0;
  }
  if (tvh_set_buf(text, sz, v, e, n)) return -1;
  *mode = TABLE_OVERRIDE;
  return 0;
}

int tvh_item_hook(void *c, const char *list, int begin, char *e, size_t n) {
  config_t *cfg = c;
  if (!strcmp(list, "input")) {
    cfg->parse_item.input = begin;
    if (begin) {
      cfg->parse_item.have_input = 0;
    } else if (!cfg->parse_item.have_input) {
      bufcpy(e, n, "missing input");
      return -1;
    }
  } else if (!strcmp(list, "cas.ecmg")) {
    cfg->parse_item.vendor = begin;
    if (begin) {
      cfg->parse_item.have_vendor = 0;
    } else if (!cfg->parse_item.have_vendor) {
      bufcpy(e, n, "missing ecmg");
      return -1;
    }
  }
  return 0;
}

dipitvhead_input_t *tvh_cur_input(config_t *cfg, char *e, size_t n) {
  if (!cfg->parse_item.input) {
    bufcpy(e, n, "only valid inside an input list");
    return NULL;
  }
  return &cfg->inputs[cfg->n_inputs - 1];
}

cas_vendor_t *tvh_cur_vendor(config_t *cfg, char *e, size_t n) {
  if (!cfg->parse_item.vendor) {
    bufcpy(e, n, "only valid inside a cas.ecmg list");
    return NULL;
  }
  return &cfg->cas_vendors[cfg->n_cas_vendors - 1];
}
