/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/bim/index.h"
#include "lib/bim/strrepo.h"
#include "lib/helper/beutil.h"
#include "lib/helper/growutil.h"
#include "lib/tva/tva_xml.h"

#define LEAF_HDR 0xBF    /* leaf_field 1, multiple_locators 0, reserved */
#define NODE_HDR 0x3F    /* leaf_field 0 */
#define INDEX_HDR 0x3F   /* no overlap, single_layer_sub_index 0 */
#define LOCATOR_REMOTE 0x01
#define XPATH_FIELD 0xFFFF
#define LOCATOR_LEN 5

/* TS 102 323 table 76 */
enum { FID_GROUPID = 0x0000, FID_TITLE = 0x0001, FID_PROGRAMID = 0x0002, FID_END = 0x0003, FID_SERVICE = 0x0004, FID_EVENT_TITLE = 0x0005 };

typedef struct {
  const char *name;
  unsigned frag_type;
  int nfields;
  int time_key; /* key[0] dateTime ordered */
  const char *xpath[2];
  unsigned fid[2];
} kind_t;

/* xpath strings, tables 87, 91, 95, 100. schedule fragment_type 0x0005 per TS 102 822-3-2 table 7, table 100: 0x0002 (GroupInformation) */
static const kind_t kinds[BIM_INDEX_KINDS] = {
  {"pi-crid", 0x0001, 1, 0, {"@tva:programId", NULL}, {FID_PROGRAMID, 0}},
  {"pi-title", 0x0001, 1, 0, {"tva:BasicDescription/tva:Title.text()", NULL}, {FID_TITLE, 0}},
  {"schedule-time", 0x0005, 2, 1, {"@tva:end", "@tva:serviceIdRef"}, {FID_END, FID_SERVICE}},
  {"schedule-title", 0x0005, 1, 0, {"tva:ScheduleEvent/tva:InstanceDescription/tva:Title.text()", NULL}, {FID_EVENT_TITLE, 0}},
  {"gi-crid", 0x0002, 1, 0, {"@tva:groupId", NULL}, {FID_GROUPID, 0}},
  {"gi-title", 0x0002, 1, 0, {"tva:BasicDescription/tva:Title.text()", NULL}, {FID_TITLE, 0}},
};

int bim_index_parse_names(const char *list, unsigned *mask) {
  const char *p = list;
  *mask = 0;
  for (;;) {
    size_t n = strcspn(p, ",");
    int k;
    for (k = 0; k < BIM_INDEX_KINDS; k++)
      if (strlen(kinds[k].name) == n && !strncmp(kinds[k].name, p, n)) break;
    if (k == BIM_INDEX_KINDS) return -1;
    *mask |= 1u << k;
    if (!p[n]) return 0;
    p += n + 1;
  }
}

int bim_index_add(bim_index_input_t *in, const char *key0, const char *key1, uint16_t container, uint32_t fragment) {
  if (array_reserve((void **)&in->e, &in->cap, in->n + 1, sizeof *in->e)) return -1;
  in->e[in->n++] = (bim_index_entry_t){{key0, key1}, container, fragment, 0, 0, 0};
  return 0;
}

void bim_index_input_free(bim_index_input_t *in) {
  free(in->e);
  memset(in, 0, sizeof *in);
}

static int key_cmp(int time_key, const char *a, const char *b) {
  long long x;
  long long y;
  if (time_key && !iso8601_utc_secs(a, &x) && !iso8601_utc_secs(b, &y) && x != y) return (x > y) - (x < y);
  return strcmp(a, b);
}

static int tuple_cmp(const kind_t *k, const char *const *a, const char *const *b) {
  int c = key_cmp(k->time_key, a[0], b[0]);
  return c || k->nfields < 2 ? c : strcmp(a[1], b[1]);
}

static int ekey_cmp(const kind_t *k, const bim_index_entry_t *a, const bim_index_entry_t *b) {
  if (k->time_key && a->t_ok && b->t_ok && a->t != b->t) return (a->t > b->t) - (a->t < b->t);
  return strcmp(a->key[0], b->key[0]);
}

static int entry_cmp(const void *pa, const void *pb) {
  const bim_index_entry_t *a = pa;
  const bim_index_entry_t *b = pb;
  const kind_t *k = &kinds[a->kind];
  int c = ekey_cmp(k, a, b);
  if (!c && k->nfields > 1) c = strcmp(a->key[1], b->key[1]);
  if (c) return c;
  if (a->container != b->container) return (a->container > b->container) - (a->container < b->container);
  return (a->fragment > b->fragment) - (a->fragment < b->fragment);
}

typedef struct {
  unsigned char *buf;
  size_t len;
  size_t cap;
} bytes_t;

static int put(bytes_t *b, const void *p, size_t n) {
  if (growbuf_reserve((void **)&b->buf, &b->cap, 1, b->len + n, 256)) return -1;
  memcpy(b->buf + b->len, p, n);
  b->len += n;
  return 0;
}

static int put8(bytes_t *b, unsigned v) {
  unsigned char c = (unsigned char)v;
  return put(b, &c, 1);
}

static int put16(bytes_t *b, unsigned v) {
  unsigned char c[2];
  be16_put(c, (uint16_t)v);
  return put(b, c, 2);
}

static int put_locator(bytes_t *b, const bim_index_entry_t *e) {
  unsigned char c[LOCATOR_LEN];
  be16_put(c, e->container);
  be24_put(c + 2, e->fragment);
  return put(b, c, sizeof c);
}

typedef struct {
  const char *s;
  long off;
} sent_t;

typedef struct {
  sent_t *v;
  size_t cap;
  size_t n;
} sset_t;

static size_t str_hash(const char *s) {
  uint64_t h = 1469598103934665603ULL;
  for (; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ULL;
  return (size_t)h;
}

static sent_t *sset_slot(const sset_t *t, const char *s) {
  size_t i = str_hash(s) & (t->cap - 1);
  while (t->v[i].s && strcmp(t->v[i].s, s)) i = (i + 1) & (t->cap - 1);
  return &t->v[i];
}

static sent_t *sset_get(sset_t *t, const char *s, int *fresh) {
  sent_t *e;
  if ((t->n + 1) * 2 > t->cap) {
    sset_t g = {NULL, t->cap ? t->cap * 2 : 64, 0};
    if (!(g.v = calloc(g.cap, sizeof *g.v))) return NULL;
    for (size_t i = 0; i < t->cap; i++)
      if (t->v[i].s) *sset_slot(&g, t->v[i].s) = t->v[i];
    g.n = t->n;
    free(t->v);
    *t = g;
  }
  e = sset_slot(t, s);
  *fresh = !e->s;
  if (*fresh) {
    *e = (sent_t){s, -1};
    t->n++;
  }
  return e;
}

/* string offset into sw, -1 oom or beyond 16 bit */
static long put_str(strrepo_writer_t *sw, sset_t *seen, const char *s) {
  int fresh;
  sent_t *e = sset_get(seen, s, &fresh);
  if (!e) return -1;
  if (fresh) {
    size_t off = sw->len;
    e->off = off + strlen(s) + 1 > BIM_REPO_MAX || strrepo_writer_put(sw, s) ? -1 : (long)off;
  }
  return e->off;
}

static int add_struct(bim_index_cont_t *c, unsigned type, unsigned id, bytes_t *b) {
  bim_struct_t *np = realloc(c->structs, sizeof *np * (size_t)(c->nstructs + 1));
  if (!np) return -1;
  c->structs = np;
  np[c->nstructs++] = (bim_struct_t){(uint8_t)type, (uint8_t)id, b->buf, b->len, b->buf};
  memset(b, 0, sizeof *b);
  return 0;
}

static int add_strings(bim_index_cont_t *c, const strrepo_writer_t *sw) {
  size_t n;
  const unsigned char *d = strrepo_writer_data(sw, &n);
  bytes_t b = {NULL, 0, 0};
  if (put(&b, d, n) || add_struct(c, BIM_STRUCT_DATA_REPOSITORY, BIM_DATAREPO_STRINGS, &b)) {
    free(b.buf);
    return -1;
  }
  return 0;
}


/* 1 sub-index container for e[0..n), single layer or 2 layers (time) */
static int build_sub(const kind_t *k, const bim_index_entry_t *e, int n, bim_index_cont_t *c) {
  strrepo_writer_t sw;
  sset_t seen = {NULL, 0, 0};
  bytes_t l1 = {NULL, 0, 0};
  bytes_t l2 = {NULL, 0, 0};
  int rc = -1;
  if (strrepo_writer_init(&sw)) return -1;
  if (k->nfields == 1) {
    if (put8(&l2, LEAF_HDR)) goto done;
    for (int i = 0; i < n; i++) {
      long off = put_str(&sw, &seen, e[i].key[0]);
      if (off < 0 || put16(&l2, (unsigned)off) || put_locator(&l2, &e[i])) goto done;
    }
    if (add_struct(c, BIM_STRUCT_SUB_INDEX, 0, &l2)) goto done;
  } else {
    if (put8(&l1, NODE_HDR) || put8(&l1, 1) || put8(&l2, LEAF_HDR)) goto done;
    for (int i = 0; i < n;) {
      int j = i;
      long off = put_str(&sw, &seen, e[i].key[0]);
      if (off < 0) goto done;
      while (j < n && !ekey_cmp(k, &e[j], &e[i])) {
        long so = put_str(&sw, &seen, e[j].key[1]);
        if (so < 0 || put16(&l2, (unsigned)so) || put_locator(&l2, &e[j])) goto done;
        j++;
      }
      if (put16(&l1, (unsigned)off) || put16(&l1, (unsigned)(j - 1))) goto done;
      i = j;
    }
    if (add_struct(c, BIM_STRUCT_SUB_INDEX, 0, &l1) || add_struct(c, BIM_STRUCT_SUB_INDEX, 1, &l2)) goto done;
  }
  rc = add_strings(c, &sw);
done:
  free(l1.buf);
  free(l2.buf);
  free(seen.v);
  strrepo_writer_free(&sw);
  return rc;
}

static int new_cont(bim_index_cont_t **conts, int *n, unsigned id) {
  bim_index_cont_t *np = realloc(*conts, sizeof *np * (size_t)(*n + 1));
  if (!np) return -1;
  *conts = np;
  np[*n] = (bim_index_cont_t){(uint16_t)id, NULL, 0};
  (*n)++;
  return 0;
}

/* entries of 1 chunk: strings below BIM_REPO_MAX, layer offsets 16 bit */
static int chunk_len(const kind_t *k, const bim_index_entry_t *e, int n) {
  sset_t seen = {NULL, 0, 0};
  size_t strs = 1;
  int i;
  for (i = 0; i < n && i < 0xFFFF; i++) {
    size_t add = 0;
    for (int f = 0; f < k->nfields; f++) {
      int fresh;
      if (!sset_get(&seen, e[i].key[f], &fresh)) {
        free(seen.v);
        return -1;
      }
      if (fresh) add += strlen(e[i].key[f]) + 1;
    }
    if (strs + add > BIM_REPO_MAX) break;
    strs += add;
  }
  free(seen.v);
  return i;
}

static int list_entry(bytes_t *b, strrepo_writer_t *sw, sset_t *seen, const kind_t *k, unsigned id) {
  if (put8(b, k->nfields == 1 ? 0x0C : 0x12) || put16(b, k->frag_type) || put8(b, (unsigned)k->nfields)) return -1;
  for (int f = 0; f < k->nfields; f++) {
    long off = put_str(sw, seen, k->xpath[f]);
    if (off < 0 || put16(b, XPATH_FIELD) || put16(b, (unsigned)off) || put16(b, 0x0000)) return -1;
  }
  return put16(b, BIM_INDEX_LIST_CONTAINER) || put8(b, id);
}

int bim_index_build(unsigned mask, bim_index_input_t in[BIM_INDEX_KINDS], uint16_t first_id, bim_index_cont_t **conts, int *nconts) {
  strrepo_writer_t sw;
  sset_t seen = {NULL, 0, 0};
  bytes_t list = {NULL, 0, 0};
  bytes_t ix = {NULL, 0, 0};
  unsigned next_id = first_id;
  int rc = -1;
  *conts = NULL;
  *nconts = 0;
  if (strrepo_writer_init(&sw)) return -1;
  if (new_cont(conts, nconts, BIM_INDEX_LIST_CONTAINER)) goto done;
  for (int kind = 0; kind < BIM_INDEX_KINDS; kind++) {
    const kind_t *k = &kinds[kind];
    bim_index_entry_t *e = in[kind].e;
    int n = in[kind].n;
    if (!(mask & (1u << kind))) continue;
    for (int i = 0; i < n; i++) {
      e[i].kind = kind;
      e[i].t_ok = k->time_key && !iso8601_utc_secs(e[i].key[0], &e[i].t);
    }
    if (n > 1) qsort(e, (size_t)n, sizeof *e, entry_cmp);
    if (list_entry(&list, &sw, &seen, k, (unsigned)kind) || put8(&ix, INDEX_HDR) || put8(&ix, LOCATOR_REMOTE)) {
      rc = sw.len + 128 > BIM_REPO_MAX ? -2 : -1;
      goto done;
    }
    for (int i = 0; i < n;) {
      int m = chunk_len(k, e + i, n - i);
      if (m <= 0) goto done;
      if (k->nfields > 1)
        while (i + m < n && m > 1 && !ekey_cmp(k, &e[i + m], &e[i + m - 1])) m--;
      if (next_id > 0xFFFF) {
        rc = -3;
        goto done;
      }
      if (new_cont(conts, nconts, next_id) || build_sub(k, e + i, m, &(*conts)[*nconts - 1])) goto done;
      for (int f = 0; f < k->nfields; f++) {
        long off = put_str(&sw, &seen, e[i + m - 1].key[f]);
        if (off < 0) {
          rc = sw.len + strlen(e[i + m - 1].key[f]) + 1 > BIM_REPO_MAX ? -2 : -1;
          goto done;
        }
        if (put16(&ix, (unsigned)off)) goto done;
      }
      if (put16(&ix, next_id) || put8(&ix, 0)) goto done;
      next_id++;
      i += m;
    }
    if (add_struct(&(*conts)[0], BIM_STRUCT_INDEX, (unsigned)kind, &ix)) goto done;
  }
  if (add_struct(&(*conts)[0], BIM_STRUCT_INDEX_LIST, BIM_STRUCT_ID_UNUSED, &list) || add_strings(&(*conts)[0], &sw)) goto done;
  rc = 0;
done:
  free(list.buf);
  free(ix.buf);
  free(seen.v);
  strrepo_writer_free(&sw);
  if (rc) {
    bim_index_conts_free(*conts, *nconts);
    *conts = NULL;
    *nconts = 0;
  }
  return rc;
}

void bim_index_conts_free(bim_index_cont_t *conts, int n) {
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < conts[i].nstructs; j++) free(conts[i].structs[j].heap_body);
    free(conts[i].structs);
  }
  free(conts);
}

typedef struct {
  bim_index_find_fn find;
  bim_index_has_fn has;
  const void *ctx;
  bim_index_report_t *rep;
} check_t;

/* NUL-terminated string at off of repository, NULL if none */
static int strings_of(const check_t *c, unsigned cont, const unsigned char **s, size_t *len) {
  return c->find(c->ctx, cont, BIM_STRUCT_DATA_REPOSITORY, BIM_DATAREPO_STRINGS, s, len);
}

/* profiled kind of one index list entry, -1 other */
static int match_kind(unsigned ftype, int nf, const unsigned *fid, const char *const *xp, const unsigned *enc) {
  for (int k = 0; k < BIM_INDEX_KINDS; k++) {
    const kind_t *d = &kinds[k];
    int ok = nf == d->nfields && (ftype == d->frag_type || (k == BIM_INDEX_SCHED_TITLE && ftype == 0x0002));
    for (int f = 0; ok && f < nf; f++)
      ok = enc[f] == 0x0000 && (fid[f] == XPATH_FIELD ? xp[f] && !strcasecmp(xp[f], d->xpath[f]) : fid[f] == d->fid[f]);
    if (ok) return k;
  }
  return -1;
}

static void check_locator(const check_t *c, const unsigned char *p) {
  c->rep->entries++;
  if (!c->has(c->ctx, be16_get(p), be24_get(p + 2))) c->rep->unresolved++;
}

/* single layer leaf entries in (lo, hi] */
static void check_single(const check_t *c, unsigned cont, unsigned id, const char *lo, const char *hi) {
  const unsigned char *b;
  const unsigned char *s;
  size_t len;
  size_t slen;
  const char *prev = lo;
  if (c->find(c->ctx, cont, BIM_STRUCT_SUB_INDEX, id, &b, &len) || strings_of(c, cont, &s, &slen)) {
    c->rep->missing++;
    return;
  }
  if (len < 1 || b[0] >> 6 != 2 || (len - 1) % (2 + LOCATOR_LEN)) {
    c->rep->malformed++;
    return;
  }
  for (size_t p = 1; p < len; p += 2 + LOCATOR_LEN) {
    const char *v = strrepo_at(s, slen, be16_get(b + p));
    if (!v) {
      c->rep->malformed++;
      return;
    }
    if ((prev && strcmp(prev, v) > (prev == lo ? -1 : 0)) || strcmp(v, hi) > 0) c->rep->misordered++;
    prev = v;
    check_locator(c, b + p + 2);
  }
}

/* two layers, tuples in (lo, hi] */
static void check_two(const check_t *c, const kind_t *k, unsigned cont, unsigned id, const char *const *lo, const char *const *hi) {
  const unsigned char *b1;
  const unsigned char *b2;
  const unsigned char *s;
  size_t l1;
  size_t l2;
  size_t slen;
  size_t start = 0;
  const char *prev[2] = {lo[0], lo[1]};
  int have_prev = lo[0] != NULL;
  if (c->find(c->ctx, cont, BIM_STRUCT_SUB_INDEX, id, &b1, &l1) || strings_of(c, cont, &s, &slen)) {
    c->rep->missing++;
    return;
  }
  if (l1 < 2 || b1[0] >> 6 != 0 || (l1 - 2) % 4) {
    c->rep->malformed++;
    return;
  }
  if (c->find(c->ctx, cont, BIM_STRUCT_SUB_INDEX, b1[1], &b2, &l2)) {
    c->rep->missing++;
    return;
  }
  if (l2 < 1 || b2[0] >> 6 != 2 || (l2 - 1) % (2 + LOCATOR_LEN)) {
    c->rep->malformed++;
    return;
  }
  for (size_t p = 2; p < l1; p += 4) {
    const char *end = strrepo_at(s, slen, be16_get(b1 + p));
    size_t last = be16_get(b1 + p + 2);
    if (!end || last < start || 1 + (last + 1) * (2 + LOCATOR_LEN) > l2) {
      c->rep->malformed++;
      return;
    }
    for (; start <= last; start++) {
      const unsigned char *q = b2 + 1 + start * (2 + LOCATOR_LEN);
      const char *t[2] = {end, strrepo_at(s, slen, be16_get(q))};
      if (!t[1]) {
        c->rep->malformed++;
        return;
      }
      if ((have_prev && tuple_cmp(k, prev, t) > 0) || tuple_cmp(k, t, hi) > 0) c->rep->misordered++;
      prev[0] = t[0];
      prev[1] = t[1];
      have_prev = 1;
      check_locator(c, q + 2);
    }
  }
  if (1 + start * (2 + LOCATOR_LEN) != l2) c->rep->malformed++;
}

static void check_index(const check_t *c, int kind, unsigned cont, unsigned id) {
  const kind_t *k = &kinds[kind];
  const unsigned char *b;
  const unsigned char *s;
  size_t len;
  size_t slen;
  size_t step;
  const char *lo[2] = {NULL, NULL};
  if (c->find(c->ctx, cont, BIM_STRUCT_INDEX, id, &b, &len) || strings_of(c, cont, &s, &slen)) {
    c->rep->missing++;
    return;
  }
  step = (b && len >= 2 && (b[0] & 0x80) ? 4u : 2u) * (size_t)k->nfields + 3;
  if (len < 2 || b[1] != LOCATOR_REMOTE || (len - 2) % step) {
    c->rep->malformed++;
    return;
  }
  for (size_t p = 2; p < len; p += step) {
    const unsigned char *h = b + p + ((b[0] & 0x80) ? 2u * (size_t)k->nfields : 0);
    const char *hi[2] = {strrepo_at(s, slen, be16_get(h)), k->nfields > 1 ? strrepo_at(s, slen, be16_get(h + 2)) : ""};
    unsigned sc = be16_get(h + 2 * k->nfields);
    unsigned sid = h[2 * k->nfields + 2];
    if (!hi[0] || !hi[1]) {
      c->rep->malformed++;
      return;
    }
    if (lo[0] && !(b[0] & 0x80) && tuple_cmp(k, lo, hi) >= 0) c->rep->misordered++;
    if (k->nfields == 1) check_single(c, sc, sid, lo[0], hi[0]);
    else check_two(c, k, sc, sid, lo, hi);
    lo[0] = hi[0];
    lo[1] = hi[1];
  }
}

int bim_index_check(bim_index_find_fn find, bim_index_has_fn has, const void *ctx, bim_index_report_t *rep) {
  check_t c = {find, has, ctx, rep};
  const unsigned char *b;
  const unsigned char *s;
  size_t len;
  size_t slen = 0;
  memset(rep, 0, sizeof *rep);
  if (find(ctx, BIM_INDEX_LIST_CONTAINER, BIM_STRUCT_INDEX_LIST, BIM_STRUCT_ID_UNUSED, &b, &len)) return -1;
  if (strings_of(&c, BIM_INDEX_LIST_CONTAINER, &s, &slen)) s = NULL;
  for (size_t p = 0; p < len;) {
    size_t dl = b[p];
    size_t q = p + 1;
    size_t end = q + dl;
    unsigned ftype;
    unsigned fid[2] = {0, 0};
    unsigned enc[2] = {0, 0};
    const char *xp[2] = {NULL, NULL};
    int nf;
    int kind;
    if (end > len || dl < 3) {
      rep->malformed++;
      return 0;
    }
    ftype = be16_get(b + q);
    q += 2 + (ftype == XPATH_FIELD ? 2 : 0);
    nf = q < end ? b[q++] : 0;
    for (int f = 0; f < nf && q + 4 <= end; f++) {
      unsigned v = be16_get(b + q);
      q += 2;
      if (v == XPATH_FIELD) {
        if (q + 2 > end) break;
        if (f < 2) xp[f] = strrepo_at(s, slen, be16_get(b + q));
        q += 2;
      }
      if (q + 2 > end) break;
      if (f < 2) {
        fid[f] = v;
        enc[f] = be16_get(b + q);
      }
      q += 2;
    }
    kind = q + 3 == end ? match_kind(ftype, nf, fid, xp, enc) : -1;
    if (kind < 0) {
      rep->ignored++;
    } else {
      rep->indexes++;
      check_index(&c, kind, be16_get(b + q), b[q + 2]);
    }
    p = end;
  }
  return 0;
}
