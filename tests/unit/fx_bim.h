/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_FX_BIM_H
#define DVBIPITOOLS_TESTS_UNIT_FX_BIM_H

#include <check.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/bim/nodexml.h"

static inline int ident(const unsigned char *in, size_t n, unsigned char *out, size_t *outlen) {
  if (n > *outlen) return -1;
  memcpy(out, in, n);
  *outlen = n;
  return 0;
}

static inline int tree_same(const bim_node_t *a, const bim_node_t *b) {
  if (!a || !b) return a == b;
  if (strcmp(a->name ? a->name : "", b->name ? b->name : "") || strcmp(a->text ? a->text : "", b->text ? b->text : "") || a->nattrs != b->nattrs || a->nkids != b->nkids) return 0;
  for (int i = 0; i < a->nattrs; i++)
    if (strcmp(a->attrs[i].name, b->attrs[i].name) || strcmp(a->attrs[i].value, b->attrs[i].value)) return 0;
  for (int i = 0; i < a->nkids; i++)
    if (!tree_same(a->kids[i], b->kids[i])) return 0;
  return 1;
}

#endif
