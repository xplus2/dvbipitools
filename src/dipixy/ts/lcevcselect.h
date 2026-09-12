/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_LCEVCSELECT_H
#define DIPIXY_LCEVCSELECT_H

#include <stddef.h>

typedef enum {
  LCEVC_SEL_BASE = 0,
  LCEVC_SEL_FULL,
  LCEVC_SEL_ALL,
  LCEVC_SEL_N
} lcevc_sel_mode_t;

/* LCEVC_SEL_N only: n is an index into lcevc_pid[] if n_is_pid is 0,
   else n is one of lcevc_pid[]'s own values */
typedef struct {
  lcevc_sel_mode_t mode;
  unsigned n;
  int n_is_pid;
} lcevc_select_t;

typedef enum { LCEVC_RESOLVE_NONE, LCEVC_RESOLVE_ALL, LCEVC_RESOLVE_ONE } lcevc_resolve_kind_t;

/* LCEVC_RESOLVE_ONE only: pid is the single lcevc_pid[] entry to keep */
typedef struct {
  lcevc_resolve_kind_t kind;
  unsigned pid;
} lcevc_resolved_t;

void lcevc_select_parse_query(const char *query, lcevc_select_t *out);

lcevc_resolved_t lcevc_select_resolve(const lcevc_select_t *sel, const unsigned *lcevc_pid, int lcevc_pid_count);

int lcevc_select_equal(const lcevc_select_t *a, const lcevc_select_t *b);

void lcevc_select_format(const lcevc_select_t *sel, char *buf, size_t bufsz);

#endif
