/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/xml_util/xml_util_int.h"

static const char *xml_find(const char *p, const char *end, const char *needle) {
  size_t nlen = strlen(needle);
  if (p >= end || (size_t)(end - p) < nlen) return NULL;
  return memmem(p, (size_t)(end - p), needle, nlen);
}

/* past a "<!..." declaration, bracket and quote aware. end if unterminated */
static const char *skip_decl(const char *p, const char *end) {
  int depth = 0;
  char q = 0;
  for (; p < end; p++) {
    if (q) {
      if (*p == q) q = 0;
    } else if (*p == '"' || *p == '\'') q = *p;
    else if (*p == '[') depth++;
    else if (*p == ']' && depth > 0) depth--;
    else if (*p == '>' && depth == 0) return p + 1;
  }
  return end;
}

/* past comment, PI or declaration at lt. NULL if lt starts a tag or CDATA */
const char *skip_misc(const char *lt, const char *end) {
  size_t n = (size_t)(end - lt);
  const char *q;
  if (n >= 4 && !memcmp(lt, "<!--", 4)) {
    q = xml_find(lt + 4, end, "-->");
    return q ? q + 3 : end;
  }
  if (n >= 9 && !memcmp(lt, "<![CDATA[", 9)) return NULL;
  if (n >= 2 && lt[1] == '?') {
    q = xml_find(lt + 2, end, "?>");
    return q ? q + 2 : end;
  }
  if (n >= 2 && lt[1] == '!') return skip_decl(lt + 2, end);
  return NULL;
}

static int is_name_end(char c) { return c == '>' || c == '/' || c == '<' || c == '=' || isspace((unsigned char)c); }

/* next start/empty/close tag at or after p, skipping text, comments, CDATA, PIs. 0 found, -1 none */
int next_tok(const char *p, const char *end, xml_tok_t *t) {
  while (p < end) {
    const char *lt = memchr(p, '<', (size_t)(end - p));
    const char *q;
    char quote = 0;
    if (!lt || lt + 1 >= end) return -1;
    q = skip_misc(lt, end);
    if (q) {
      p = q;
      continue;
    }
    if (end - lt >= 9 && !memcmp(lt, "<![CDATA[", 9)) {
      q = xml_find(lt + 9, end, "]]>");
      if (!q) return -1;
      p = q + 3;
      continue;
    }
    t->lt = lt;
    t->close = lt[1] == '/';
    t->name = lt + 1 + t->close;
    q = t->name;
    while (q < end && !is_name_end(*q)) q++;
    t->nlen = (size_t)(q - t->name);
    if (!t->nlen) {
      p = lt + 1;
      continue;
    }
    for (; q < end; q++) {
      if (quote) {
        if (*q == quote) quote = 0;
      } else if (*q == '"' || *q == '\'') quote = *q;
      else if (*q == '>') break;
    }
    t->gt = q;
    t->empty = !t->close && q < end && q[-1] == '/';
    return 0;
  }
  return -1;
}

const char *tok_after(const xml_tok_t *t, const char *end) { return t->gt < end ? t->gt + 1 : end; }

int name_is(const char *n, size_t nl, const char *want) {
  size_t wl = strlen(want);
  size_t i = nl;
  if (nl == wl && !memcmp(n, want, nl)) return 1;
  while (i > 0 && n[i - 1] != ':') i--;
  return nl - i == wl && !memcmp(n + i, want, wl);
}

/* matching close tag of non-empty start tag st, same-name nesting counted. 0 found, -1 none */
int find_close(const xml_tok_t *st, const char *end, xml_tok_t *cl) {
  const char *p;
  int depth = 1;
  if (st->gt >= end) return -1;
  p = st->gt + 1;
  while (p < end && next_tok(p, end, cl) == 0) {
    p = tok_after(cl, end);
    if (cl->nlen != st->nlen || memcmp(cl->name, st->name, st->nlen)) continue;
    if (cl->close) {
      if (--depth == 0) return 0;
    } else if (!cl->empty) depth++;
  }
  return -1;
}

/* name="v" / name='v' pairs in [p,end), value decoded. stray tokens skipped. 0 ok, -1 not found */
int attr_lookup(const char *p, const char *end, const char *name, char *out, size_t outcap, int *truncated) {
  while (p < end) {
    const char *n;
    const char *v;
    size_t nl;
    char q;
    while (p < end && (isspace((unsigned char)*p) || *p == '/')) p++;
    n = p;
    while (p < end && !is_name_end(*p) && *p != '"' && *p != '\'') p++;
    nl = (size_t)(p - n);
    if (!nl) {
      p++;
      continue;
    }
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p >= end || *p != '=') continue;
    p++;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p >= end || (*p != '"' && *p != '\'')) continue;
    q = *p++;
    v = p;
    p = memchr(v, q, (size_t)(end - v));
    if (!p) return -1;
    if (name_is(n, nl, name)) {
      sink_t k;
      sink_init(&k, out, outcap);
      sink_decode(&k, v, (size_t)(p - v));
      sink_finish(&k, truncated);
      return 0;
    }
    p++;
  }
  return -1;
}

/* text content of [p,end): text and CDATA, markup dropped */
void gather_text(const char *p, const char *end, sink_t *k) {
  while (p < end && !k->cut) {
    const char *lt = memchr(p, '<', (size_t)(end - p));
    const char *q;
    xml_tok_t t;
    sink_decode(k, p, (size_t)((lt ? lt : end) - p));
    if (!lt) return;
    if (end - lt >= 9 && !memcmp(lt, "<![CDATA[", 9)) {
      q = xml_find(lt + 9, end, "]]>");
      sink_put(k, lt + 9, (size_t)((q ? q : end) - (lt + 9)));
      p = q ? q + 3 : end;
      continue;
    }
    q = skip_misc(lt, end);
    if (!q && next_tok(lt, end, &t) == 0 && t.lt == lt) q = tok_after(&t, end);
    p = q ? q : lt + 1;
  }
}
