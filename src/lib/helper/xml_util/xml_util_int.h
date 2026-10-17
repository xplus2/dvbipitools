/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_HELPER_XML_UTIL_XML_UTIL_INT_H
#define LIB_HELPER_XML_UTIL_XML_UTIL_INT_H

#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>
#include "lib/helper/xml_util.h"

typedef struct {
  char *out;
  size_t cap;
  size_t len;
  int cut;
} sink_t;
typedef struct {
  const char *lt;
  const char *name;
  size_t nlen;
  const char *gt; /* '>' or end when unterminated */
  int close;
  int empty;
} xml_tok_t;

int utf8_encode(unsigned long cp, char *out);
void sink_put(sink_t *k, const char *src, size_t n);
void sink_decode(sink_t *k, const char *src, size_t n);
void sink_init(sink_t *k, char *out, size_t cap);
int sink_finish(sink_t *k, int *truncated);
int next_tok(const char *p, const char *end, xml_tok_t *t);
const char *tok_after(const xml_tok_t *t, const char *end);
int name_is(const char *n, size_t nl, const char *want);
int find_close(const xml_tok_t *st, const char *end, xml_tok_t *cl);
int attr_lookup(const char *p, const char *end, const char *name, char *out, size_t outcap, int *truncated);
void gather_text(const char *p, const char *end, sink_t *k);

#endif
