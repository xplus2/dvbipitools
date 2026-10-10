/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "xml_util.h"

static int utf8_encode(unsigned long cp, char *out) {
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

void xml_escape(FILE *f, const char *s) {
  for (; *s; s++) {
    switch (*s) {
    case '&': fputs("&amp;", f); break;
    case '<': fputs("&lt;", f); break;
    case '>': fputs("&gt;", f); break;
    case '"': fputs("&quot;", f); break;
    case '\'': fputs("&apos;", f); break;
    default: fputc(*s, f); break;
    }
  }
}

typedef struct {
  char *out;
  size_t cap, len;
  int cut;
} sink_t;

static void sink_put(sink_t *k, const char *src, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (k->len + 1 >= k->cap) {
      k->cut = 1;
      return;
    }
    k->out[k->len++] = src[i];
  }
}

static const struct {
  const char *ent;
  size_t len;
  char ch;
} entities[] = {
    {"&amp;", 5, '&'}, {"&lt;", 4, '<'}, {"&gt;", 4, '>'}, {"&quot;", 6, '"'}, {"&apos;", 6, '\''},
};

/* &#NNN; or &#xHH; at src. consumed length, 0 if invalid */
static size_t numeric_ref(const char *src, size_t n, sink_t *k) {
  size_t j = 2;
  int hex = 0;
  unsigned long cp;
  char *endp;
  char utf8[4];
  int len;

  if (j < n && (src[j] == 'x' || src[j] == 'X')) {
    hex = 1;
    j++;
  }
  if (j >= n || !isxdigit((unsigned char)src[j])) return 0;
  cp = strtoul(src + j, &endp, hex ? 16 : 10);
  if ((size_t)(endp - src) >= n || *endp != ';') return 0;
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  len = utf8_encode(cp, utf8);
  sink_put(k, utf8, (size_t)len);
  return (size_t)(endp - src) + 1;
}

static void sink_decode(sink_t *k, const char *src, size_t n) {
  size_t i = 0;
  while (i < n && !k->cut) {
    size_t used = 0;
    if (src[i] == '&') {
      for (size_t e = 0; e < sizeof entities / sizeof entities[0] && !used; e++) {
        if (n - i >= entities[e].len && !memcmp(src + i, entities[e].ent, entities[e].len)) {
          sink_put(k, &entities[e].ch, 1);
          used = entities[e].len;
        }
      }
      if (!used && n - i > 2 && src[i + 1] == '#') used = numeric_ref(src + i, n - i, k);
    }
    if (!used) {
      sink_put(k, src + i, 1);
      used = 1;
    }
    i += used;
  }
}

/* len minus a trailing incomplete UTF-8 sequence */
static size_t utf8_complete_len(const char *s, size_t len) {
  size_t i = len, need;
  unsigned char lead;
  while (i > 0 && len - i < 3 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) i--;
  if (i == 0) return len;
  lead = (unsigned char)s[i - 1];
  need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
  return need > len - (i - 1) ? i - 1 : len;
}

static void sink_init(sink_t *k, char *out, size_t cap) {
  k->out = out;
  k->cap = cap;
  k->len = 0;
  k->cut = 0;
}

/* NUL-terminates, trims a cut UTF-8 sequence. returns cut flag */
static int sink_finish(sink_t *k, int *truncated) {
  if (k->cut) k->len = utf8_complete_len(k->out, k->len);
  k->out[k->len] = '\0';
  if (truncated) *truncated = k->cut;
  return k->cut;
}

static const char *xml_find(const char *p, const char *end, const char *needle) {
  size_t nlen = strlen(needle);
  if (p >= end || (size_t)(end - p) < nlen) return NULL;
  return memmem(p, (size_t)(end - p), needle, nlen);
}

typedef struct {
  const char *lt;
  const char *name;
  size_t nlen;
  const char *gt; /* '>' or end when unterminated */
  int close;
  int empty;
} xml_tok_t;

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
static const char *skip_misc(const char *lt, const char *end) {
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
static int next_tok(const char *p, const char *end, xml_tok_t *t) {
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
    for (q = t->name; q < end && !is_name_end(*q); q++) {
    }
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

static const char *tok_after(const xml_tok_t *t, const char *end) { return t->gt < end ? t->gt + 1 : end; }

static int name_is(const char *n, size_t nl, const char *want) {
  size_t wl = strlen(want), i = nl;
  if (nl == wl && !memcmp(n, want, nl)) return 1;
  while (i > 0 && n[i - 1] != ':') i--;
  return nl - i == wl && !memcmp(n + i, want, wl);
}

/* matching close tag of non-empty start tag st, same-name nesting counted. 0 found, -1 none */
static int find_close(const xml_tok_t *st, const char *end, xml_tok_t *cl) {
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
static int attr_lookup(const char *p, const char *end, const char *name, char *out, size_t outcap, int *truncated) {
  while (p < end) {
    const char *n, *v;
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
static void gather_text(const char *p, const char *end, sink_t *k) {
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
  xml_tok_t t, cl;
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

int xml_elem_text(const char *s, const char *end, const char *tag, char *out, size_t outcap) {
  return xml_elem_text_chk(s, end, tag, out, outcap, NULL);
}

int for_each_xml_elem(const char *buf, const char *end, const char *name, xml_block_cb cb, void *ctx) {
  const char *p = buf;
  xml_tok_t t, cl;
  while (p < end && next_tok(p, end, &t) == 0) {
    const char *blk_end;
    p = tok_after(&t, end);
    if (t.close || !name_is(t.name, t.nlen, name)) continue;
    if (t.empty) {
      blk_end = t.gt - 1;
    } else {
      if (find_close(&t, end, &cl)) return 0;
      blk_end = cl.lt;
      p = tok_after(&cl, end);
    }
    if (cb(t.lt, blk_end, ctx)) return -1;
  }
  return 0;
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
