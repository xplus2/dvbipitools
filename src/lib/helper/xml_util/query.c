/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>

#include "lib/helper/xml_util.h"
#include "lib/helper/xml_util/xml_util_int.h"

int xml_span_text_chk(const char *tag, const char *blk_end, char *out, size_t outcap, int *truncated) {
  xml_tok_t t;
  sink_t k;
  if (next_tok(tag, blk_end, &t) || t.lt != tag || t.close || t.empty || t.gt >= blk_end) return -1;
  sink_init(&k, out, outcap);
  gather_text(t.gt + 1, blk_end, &k);
  sink_finish(&k, truncated);
  return 0;
}

const char *xml_find_start(const char *s, const char *end, const char *name) {
  const char *p = s;
  xml_tok_t t;
  while (p < end && next_tok(p, end, &t) == 0) {
    p = tok_after(&t, end);
    if (!t.close && name_is(t.name, t.nlen, name)) return t.lt;
  }
  return NULL;
}

int xml_find_elem(const char *s, const char *end, const char *name, xml_span_t *sp) {
  const char *p = s;
  xml_tok_t t;
  xml_tok_t cl;
  while (p < end && next_tok(p, end, &t) == 0) {
    p = tok_after(&t, end);
    if (t.close || !name_is(t.name, t.nlen, name)) continue;
    sp->tag = t.lt;
    if (t.empty) {
      sp->end = t.gt - 1;
      return 0;
    }
    if (find_close(&t, end, &cl)) return -1;
    sp->end = cl.lt;
    return 0;
  }
  return -1;
}

int xml_elem_text_chk(const char *s, const char *end, const char *tag, char *out, size_t outcap, int *truncated) {
  xml_span_t sp;
  if (xml_find_elem(s, end, tag, &sp)) return -1;
  return xml_span_text_chk(sp.tag, sp.end, out, outcap, truncated);
}

int xml_elem_text_dup(const char *s, const char *end, const char *tag, char **out) {
  xml_span_t sp;
  size_t cap;
  char *buf;
  char *shrunk;
  if (xml_find_elem(s, end, tag, &sp)) return -1;
  cap = (size_t)(sp.end - sp.tag) + 1;
  buf = malloc(cap);
  if (!buf) return -2;
  if (xml_span_text_chk(sp.tag, sp.end, buf, cap, NULL)) {
    free(buf);
    return -1;
  }
  shrunk = realloc(buf, strlen(buf) + 1);
  if (shrunk) buf = shrunk;
  *out = buf;
  return 0;
}

int xml_elem_text(const char *s, const char *end, const char *tag, char *out, size_t outcap) {
  return xml_elem_text_chk(s, end, tag, out, outcap, NULL);
}

int for_each_xml_elem_chk(const char *buf, const char *end, const char *name, xml_block_cb cb, void *ctx, const char **unclosed) {
  const char *p = buf;
  xml_tok_t t;
  xml_tok_t cl;
  while (p < end && next_tok(p, end, &t) == 0) {
    const char *blk_end;
    p = tok_after(&t, end);
    if (t.close || !name_is(t.name, t.nlen, name)) continue;
    if (t.empty) {
      blk_end = t.gt - 1;
    } else {
      if (find_close(&t, end, &cl)) {
        if (unclosed) *unclosed = t.lt;
        return 0;
      }
      blk_end = cl.lt;
      p = tok_after(&cl, end);
    }
    if (cb(t.lt, blk_end, ctx)) return -1;
  }
  return 0;
}

int for_each_xml_elem(const char *buf, const char *end, const char *name, xml_block_cb cb, void *ctx) {
  return for_each_xml_elem_chk(buf, end, name, cb, ctx, NULL);
}

int xml_attr_chk(const char *s, const char *end, const char *name, char *out, size_t outcap, int *truncated) {
  const char *p = s;
  xml_tok_t t;
  while (p < end && next_tok(p, end, &t) == 0) {
    p = tok_after(&t, end);
    if (!t.close && attr_lookup(t.name + t.nlen, t.gt, name, out, outcap, truncated) == 0) return 0;
  }
  return -1;
}

int xml_attr(const char *s, const char *end, const char *name, char *out, size_t outcap) {
  return xml_attr_chk(s, end, name, out, outcap, NULL);
}

int xml_tag_attr_chk(const char *s, const char *end, const char *name, char *out, size_t outcap, int *truncated) {
  xml_tok_t t;
  if (next_tok(s, end, &t) || t.lt != s || t.close) return -1;
  return attr_lookup(t.name + t.nlen, t.gt, name, out, outcap, truncated);
}

int xml_tag_attr(const char *s, const char *end, const char *name, char *out, size_t outcap) {
  return xml_tag_attr_chk(s, end, name, out, outcap, NULL);
}

int xml_attr_list(const char *s, const char *end, const char *name, char *out, size_t outcap) {
  return attr_lookup(s, end, name, out, outcap, NULL);
}
