/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/codec.h"
#include "lib/helper/xml_util/xml_util_int.h"
#include "lib/sys/ioutil.h"

#define XML_DEPTH_MAX 256
#define POOL_CHUNK 16384

typedef struct pool_chunk {
  struct pool_chunk *next;
  size_t used;
  size_t cap;
  char data[];
} pool_chunk_t;

struct xml_doc {
  bim_node_t *root;
  pool_chunk_t *chunks;
  const char **slots; /* interned strings, open addressing */
  size_t nslots;
  size_t nused;
};

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} gbuf_t;

typedef struct {
  const char *name;
  size_t nlen;
  const char *val;
  size_t vlen;
} raw_attr_t;

typedef struct {
  xml_doc_t *doc;
  const char *end;
  const char *err;
  int oom;
  bim_node_t *cur;
  int depth;
  const char *qname[XML_DEPTH_MAX];
  size_t qlen[XML_DEPTH_MAX];
  int has_kids[XML_DEPTH_MAX];
  gbuf_t text; /* pending decoded text of cur */
  gbuf_t scratch;
  raw_attr_t *attrs;
  int acap;
} parser_t;

static int gbuf_reserve(gbuf_t *b, size_t extra) {
  char *nb;
  size_t cap;
  if (b->len + extra + 1 <= b->cap) return 0;
  cap = b->cap ? b->cap : 256;
  while (cap < b->len + extra + 1) cap *= 2;
  nb = realloc(b->buf, cap);
  if (!nb) return -1;
  b->buf = nb;
  b->cap = cap;
  return 0;
}

static char *pool_alloc(xml_doc_t *d, size_t n) {
  pool_chunk_t *c = d->chunks;
  if (!c || c->cap - c->used < n) {
    size_t cap = n > POOL_CHUNK ? n : POOL_CHUNK;
    c = malloc(sizeof *c + cap);
    if (!c) return NULL;
    c->next = d->chunks;
    c->used = 0;
    c->cap = cap;
    d->chunks = c;
  }
  c->used += n;
  return c->data + c->used - n;
}

static size_t str_hash(const char *s, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 1099511628211ull;
  return (size_t)h;
}

static size_t slot_of(const xml_doc_t *d, const char *s, size_t n) {
  size_t i = str_hash(s, n) & (d->nslots - 1);
  while (d->slots[i] && (strncmp(d->slots[i], s, n) || d->slots[i][n])) i = (i + 1) & (d->nslots - 1);
  return i;
}

static int slots_grow(xml_doc_t *d) {
  size_t old = d->nslots;
  const char **os = d->slots;
  d->nslots = old ? old * 2 : 256;
  d->slots = calloc(d->nslots, sizeof *d->slots);
  if (!d->slots) {
    d->slots = os;
    d->nslots = old;
    return -1;
  }
  for (size_t i = 0; i < old; i++)
    if (os[i]) d->slots[slot_of(d, os[i], strlen(os[i]))] = os[i];
  free(os);
  return 0;
}

static const char *intern(xml_doc_t *d, const char *s, size_t n) {
  size_t i;
  char *c;
  if ((d->nused + 1) * 2 > d->nslots && slots_grow(d)) return NULL;
  i = slot_of(d, s, n);
  if (d->slots[i]) return d->slots[i];
  c = pool_alloc(d, n + 1);
  if (!c) return NULL;
  memcpy(c, s, n);
  c[n] = '\0';
  d->slots[i] = c;
  d->nused++;
  return c;
}

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static int is_name_char(char c) { return c && !is_space(c) && c != '>' && c != '/' && c != '<' && c != '=' && c != '"' && c != '\''; }

static int fail(parser_t *ps, const char *at) {
  if (!ps->err) ps->err = at;
  return -1;
}

static int oom(parser_t *ps) {
  ps->oom = 1;
  return -1;
}

/* XML 1.0 2.11 line ends, attr: 3.3.3 whitespace. NUL-terminated, returns length */
static size_t norm_copy(char *dst, const char *s, size_t n, int attr) {
  size_t len = 0;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (c == '\r') {
      if (i + 1 < n && s[i + 1] == '\n') i++;
      c = '\n';
    }
    if (attr && (c == '\n' || c == '\t')) c = ' ';
    dst[len++] = c;
  }
  dst[len] = '\0';
  return len;
}

/* normalized, entities decoded, appended to out */
static int decode_into(parser_t *ps, gbuf_t *out, const char *s, size_t n, int attr) {
  sink_t k;
  size_t len;
  ps->scratch.len = 0;
  if (gbuf_reserve(&ps->scratch, n) || gbuf_reserve(out, n)) return oom(ps);
  len = norm_copy(ps->scratch.buf, s, n, attr);
  sink_init(&k, out->buf + out->len, n + 1);
  sink_decode(&k, ps->scratch.buf, len);
  sink_finish(&k, NULL);
  out->len += k.len;
  return 0;
}

static int only_space(const char *s, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (!is_space(s[i])) return 0;
  return 1;
}

static char *take_text(parser_t *ps) {
  char *t = malloc(ps->text.len + 1);
  if (!t) return NULL;
  memcpy(t, ps->text.buf, ps->text.len);
  t[ps->text.len] = '\0';
  ps->text.len = 0;
  return t;
}

/* pending text at child start or close: simple content, mixed text node or dropped whitespace */
static int flush_text(parser_t *ps, int closing) {
  bim_node_t *t;
  if (closing && !ps->has_kids[ps->depth - 1]) {
    if (!ps->text.len) return 0;
    ps->cur->text = take_text(ps);
    return ps->cur->text ? 0 : oom(ps);
  }
  if (only_space(ps->text.buf, ps->text.len)) {
    ps->text.len = 0;
    return 0;
  }
  t = bim_node_new(NULL);
  if (!t) return oom(ps);
  t->text = take_text(ps);
  if (!t->text || bim_node_add(ps->cur, t)) {
    bim_node_free(t);
    return oom(ps);
  }
  return 0;
}

static const char *skip_space(const char *p, const char *end) {
  while (p < end && is_space(*p)) p++;
  return p;
}

static int add_raw_attr(parser_t *ps, int n, const raw_attr_t *a) {
  if (n >= ps->acap) {
    void *np = array_grow(ps->attrs, &ps->acap, n + 1, sizeof *ps->attrs);
    if (!np) return oom(ps);
    ps->attrs = np;
  }
  ps->attrs[n] = *a;
  return 0;
}

static int is_xmlns(const raw_attr_t *a) { return a->nlen >= 5 && !memcmp(a->name, "xmlns", 5) && (a->nlen == 5 || a->name[5] == ':'); }

static int add_nsdecls(parser_t *ps, bim_node_t *n, int nattrs, const char *at) {
  gbuf_t v = {NULL, 0, 0};
  int rc = 0;
  for (int i = 0; i < nattrs && !rc; i++) {
    const raw_attr_t *a = &ps->attrs[i];
    bim_nsdecl_t *d;
    void *np;
    if (!is_xmlns(a)) continue;
    v.len = 0;
    np = realloc(n->nsdecls, sizeof *n->nsdecls * (size_t)(n->nnsdecls + 1));
    if (!np) {
      rc = oom(ps);
      break;
    }
    n->nsdecls = np;
    if (decode_into(ps, &v, a->val, a->vlen, 1)) {
      rc = -1;
      break;
    }
    d = &n->nsdecls[n->nnsdecls++];
    d->prefix = a->nlen == 5 ? intern(ps->doc, "", 0) : intern(ps->doc, a->name + 6, a->nlen - 6);
    d->uri = intern(ps->doc, v.buf, v.len);
    if (!d->prefix || !d->uri) rc = oom(ps);
    else if (a->nlen > 5 && !v.len) rc = fail(ps, at); /* xmlns:p="": XML 1.1 only */
  }
  free(v.buf);
  return rc;
}

/* qualified name in scope of n to ns and interned local name. dflt: default namespace applies */
static int resolve(parser_t *ps, const bim_node_t *n, const char *q, size_t ql, int dflt, const char **ns, const char **local, const char *at) {
  const char *c = memchr(q, ':', ql);
  size_t pl = c ? (size_t)(c - q) : 0;
  const char *l = c ? c + 1 : q;
  char pfx[256];
  if (l == q + ql || (c && !pl) || pl >= sizeof pfx || memchr(l, ':', ql - (size_t)(l - q))) return fail(ps, at);
  memcpy(pfx, q, pl);
  pfx[pl] = '\0';
  *ns = pl || dflt ? bim_node_ns_uri(n, pfx) : NULL;
  if (pl && !*ns) return fail(ps, at);
  *local = intern(ps->doc, l, ql - (size_t)(l - q));
  return *local ? 0 : oom(ps);
}

static int add_attrs(parser_t *ps, bim_node_t *n, int nattrs, const char *at) {
  for (int i = 0; i < nattrs; i++) {
    const raw_attr_t *a = &ps->attrs[i];
    const char *ns;
    const char *local;
    gbuf_t v = {NULL, 0, 0};
    int rc;
    if (is_xmlns(a)) continue;
    if (resolve(ps, n, a->name, a->nlen, 0, &ns, &local, at)) return -1;
    if (decode_into(ps, &v, a->val, a->vlen, 1)) {
      free(v.buf);
      return -1;
    }
    v.buf[v.len] = '\0';
    rc = bim_node_add_attr_ns(n, ns, local, v.buf);
    if (rc == -2) return fail(ps, at);
    if (rc) return oom(ps);
  }
  return 0;
}

/* lt at '<' of a start tag. 1 open element, 0 empty element, -1 error */
static int start_tag(parser_t *ps, const char *lt, const char **next) {
  const char *q = lt + 1;
  const char *p = q;
  size_t ql;
  int nattrs = 0;
  int empty = 0;
  bim_node_t *n;
  while (p < ps->end && is_name_char(*p)) p++;
  ql = (size_t)(p - q);
  if (!ql) return fail(ps, lt);
  for (;;) {
    raw_attr_t a;
    const char *ws = p;
    char quote;
    p = skip_space(p, ps->end);
    if (p >= ps->end) return fail(ps, lt);
    if (*p == '>') break;
    if (*p == '/') {
      if (p + 1 >= ps->end || p[1] != '>') return fail(ps, p);
      empty = 1;
      p++;
      break;
    }
    if (p == ws) return fail(ps, p);
    a.name = p;
    while (p < ps->end && is_name_char(*p)) p++;
    a.nlen = (size_t)(p - a.name);
    p = skip_space(p, ps->end);
    if (!a.nlen || p >= ps->end || *p != '=') return fail(ps, p);
    p = skip_space(p + 1, ps->end);
    if (p >= ps->end || (*p != '"' && *p != '\'')) return fail(ps, p);
    quote = *p++;
    a.val = p;
    while (p < ps->end && *p != quote && *p != '<') p++;
    if (p >= ps->end || *p == '<') return fail(ps, p);
    a.vlen = (size_t)(p - a.val);
    p++;
    if (add_raw_attr(ps, nattrs++, &a)) return -1;
  }
  *next = p + 1;
  if (!ps->cur && ps->doc->root) return fail(ps, lt);
  if (ps->cur && flush_text(ps, 0)) return -1;
  n = bim_node_new(NULL);
  if (!n) return oom(ps);
  if (!ps->cur) {
    ps->doc->root = n;
  } else {
    if (bim_node_add(ps->cur, n)) {
      bim_node_free(n);
      return oom(ps);
    }
    ps->has_kids[ps->depth - 1] = 1;
  }
  if (add_nsdecls(ps, n, nattrs, lt) || resolve(ps, n, q, ql, 1, &n->ns, &n->name, lt) || add_attrs(ps, n, nattrs, lt)) return -1;
  if (empty) return 0;
  if (ps->depth >= XML_DEPTH_MAX) return fail(ps, lt);
  ps->qname[ps->depth] = q;
  ps->qlen[ps->depth] = ql;
  ps->has_kids[ps->depth] = 0;
  ps->depth++;
  ps->cur = n;
  return 1;
}

static int close_tag(parser_t *ps, const char *lt, const char **next) {
  const char *q = lt + 2;
  const char *p = q;
  size_t ql;
  while (p < ps->end && is_name_char(*p)) p++;
  ql = (size_t)(p - q);
  p = skip_space(p, ps->end);
  if (!ps->cur || p >= ps->end || *p != '>' || ql != ps->qlen[ps->depth - 1] || memcmp(q, ps->qname[ps->depth - 1], ql)) return fail(ps, lt);
  if (flush_text(ps, 1)) return -1;
  ps->depth--;
  ps->cur = ps->depth ? ps->cur->parent : NULL;
  *next = p + 1;
  return 0;
}

/* comment, PI or DOCTYPE at lt, terminator checked. DOCTYPE in prolog only */
static int skip_markup(parser_t *ps, const char *lt, const char **next) {
  const char *q = skip_misc(lt, ps->end);
  size_t n = (size_t)(q - lt);
  int comment = ps->end - lt >= 4 && !memcmp(lt, "<!--", 4);
  if (comment ? n < 7 || memcmp(q - 3, "-->", 3) : lt[1] == '?' ? n < 4 || memcmp(q - 2, "?>", 2) : q[-1] != '>') return fail(ps, lt);
  if (!comment && lt[1] == '!' && (ps->cur || ps->doc->root)) return fail(ps, lt);
  *next = q;
  return 0;
}

static int cdata(parser_t *ps, const char *lt, const char **next) {
  const char *s = lt + 9;
  const char *c = memmem(s, (size_t)(ps->end - s), "]]>", 3);
  if (!ps->cur || !c) return fail(ps, lt);
  if (gbuf_reserve(&ps->text, (size_t)(c - s))) return oom(ps);
  ps->text.len += norm_copy(ps->text.buf + ps->text.len, s, (size_t)(c - s), 0);
  *next = c + 3;
  return 0;
}

static int parse(parser_t *ps, const char *p) {
  while (p < ps->end) {
    const char *lt = memchr(p, '<', (size_t)(ps->end - p));
    const char *seg = lt ? lt : ps->end;
    int rc;
    if (seg > p) {
      if (ps->cur) {
        if (decode_into(ps, &ps->text, p, (size_t)(seg - p), 0)) return -1;
      } else if (!only_space(p, (size_t)(seg - p))) {
        return fail(ps, p);
      }
    }
    if (!lt) break;
    if (lt + 1 >= ps->end) return fail(ps, lt);
    if (ps->end - lt >= 9 && !memcmp(lt, "<![CDATA[", 9)) rc = cdata(ps, lt, &p);
    else if (lt[1] == '!' || lt[1] == '?') rc = skip_markup(ps, lt, &p);
    else if (lt[1] == '/') rc = close_tag(ps, lt, &p);
    else rc = start_tag(ps, lt, &p) < 0 ? -1 : 0;
    if (rc) return -1;
  }
  return ps->cur || !ps->doc->root ? fail(ps, ps->end) : 0;
}

int xml_doc_parse(const char *buf, size_t len, xml_doc_t **doc, size_t *err_off) {
  parser_t ps;
  int rc;
  memset(&ps, 0, sizeof ps);
  ps.doc = calloc(1, sizeof *ps.doc);
  if (!ps.doc) return -2;
  ps.end = buf + len;
  if (len >= 3 && !memcmp(buf, "\xEF\xBB\xBF", 3)) buf += 3;
  rc = parse(&ps, buf);
  free(ps.text.buf);
  free(ps.scratch.buf);
  free(ps.attrs);
  if (rc) {
    if (err_off) *err_off = ps.err ? (size_t)(ps.err - (ps.end - len)) : 0;
    xml_doc_free(ps.doc);
    return ps.oom ? -2 : -1;
  }
  *doc = ps.doc;
  return 0;
}

bim_node_t *xml_doc_root(const xml_doc_t *doc) { return doc->root; }

void xml_doc_free(xml_doc_t *doc) {
  if (!doc) return;
  bim_node_free(doc->root);
  while (doc->chunks) {
    pool_chunk_t *c = doc->chunks;
    doc->chunks = c->next;
    free(c);
  }
  free(doc->slots);
  free(doc);
}
