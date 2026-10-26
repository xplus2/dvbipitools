/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/tvafrag.h"

#define STEPS_MAX 16

typedef struct {
  unsigned code;
  const char *name; /* root element and type of table 58 */
  const char *type;
  const char *path; /* below TVAMain, prefix:name segments, table 7. NULL: found by name and type */
} row_t;

static const row_t rows[] = {
  {0x0001, "ProgramInformation", "ProgramInformationType", NULL},
  {0x0002, "GroupInformation", "GroupInformationType", NULL},
  {0x0003, "OnDemandProgram", "OnDemandProgramType", NULL},
  {0x0004, "BroadcastEvent", "BroadcastEventType", NULL},
  {0x0005, "Schedule", "ScheduleType", NULL},
  {0x0006, "ServiceInformation", "ServiceInformationType", NULL},
  {0x0007, NULL, NULL, "tva:ProgramDescription/tva:CreditsInformationTable/tva:PersonName"},
  {0x0008, NULL, NULL, "tva:ProgramDescription/tva:CreditsInformationTable/tva:OrganizationName"},
  {0x0009, "Review", "ReviewType", NULL},
  {0x000A, "CSAlias", "CSAliasType", NULL},
  {0x000B, "ClassificationScheme", "ClassificationSchemeType", NULL},
  {0x000C, "SegmentInformation", "SegmentInformationType", NULL},
  {0x000D, "SegmentGroupInformation", "SegmentGroupInformationType", NULL},
  {0x000E, "TVAMain", "TVAMainType", NULL},
  {0x000F, "OnDemandService", "OnDemandServiceType", NULL},
  {0x0010, "PurchaseInformation", "PurchaseInformationType", NULL},
  {0x0011, NULL, NULL, "tva:MetadataOriginationInformationTable/tva:MetadataOriginationInformation"},
  {0x0012, NULL, NULL, "tva2:PackageTable/tva2:Package"},
  {0x0013, NULL, NULL, "tva2:InterstitialCampaignTable/tva2:InterstitialCampaign"},
  {0x0014, NULL, NULL, "tva2:RMPITable/tva2:RMPIDescription"},
  {0x0015, NULL, NULL, "tva2:CouponTable/tva2:CouponDescription"},
  {0x0016, NULL, NULL, "tva2:TargetingInformationTable/tva2:TargetingInformation"},
  {0x0017, NULL, NULL, "tva2:InterstitialTargetingTable/int:RulesTable/int:Rule"},
  {0x0018, NULL, NULL, "tva2:InterstitialTargetingTable/int:RecordingCacheTable/int:Request/int:Item"},
  {0x0019, NULL, NULL, "tva2:InterstitialTargetingTable/int:RecordingCacheTable/int:Replace/int:Item"},
  {0x0020, NULL, NULL, "tva2:InterstitialTargetingTable/int:RecordingCacheTable/int:Expire/int:Item"},
  {0x0021, NULL, NULL, "tva2:InterstitialTargetingTable/int:InterstitialTable/int:InterstitialBreak"},
  {0x0022, "PushDownloadProgram", "PushDownloadType", NULL},
};

static const char *find_ns(const xsd_compiled_t *c, const char *prefix) {
  const bim_schema_t *s = &c->schema;
  for (int i = 0; i < s->ntypes; i++)
    if (s->types[i]->ns && !strncmp(s->types[i]->ns, prefix, strlen(prefix))) return s->types[i]->ns;
  for (int i = 0; i < s->nelems; i++)
    if (s->elems[i]->ns && !strncmp(s->elems[i]->ns, prefix, strlen(prefix))) return s->elems[i]->ns;
  return NULL;
}

void bim_tvans_detect(const xsd_compiled_t *c, bim_tvans_t *ns) {
  ns->tva2 = find_ns(c, "urn:tva:metadata:extended:");
  ns->intr = find_ns(c, "urn:tva:metadata:int:");
}

static int same(const char *a, const char *b) { return (!a || !*a) && (!b || !*b) ? 1 : a && b && !strcmp(a, b); }

static const char *prefix_ns(const bim_tvans_t *ns, const char *seg, size_t n) {
  if (n == 3 && !strncmp(seg, "tva", 3)) return ns->tva;
  if (n == 4 && !strncmp(seg, "tva2", 4)) return ns->tva2;
  if (n == 3 && !strncmp(seg, "int", 3)) return ns->intr;
  return NULL;
}

static const bim_sbc_t *child(const bim_type_t *t, const char *ns, const char *name, size_t n) {
  for (int i = 0; t && i < t->nsbc; i++)
    if (t->sbc[i].kind == BIM_SBC_ELEM && strlen(t->sbc[i].name) == n && !strncmp(t->sbc[i].name, name, n) && same(t->sbc[i].ns, ns)) return &t->sbc[i];
  return NULL;
}

static int walk(const bim_tvans_t *ns, const bim_type_t *top, const char *path, bim_tvaroot_t *steps, int cap) {
  const bim_type_t *t = top;
  int n = 1;
  for (const char *p = path; t && *p;) {
    const char *colon = strchr(p, ':');
    const char *end = strchr(p, '/');
    const char *sns;
    const bim_sbc_t *s;
    if (!end) end = p + strlen(p);
    if (!colon || colon > end || !(sns = prefix_ns(ns, p, (size_t)(colon - p))) || !(s = child(t, sns, colon + 1, (size_t)(end - colon - 1))) || n >= cap) return -1;
    steps[n++] = (bim_tvaroot_t){s->name, s->ns, s->type};
    t = s->type;
    p = *end ? end + 1 : end;
  }
  return t ? n : -1;
}

typedef struct {
  const bim_type_t **v;
  size_t cap;
  size_t n;
} tset_t;

/* 1 added, 0 present, -1 oom */
static int tset_add(tset_t *s, const bim_type_t *t) {
  size_t i;
  if ((s->n + 1) * 2 > s->cap) {
    tset_t g = {NULL, s->cap ? s->cap * 2 : 256, 0};
    if (!(g.v = calloc(g.cap, sizeof *g.v))) return -1;
    for (size_t k = 0; k < s->cap; k++)
      if (s->v[k]) tset_add(&g, s->v[k]);
    free(s->v);
    *s = g;
  }
  for (i = ((uintptr_t)t >> 4) * 0x9E3779B97F4A7C15ull & (s->cap - 1); s->v[i]; i = (i + 1) & (s->cap - 1))
    if (s->v[i] == t) return 0;
  s->v[i] = t;
  s->n++;
  return 1;
}

typedef struct {
  const bim_type_t *t;
  const bim_sbc_t *s; /* element reaching t, NULL top */
  int parent;
  int depth; /* steps up to t */
} hop_t;

/* breadth first below top: shortest path to element name of type want, table before nested use */
static int search(const bim_type_t *top, const char *tvans, const row_t *r, const bim_type_t *want, bim_tvaroot_t *steps, int cap) {
  hop_t *q = malloc(sizeof *q * 64);
  size_t nq = 0;
  size_t qcap = 64;
  tset_t seen = {NULL, 0, 0};
  int rc = -1;
  if (!q || tset_add(&seen, top) < 0) goto out;
  q[nq++] = (hop_t){top, NULL, -1, 1};
  for (size_t h = 0; h < nq; h++) {
    const bim_type_t *t = q[h].t;
    if (q[h].depth >= cap) continue;
    for (int i = 0; i < t->nsbc; i++) {
      const bim_sbc_t *s = &t->sbc[i];
      int fresh;
      if (s->kind != BIM_SBC_ELEM || !s->type) continue;
      if (!strcmp(s->name, r->name) && same(s->ns, tvans) && s->type == want) {
        steps[q[h].depth] = (bim_tvaroot_t){s->name, s->ns, s->type};
        for (size_t k = h; q[k].s; k = (size_t)q[k].parent) steps[q[k].depth - 1] = (bim_tvaroot_t){q[k].s->name, q[k].s->ns, q[k].s->type};
        rc = q[h].depth + 1;
        goto out;
      }
      if ((fresh = tset_add(&seen, s->type)) < 0) goto out;
      if (!fresh) continue;
      if (nq == qcap) {
        hop_t *g = realloc(q, sizeof *q * qcap * 2);
        if (!g) goto out;
        q = g;
        qcap *= 2;
      }
      q[nq++] = (hop_t){s->type, s, (int)h, q[h].depth + 1};
    }
  }
out:
  free(q);
  free(seen.v);
  return rc;
}

const char *bim_tvafrag_name(unsigned code) {
  for (size_t i = 0; i < sizeof rows / sizeof *rows; i++) {
    const char *slash;
    const char *colon;
    if (rows[i].code != code) continue;
    if (rows[i].name) return rows[i].name;
    slash = strrchr(rows[i].path, '/');
    colon = strchr(slash ? slash : rows[i].path, ':');
    return colon + 1;
  }
  return NULL;
}

int bim_tvafrag_steps(const xsd_compiled_t *c, const bim_tvans_t *ns, unsigned code, bim_tvaroot_t *steps, int cap) {
  const bim_elem_t *main = xsd_compiled_elem(c, ns->tva, "TVAMain");
  if (!main || cap < 1 || cap > STEPS_MAX) return -1;
  steps[0] = (bim_tvaroot_t){main->name, main->ns, main->type};
  for (size_t i = 0; i < sizeof rows / sizeof *rows; i++) {
    const bim_type_t *want;
    if (rows[i].code != code) continue;
    if (rows[i].path) return walk(ns, main->type, rows[i].path, steps, cap);
    want = xsd_compiled_type(c, ns->tva, rows[i].type);
    if (!want) return -1;
    if (code == 0x000E) return main->type == want ? 1 : -1;
    return search(main->type, ns->tva, &rows[i], want, steps, cap);
  }
  return -1;
}

int bim_tvafrag_root(const xsd_compiled_t *c, const bim_tvans_t *ns, unsigned code, bim_tvaroot_t *out) {
  bim_tvaroot_t steps[STEPS_MAX];
  int n = bim_tvafrag_steps(c, ns, code, steps, STEPS_MAX);
  if (n < 1) return -1;
  *out = steps[n - 1];
  return 0;
}

int bim_tvafrag_code(const xsd_compiled_t *c, const bim_tvans_t *ns, const char *name, const char *elem_ns, const bim_type_t *type) {
  for (size_t i = 0; i < sizeof rows / sizeof *rows; i++) {
    bim_tvaroot_t r;
    if (strcmp(bim_tvafrag_name(rows[i].code), name)) continue;
    if (!bim_tvafrag_root(c, ns, rows[i].code, &r) && !strcmp(r.name, name) && same(r.ns, elem_ns) && r.type == type) return (int)rows[i].code;
  }
  return -1;
}

typedef struct {
  const bim_node_t *node;
  unsigned code;
} hit_t;

typedef struct {
  hit_t *v;
  size_t n;
  size_t cap;
} hits_t;

static int hit_cmp(const void *a, const void *b) {
  const char *x = (const char *)((const hit_t *)a)->node;
  const char *y = (const char *)((const hit_t *)b)->node;
  return x < y ? -1 : x > y;
}

static const hit_t *hit_find(const hits_t *h, const bim_node_t *n) {
  hit_t key = {n, 0};
  return h->n ? bsearch(&key, h->v, h->n, sizeof *h->v, hit_cmp) : NULL;
}

static int collect(hits_t *h, const bim_node_t *n, const bim_tvaroot_t *steps, int level, int k, unsigned code) {
  for (int i = 0; i < n->nkids; i++) {
    const bim_node_t *kid = n->kids[i];
    if (!kid->name || !bim_node_is(kid, steps[level].ns, steps[level].name)) continue;
    if (level == k - 1) {
      if (h->n == h->cap) {
        void *np = realloc(h->v, sizeof *h->v * (h->cap ? h->cap * 2 : 64));
        if (!np) return -1;
        h->v = np;
        h->cap = h->cap ? h->cap * 2 : 64;
      }
      h->v[h->n++] = (hit_t){kid, code};
    } else if (collect(h, kid, steps, level + 1, k, code)) {
      return -1;
    }
  }
  return 0;
}

static const char *own(bim_node_t *n, const char *s) { return s ? bim_node_keep(n, s) : NULL; }

static int copy_decls(bim_node_t *to, const bim_node_t *from) {
  for (const bim_node_t *a = from; a; a = a->parent) {
    void *np;
    if (!a->nnsdecls) continue;
    if (!(np = realloc(to->nsdecls, sizeof *to->nsdecls * (size_t)(to->nnsdecls + a->nnsdecls)))) return -1;
    to->nsdecls = np;
    for (int i = a->nnsdecls - 1; i >= 0; i--) {
      bim_nsdecl_t *d = &to->nsdecls[to->nnsdecls];
      if (!(d->prefix = bim_node_keep(to, a->nsdecls[i].prefix)) || !(d->uri = bim_node_keep(to, a->nsdecls[i].uri))) return -1;
      to->nnsdecls++;
    }
  }
  return 0;
}

static bim_node_t *prune_copy(const bim_node_t *src, const hits_t *h, int *lost) {
  bim_node_t *d = bim_node_new(NULL);
  if (!d) return NULL;
  d->name = own(d, src->name);
  d->ns = own(d, src->ns);
  d->cast = src->cast;
  if ((src->name && !d->name) || (src->ns && !d->ns) || (src->text && bim_node_set_text(d, src->text))) goto fail;
  for (int i = 0; i < src->nattrs; i++) {
    char *v = strdup(src->attrs[i].value);
    const char *an = own(d, src->attrs[i].name);
    const char *as = own(d, src->attrs[i].ns);
    if (!v || !an || (src->attrs[i].ns && !as)) {
      free(v);
      goto fail;
    }
    if (bim_node_add_attr_ns(d, as, an, v) == -1) goto fail;
  }
  for (int i = 0; i < src->nkids; i++) {
    const bim_node_t *sk = src->kids[i];
    int kl = 0;
    bim_node_t *k;
    if (sk->name && hit_find(h, sk)) {
      *lost = 1;
      continue;
    }
    if (!(k = prune_copy(sk, h, &kl))) goto fail;
    if (bim_node_add(d, k)) {
      bim_node_free(k);
      goto fail;
    }
    *lost |= kl;
  }
  return d;
fail:
  bim_node_free(d);
  return NULL;
}

static int split_walk(const bim_node_t *n, const hits_t *h, bim_tvapart_t *out, size_t *cnt) {
  const hit_t *hit = n->name ? hit_find(h, n) : NULL;
  if (hit) {
    int lost = 0;
    bim_node_t *r = prune_copy(n, h, &lost);
    if (!r || copy_decls(r, n->parent)) {
      bim_node_free(r);
      return -1;
    }
    out[*cnt] = (bim_tvapart_t){hit->code, r};
    (*cnt)++;
  }
  for (int i = 0; i < n->nkids; i++)
    if (split_walk(n->kids[i], h, out, cnt)) return -1;
  return 0;
}

void bim_tvafrag_free(bim_tvapart_t *p, size_t n) {
  for (size_t i = 0; p && i < n; i++) bim_node_free(p[i].root);
  free(p);
}

int bim_tvafrag_copy(const bim_tvapart_t *p, size_t n, bim_tvapart_t **out) {
  hits_t none = {0};
  bim_tvapart_t *c = calloc(n + 1, sizeof *c);
  if (!c) return -1;
  for (size_t i = 0; i < n; i++) {
    int lost = 0;
    c[i].code = p[i].code;
    if (!(c[i].root = prune_copy(p[i].root, &none, &lost)) || copy_decls(c[i].root, p[i].root)) {
      bim_tvafrag_free(c, i + 1);
      return -1;
    }
  }
  *out = c;
  return 0;
}

int bim_tvafrag_split(const xsd_compiled_t *c, const bim_tvans_t *ns, const bim_node_t *doc, bim_tvapart_t **out, size_t *n) {
  hits_t h = {0};
  bim_tvaroot_t steps[STEPS_MAX];
  bim_tvapart_t *parts = NULL;
  size_t cnt = 0;
  int rc = -1;
  if (bim_tvafrag_steps(c, ns, 0x000E, steps, STEPS_MAX) != 1 || !bim_node_is(doc, steps[0].ns, steps[0].name)) return -2;
  for (size_t i = 0; i < sizeof rows / sizeof *rows; i++) {
    int k = bim_tvafrag_steps(c, ns, rows[i].code, steps, STEPS_MAX);
    if (k < 1) continue;
    if (k == 1) {
      if (h.n == h.cap) {
        void *np = realloc(h.v, sizeof *h.v * (h.cap ? h.cap * 2 : 64));
        if (!np) goto out;
        h.v = np;
        h.cap = h.cap ? h.cap * 2 : 64;
      }
      h.v[h.n++] = (hit_t){doc, rows[i].code};
    } else if (collect(&h, doc, steps, 1, k, rows[i].code)) {
      goto out;
    }
  }
  qsort(h.v, h.n, sizeof *h.v, hit_cmp);
  parts = calloc(h.n ? h.n : 1, sizeof *parts);
  if (!parts || split_walk(doc, &h, parts, &cnt)) {
    bim_tvafrag_free(parts, cnt);
    goto out;
  }
  *out = parts;
  *n = cnt;
  rc = 0;
out:
  free(h.v);
  return rc;
}

static int sbc_index(const bim_type_t *t, const bim_node_t *k) {
  for (int i = 0; t && k->name && i < t->nsbc; i++)
    if (t->sbc[i].kind == BIM_SBC_ELEM && bim_node_is(k, t->sbc[i].ns, t->sbc[i].name)) return i;
  return 1 << 30;
}

static int add_ordered(bim_node_t *parent, const bim_type_t *pt, bim_node_t *kid) {
  int at = parent->nkids;
  int idx = sbc_index(pt, kid);
  if (bim_node_add(parent, kid)) return -1;
  while (at > 0 && sbc_index(pt, parent->kids[at - 1]) > idx) {
    parent->kids[at] = parent->kids[at - 1];
    at--;
  }
  parent->kids[at] = kid;
  return 0;
}

int bim_tvafrag_join(const xsd_compiled_t *c, const bim_tvans_t *ns, bim_tvapart_t *p, size_t n, bim_node_t **out) {
  bim_tvaroot_t steps[STEPS_MAX];
  bim_node_t *main = NULL;
  for (size_t i = 0; i < n; i++)
    if (p[i].code == 0x000E) {
      if (main) continue;
      main = p[i].root;
      p[i].root = NULL;
    }
  if (bim_tvafrag_steps(c, ns, 0x000E, steps, STEPS_MAX) != 1) return -3;
  if (!main && ((main = bim_node_new(steps[0].name)) == NULL)) return -1;
  if (!main->ns) main->ns = steps[0].ns;
  for (size_t i = 0; i < n; i++) {
    bim_node_t *cur = main;
    int k;
    if (!p[i].root || p[i].code == 0x000E) continue;
    if ((k = bim_tvafrag_steps(c, ns, p[i].code, steps, STEPS_MAX)) < 2) {
      bim_node_free(main);
      return -3;
    }
    for (int l = 1; l < k - 1; l++) {
      bim_node_t *next = NULL;
      for (int j = 0; j < cur->nkids && !next; j++)
        if (cur->kids[j]->name && bim_node_is(cur->kids[j], steps[l].ns, steps[l].name)) next = cur->kids[j];
      if (!next) {
        if (!(next = bim_node_new(steps[l].name))) goto fail;
        next->ns = steps[l].ns;
        if (add_ordered(cur, steps[l - 1].type, next)) {
          bim_node_free(next);
          goto fail;
        }
      }
      cur = next;
    }
    if (add_ordered(cur, steps[k - 2].type, p[i].root)) goto fail;
    p[i].root = NULL;
  }
  *out = main;
  return 0;
fail:
  bim_node_free(main);
  return -1;
}
