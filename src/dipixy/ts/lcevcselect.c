/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lcevcselect.h"
#include "pidfilter.h"

#include <string.h>

#include "lib/helper/ioutil.h"

void lcevc_select_parse_query(const char *query, lcevc_select_t *out) {
  char buf[16];
  char *end;
  unsigned long v;
  size_t len;
  int is_hex;

  out->mode = LCEVC_SEL_FULL;
  out->n = 0;
  out->n_is_pid = 0;
  if (!query_param_extract(query, "lcevc=", buf, sizeof buf)) return;
  if (!strcmp(buf, "base")) {
    out->mode = LCEVC_SEL_BASE;
    return;
  }
  if (!strcmp(buf, "all")) {
    out->mode = LCEVC_SEL_ALL;
    return;
  }
  if (!strcmp(buf, "full")) return;
  if (!pid_token_parse(buf, &end, &v) || *end != '\0') return;
  len = strlen(buf);
  is_hex = buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X');
  out->mode = LCEVC_SEL_N;
  out->n = (unsigned)v;
  out->n_is_pid = is_hex || len != 1;
}

lcevc_resolved_t lcevc_select_resolve(const lcevc_select_t *sel, const unsigned *lcevc_pid, int lcevc_pid_count) {
  lcevc_resolved_t r = {0};

  if (sel->mode == LCEVC_SEL_BASE) {
    r.kind = LCEVC_RESOLVE_NONE;
    return r;
  }
  if (sel->mode == LCEVC_SEL_N) {
    if (sel->n_is_pid) {
      for (int i = 0; i < lcevc_pid_count; i++)
        if (lcevc_pid[i] == sel->n) {
          r.kind = LCEVC_RESOLVE_ONE;
          r.pid = sel->n;
          return r;
        }
    } else if ((int)sel->n < lcevc_pid_count) {
      r.kind = LCEVC_RESOLVE_ONE;
      r.pid = lcevc_pid[sel->n];
      return r;
    }
  }
  r.kind = LCEVC_RESOLVE_ALL;
  return r;
}

int lcevc_select_equal(const lcevc_select_t *a, const lcevc_select_t *b) {
  return a->mode == b->mode && (a->mode != LCEVC_SEL_N || (a->n == b->n && a->n_is_pid == b->n_is_pid));
}

void lcevc_select_format(const lcevc_select_t *sel, char *buf, size_t bufsz) {
  if (!bufsz) return;
  buf[0] = '\0';
  switch (sel->mode) {
    case LCEVC_SEL_BASE:
      bufcpy(buf, bufsz, "base");
      return;
    case LCEVC_SEL_ALL:
      bufcpy(buf, bufsz, "all");
      return;
    case LCEVC_SEL_N: {
      char frag[16];
      uint_to_str(frag, sel->n);
      bufcpy(buf, bufsz, frag);
      return;
    }
    case LCEVC_SEL_FULL:
    default:
      return;
  }
}
