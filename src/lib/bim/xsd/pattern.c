/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/xsd/xsd.h"

/* XML Schema Part 2 Appendix F pattern facet, matched against whole value */

#define DEPTH_MAX 64
#define STEPS_MAX 1000000
#define RECURSE_MAX 10000 /* matcher frames, well below default 8 MiB stack */

typedef struct {
  uint32_t lo;
  uint32_t hi;
} span_t;

/* XML 1.0 5th edition NameStartChar, NameChar extra */
static const span_t name_start[] = {{':', ':'},       {'A', 'Z'},       {'_', '_'},       {'a', 'z'},       {0xC0, 0xD6},     {0xD8, 0xF6},
                                    {0xF8, 0x2FF},    {0x370, 0x37D},   {0x37F, 0x1FFF},  {0x200C, 0x200D}, {0x2070, 0x218F}, {0x2C00, 0x2FEF},
                                    {0x3001, 0xD7FF}, {0xF900, 0xFDCF}, {0xFDF0, 0xFFFD}, {0x10000, 0xEFFFF}};
static const span_t name_more[] = {{'-', '.'}, {'0', '9'}, {0xB7, 0xB7}, {0x300, 0x36F}, {0x203F, 0x2040}};
/* Unicode Nd */
static const span_t digits[] = {{0x30, 0x39},     {0x660, 0x669},   {0x6F0, 0x6F9},   {0x7C0, 0x7C9},   {0x966, 0x96F},   {0x9E6, 0x9EF},
                                {0xA66, 0xA6F},   {0xAE6, 0xAEF},   {0xB66, 0xB6F},   {0xBE6, 0xBEF},   {0xC66, 0xC6F},   {0xCE6, 0xCEF},
                                {0xD66, 0xD6F},   {0xDE6, 0xDEF},   {0xE50, 0xE59},   {0xED0, 0xED9},   {0xF20, 0xF29},   {0x1040, 0x1049},
                                {0x1090, 0x1099}, {0x17E0, 0x17E9}, {0x1810, 0x1819}, {0x1946, 0x194F}, {0x19D0, 0x19D9}, {0x1A80, 0x1A89},
                                {0x1A90, 0x1A99}, {0x1B50, 0x1B59}, {0x1BB0, 0x1BB9}, {0x1C40, 0x1C49}, {0x1C50, 0x1C59}, {0xA620, 0xA629},
                                {0xA8D0, 0xA8D9}, {0xA900, 0xA909}, {0xA9D0, 0xA9D9}, {0xA9F0, 0xA9F9}, {0xAA50, 0xAA59}, {0xABF0, 0xABF9},
                                {0xFF10, 0xFF19}};

static int in_spans(const span_t *s, size_t n, uint32_t c) {
  for (size_t i = 0; i < n; i++)
    if (c >= s[i].lo && c <= s[i].hi) return 1;
  return 0;
}

/* 1 member. kind: 'd' digit, 's' space, 'i' name start, 'c' name char */
static int in_multi(char kind, uint32_t c) {
  switch (kind) {
    case 'd': return in_spans(digits, sizeof digits / sizeof digits[0], c);
    case 's': return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    case 'i': return in_spans(name_start, sizeof name_start / sizeof name_start[0], c);
    default: return in_spans(name_start, sizeof name_start / sizeof name_start[0], c) || in_spans(name_more, sizeof name_more / sizeof name_more[0], c);
  }
}

/* next code point, -1 malformed */
static int64_t utf8_next(const char **p, const char *end) {
  const unsigned char *s = (const unsigned char *)*p;
  uint32_t c;
  int n;
  if (s >= (const unsigned char *)end) return -1;
  if (s[0] < 0x80) {
    (*p)++;
    return s[0];
  }
  if (s[0] >= 0xF0) n = 3;
  else if (s[0] >= 0xE0) n = 2;
  else if (s[0] >= 0xC0) n = 1;
  else n = -1;
  if (n < 0 || end - *p <= n) return -1;
  c = s[0] & (0x3F >> n);
  for (int i = 1; i <= n; i++) {
    if ((s[i] & 0xC0) != 0x80) return -1;
    c = c << 6 | (s[i] & 0x3F);
  }
  *p += n + 1;
  return c;
}

typedef struct cls cls_t;

/* class item: range lo..hi, or multi char escape */
typedef struct {
  uint32_t lo;
  uint32_t hi;
  char multi;
  int neg;
} item_t;

struct cls {
  int neg;
  item_t *v;
  int n;
  cls_t *sub; /* subtracted class */
};

typedef struct seq seq_t;

typedef enum { N_CHAR, N_ANY, N_CLASS, N_MULTI, N_GROUP } nkind_t;

typedef struct {
  nkind_t kind;
  uint32_t c;
  char multi;
  int neg;
  cls_t *cls;
  seq_t *alts; /* N_GROUP */
  int nalts;
  unsigned min;
  unsigned max;
} node_t;

struct seq {
  node_t *v;
  int n;
};

struct xsd_pattern {
  seq_t *alts;
  int nalts;
};

typedef struct {
  xsd_set_t *s;
  const char *p;
  const char *end;
  int bad;
  int depth;
} rd_t;

static void *ralloc(rd_t *r, size_t n) {
  void *v = xsd_set_alloc(r->s, n ? n : 1);
  if (!v) r->bad = 1;
  return v;
}

/* single char escape, or multi char escape in *multi (upper case escape: *neg) */
static int64_t escape(rd_t *r, char *multi, int *neg) {
  char e;
  *multi = 0;
  *neg = 0;
  if (r->p >= r->end) return -1;
  e = *r->p++;
  switch (e) {
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case '\\': case '|': case '.': case '-': case '^': case '?': case '*': case '+': case '{': case '}': case '(': case ')': case '[': case ']': return e;
    case 'd': case 's': case 'i': case 'c': *multi = e; return 0;
    case 'D': case 'S': case 'I': case 'C':
      *multi = (char)(e - 'A' + 'a');
      *neg = 1;
      return 0;
    default: return -1; /* \p \P \w \W: Unicode categories, unsupported */
  }
}

static int cls_add(rd_t *r, cls_t *k, item_t it) {
  item_t *nv = ralloc(r, sizeof *nv * (size_t)(k->n + 1));
  if (!nv) return -1;
  if (k->n) memcpy(nv, k->v, sizeof *nv * (size_t)k->n);
  nv[k->n++] = it;
  k->v = nv;
  return 0;
}

/* after '[' up to and incl. ']' */
static cls_t *parse_class(rd_t *r) {
  cls_t *k = ralloc(r, sizeof *k);
  if (!k) return NULL;
  if (++r->depth > DEPTH_MAX) return NULL;
  if (r->p < r->end && *r->p == '^') {
    k->neg = 1;
    r->p++;
  }
  while (r->p < r->end && *r->p != ']') {
    item_t it = {0, 0, 0, 0};
    int64_t lo;
    if (*r->p == '-' && r->p + 1 < r->end && r->p[1] == '[' && k->n) {
      r->p += 2;
      if (!(k->sub = parse_class(r))) return NULL;
      break;
    }
    if (*r->p == '[') return NULL;
    if (*r->p == '\\') {
      r->p++;
      if ((lo = escape(r, &it.multi, &it.neg)) < 0) return NULL;
    } else if ((lo = utf8_next(&r->p, r->end)) < 0) {
      return NULL;
    }
    it.hi = (uint32_t)lo;
    it.lo = it.hi;
    if (!it.multi && r->p + 1 < r->end && *r->p == '-' && r->p[1] != '[' && r->p[1] != ']') {
      int64_t hi;
      char m;
      int ng;
      r->p++;
      if (*r->p == '\\') {
        r->p++;
        hi = escape(r, &m, &ng);
        if (m) hi = -1;
      } else {
        hi = utf8_next(&r->p, r->end);
      }
      if (hi < lo) return NULL;
      it.hi = (uint32_t)hi;
    }
    if (cls_add(r, k, it)) return NULL;
  }
  if (r->p >= r->end || *r->p != ']' || !k->n) return NULL;
  r->p++;
  r->depth--;
  return k;
}

static int cls_has(const cls_t *k, uint32_t c) {
  int hit = 0;
  for (int i = 0; i < k->n && !hit; i++) hit = k->v[i].multi ? in_multi(k->v[i].multi, c) != k->v[i].neg : c >= k->v[i].lo && c <= k->v[i].hi;
  if (k->neg) hit = !hit;
  return hit && !(k->sub && cls_has(k->sub, c));
}

static int parse_alts(rd_t *r, seq_t **alts, int *nalts, int nested);

static int parse_quant(rd_t *r, node_t *n) {
  n->max = 1;
  n->min = n->max;
  if (r->p >= r->end) return 0;
  if (*r->p == '?' || *r->p == '*' || *r->p == '+') {
    n->min = *r->p == '+';
    n->max = *r->p == '?' ? 1 : UINT32_MAX;
    r->p++;
    return 0;
  }
  if (*r->p == '{') {
    unsigned long a = 0;
    unsigned long b;
    r->p++;
    if (r->p >= r->end || *r->p < '0' || *r->p > '9') return -1;
    while (r->p < r->end && *r->p >= '0' && *r->p <= '9' && a < 100000) a = a * 10 + (unsigned long)(*r->p++ - '0');
    b = a;
    if (r->p < r->end && *r->p == ',') {
      r->p++;
      b = UINT32_MAX;
      if (r->p < r->end && *r->p >= '0' && *r->p <= '9')
        for (b = 0; r->p < r->end && *r->p >= '0' && *r->p <= '9' && b < 100000;) b = b * 10 + (unsigned long)(*r->p++ - '0');
    }
    if (r->p >= r->end || *r->p != '}' || b < a || a >= 100000) return -1;
    r->p++;
    n->min = (unsigned)a;
    n->max = (unsigned)b;
  }
  return 0;
}

static int parse_seq(rd_t *r, seq_t *q, int nested) {
  while (r->p < r->end && *r->p != '|' && !(nested && *r->p == ')')) {
    node_t n;
    node_t *nv;
    memset(&n, 0, sizeof n);
    if (*r->p == '(') {
      r->p++;
      if (++r->depth > DEPTH_MAX || parse_alts(r, &n.alts, &n.nalts, 1) || r->p >= r->end || *r->p != ')') return -1;
      r->p++;
      r->depth--;
      n.kind = N_GROUP;
    } else if (*r->p == '[') {
      r->p++;
      if (!(n.cls = parse_class(r))) return -1;
      n.kind = N_CLASS;
    } else if (*r->p == '.') {
      r->p++;
      n.kind = N_ANY;
    } else if (*r->p == '\\') {
      int64_t e;
      r->p++;
      if ((e = escape(r, &n.multi, &n.neg)) < 0) return -1;
      n.kind = n.multi ? N_MULTI : N_CHAR;
      n.c = (uint32_t)e;
    } else {
      int64_t c;
      if (*r->p == '?') return -1;
      if (*r->p == '*') return -1;
      if (*r->p == '+') return -1;
      if (*r->p == '{') return -1;
      if (*r->p == ')') return -1;
      if (*r->p == ']') return -1;
      if ((c = utf8_next(&r->p, r->end)) < 0) return -1;
      n.kind = N_CHAR;
      n.c = (uint32_t)c;
    }
    if (parse_quant(r, &n)) return -1;
    nv = ralloc(r, sizeof *nv * (size_t)(q->n + 1));
    if (!nv) return -1;
    if (q->n) memcpy(nv, q->v, sizeof *nv * (size_t)q->n);
    nv[q->n++] = n;
    q->v = nv;
  }
  return 0;
}

static int parse_alts(rd_t *r, seq_t **alts, int *nalts, int nested) {
  *alts = NULL;
  *nalts = 0;
  for (;;) {
    seq_t *nv = ralloc(r, sizeof *nv * (size_t)(*nalts + 1));
    if (!nv) return -1;
    if (*nalts) memcpy(nv, *alts, sizeof *nv * (size_t)*nalts);
    memset(&nv[*nalts], 0, sizeof *nv);
    *alts = nv;
    if (parse_seq(r, &nv[(*nalts)++], nested)) return -1;
    if (r->p >= r->end || *r->p != '|') return 0;
    r->p++;
  }
}

/* continuation: rest of seq q from i, or one more repetition of node */
typedef struct cont {
  const seq_t *q;
  int i;
  const node_t *node; /* set: repetition frame */
  unsigned cnt;
  const char *start;
  const struct cont *k;
} cont_t;

typedef struct {
  const char *end;
  long steps;
  int bad;
} mt_t;

static int m_seq(mt_t *m, const seq_t *q, int i, const char *s, const cont_t *k, int depth);

static int m_rep(mt_t *m, const node_t *n, unsigned cnt, const char *s, const seq_t *q, int i, const cont_t *k, int depth);

static int m_cont(mt_t *m, const cont_t *k, const char *s, int depth) {
  if (!k) return s == m->end;
  if (k->node) return s != k->start && m_rep(m, k->node, k->cnt + 1, s, k->q, k->i, k->k, depth);
  return m_seq(m, k->q, k->i, s, k->k, depth);
}

/* single code point atom at *p, advances on match */
static int atom_take(const node_t *n, const char **p, const char *end) {
  const char *s = *p;
  int64_t c = utf8_next(&s, end);
  int ok = c >= 0;
  if (ok && n->kind == N_CHAR) ok = (uint32_t)c == n->c;
  else if (ok && n->kind == N_ANY) ok = c != '\n' && c != '\r';
  else if (ok && n->kind == N_CLASS) ok = cls_has(n->cls, (uint32_t)c);
  else if (ok) ok = in_multi(n->multi, (uint32_t)c) != n->neg;
  if (ok) *p = s;
  return ok;
}

static int m_rep(mt_t *m, const node_t *n, unsigned cnt, const char *s, const seq_t *q, int i, const cont_t *k, int depth) {
  if (++m->steps > STEPS_MAX || depth > 4 * DEPTH_MAX + RECURSE_MAX) {
    m->bad = 1;
    return 0;
  }
  if (n->kind != N_GROUP) {
    /* greedy run, then back off one code point at a time: no frame per character */
    const char *p = s;
    while (cnt < n->max && atom_take(n, &p, m->end)) cnt++;
    for (;;) {
      if (cnt >= n->min && m_seq(m, q, i + 1, p, k, depth + 1)) return 1;
      if (m->bad) return 0;
      if (cnt <= n->min) return 0;
      if (++m->steps > STEPS_MAX) return 0;
      do {
        p--;
      } while (p > s && ((unsigned char)*p & 0xC0) == 0x80);
      cnt--;
    }
  }
  if (cnt < n->max) {
    cont_t rep = {q, i, n, cnt, s, k};
    for (int a = 0; a < n->nalts; a++)
      if (m_seq(m, &n->alts[a], 0, s, &rep, depth + 1)) return 1;
  }
  return cnt >= n->min && m_seq(m, q, i + 1, s, k, depth + 1);
}

static int m_seq(mt_t *m, const seq_t *q, int i, const char *s, const cont_t *k, int depth) {
  if (m->bad) return 0;
  if (i >= q->n) return m_cont(m, k, s, depth);
  return m_rep(m, &q->v[i], 0, s, q, i, k, depth);
}

xsd_pattern_t *xsd_pattern_compile(xsd_set_t *s, const char *re) {
  rd_t r = {s, re, re + strlen(re), 0, 0};
  xsd_pattern_t *p = xsd_set_alloc(s, sizeof *p);
  if (!p || parse_alts(&r, &p->alts, &p->nalts, 0) || r.bad || r.p != r.end) return NULL;
  return p;
}

int xsd_pattern_match(const void *pv, const char *v) {
  const xsd_pattern_t *p = pv;
  mt_t m = {v + strlen(v), 0, 0};
  for (int a = 0; a < p->nalts; a++)
    if (m_seq(&m, &p->alts[a], 0, v, NULL, 0)) return 1;
  return m.bad ? -1 : 0;
}
