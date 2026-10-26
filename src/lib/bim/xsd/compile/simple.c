/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/xsd/compile/priv.h"

static int simple_codec(cc_t *c, const xsd_type_t *t, int exact, facets_t *a, bim_simple_t *d);

static const xsd_codec_map_t *codec_for(const cc_t *c, const xsd_type_t *t, int exact) {
  if (!t->q.name) return NULL;
  for (int i = 0; i < c->ncodecs; i++) {
    const xsd_codec_map_t *m = &c->codecs[i];
    if ((exact || m->derived) && !strcmp(m->name, t->q.name) && !strcmp(m->ns ? m->ns : "", t->q.ns ? t->q.ns : "")) return m;
  }
  return NULL;
}

static void take_facets(facets_t *a, const xsd_facet_t *f, int n) {
  int has_enum = 0;
  int has_pat = 0;
  for (int i = 0; i < n; i++) {
    if (f[i].kind == XSD_F_ENUMERATION) has_enum = 1;
    else if (f[i].kind == XSD_F_PATTERN) has_pat = 1;
    else if (!a->f[f[i].kind]) a->f[f[i].kind] = f[i].value;
  }
  if (has_pat && a->np <= DEPTH_MAX) {
    a->psteps[a->np] = f;
    a->npsteps[a->np++] = n;
  }
  if (has_enum && !a->estep) {
    a->estep = f;
    a->nestep = n;
  }
}

static int parse_sint(const char *v, sint_t *out) {
  uint64_t m = 0;
  out->neg = *v == '-';
  if (*v == '-' || *v == '+') v++;
  if (!*v) return -1;
  for (; *v; v++) {
    if (*v < '0' || *v > '9' || m > (UINT64_MAX - (uint64_t)(*v - '0')) / 10) return -1;
    m = m * 10 + (uint64_t)(*v - '0');
  }
  out->mag = m;
  if (!m) out->neg = 0;
  return 0;
}

/* v + d, d -1 or 1 */
static int sint_step(sint_t *v, int d) {
  if (v->neg == (d < 0)) {
    if (v->mag == UINT64_MAX) return -1;
    v->mag++;
  } else if (!v->mag) {
    v->neg = d < 0;
    v->mag = 1;
  } else if (!--v->mag) {
    v->neg = 0;
  }
  return 0;
}

static int facet_sint(cc_t *c, const xsd_type_t *t, const char *v, sint_t *out) {
  return parse_sint(v, out) ? xsd_set_fail(c->s, "LS400283", "bim: %s: integer facet value %s outside 64 bit", xc_label(t), v) : 0;
}

/* ISO 23001-1 7.5.4.2.1 */
static int integer_codec(cc_t *c, const xsd_type_t *t, const facets_t *a, bim_simple_t *d) {
  sint_t lo = {0};
  sint_t hi = {0};
  int have_lo = 0;
  int have_hi = 0;
  if (a->f[XSD_F_MININCLUSIVE]) {
    have_lo = !facet_sint(c, t, a->f[XSD_F_MININCLUSIVE], &lo);
    if (!have_lo) return -1;
  }
  if (a->f[XSD_F_MINEXCLUSIVE]) {
    if (facet_sint(c, t, a->f[XSD_F_MINEXCLUSIVE], &lo)) return xsd_set_fail(c->s, "LS400283", "bim: %s: integer facet value %s outside 64 bit", xc_label(t), a->f[XSD_F_MINEXCLUSIVE]);
    if (sint_step(&lo, 1)) return xsd_set_fail(c->s, "LS400283", "bim: %s: integer facet value %s outside 64 bit", xc_label(t), a->f[XSD_F_MINEXCLUSIVE]);
    have_lo = 1;
  }
  if (a->f[XSD_F_MAXINCLUSIVE]) {
    have_hi = !facet_sint(c, t, a->f[XSD_F_MAXINCLUSIVE], &hi);
    if (!have_hi) return -1;
  }
  if (a->f[XSD_F_MAXEXCLUSIVE]) {
    if (facet_sint(c, t, a->f[XSD_F_MAXEXCLUSIVE], &hi)) return xsd_set_fail(c->s, "LS400283", "bim: %s: integer facet value %s outside 64 bit", xc_label(t), a->f[XSD_F_MAXEXCLUSIVE]);
    if (sint_step(&hi, -1)) return xsd_set_fail(c->s, "LS400283", "bim: %s: integer facet value %s outside 64 bit", xc_label(t), a->f[XSD_F_MAXEXCLUSIVE]);
    have_hi = 1;
  }
  if (!have_lo || !have_hi) {
    d->codec = BIM_SVLU5;
    return 0;
  }
  if (lo.neg ? lo.mag > (uint64_t)INT64_MAX + 1 : lo.mag > (uint64_t)INT64_MAX) return xsd_set_fail(c->s, "LS400283", "bim: %s: integer facet value %s outside 64 bit", xc_label(t), a->f[XSD_F_MININCLUSIVE] ? a->f[XSD_F_MININCLUSIVE] : a->f[XSD_F_MINEXCLUSIVE]);
  if (!lo.neg && !hi.neg && hi.mag >= lo.mag) d->range.span = hi.mag - lo.mag;
  else if (lo.neg && !hi.neg && hi.mag <= UINT64_MAX - lo.mag) d->range.span = hi.mag + lo.mag;
  else if (lo.neg && hi.neg && lo.mag >= hi.mag) d->range.span = lo.mag - hi.mag;
  else return xsd_set_fail(c->s, "LS400284", "bim: %s: integer range empty or wider than 64 bit", xc_label(t));
  if (!lo.neg) d->range.min = (int64_t)lo.mag;
  else if (lo.mag > (uint64_t)INT64_MAX) d->range.min = INT64_MIN;
  else d->range.min = -(int64_t)lo.mag;
  d->codec = BIM_RANGE;
  return 0;
}

static int list_length(cc_t *c, const xsd_type_t *t, const char *v, uint64_t *out) {
  sint_t n;
  if (parse_sint(v, &n) || n.neg || n.mag > (uint64_t)INT64_MAX) return xsd_set_fail(c->s, "LS400285", "bim: %s: list length facet %s invalid", xc_label(t), v);
  *out = n.mag;
  return 0;
}

/* ISO 23001-1 7.5.4.2.3 */
static int list_codec(cc_t *c, const xsd_type_t *t, const xsd_type_t *l, const facets_t *a, bim_simple_t *d) {
  uint64_t lo = 0;
  uint64_t hi = 0;
  d->codec = BIM_LIST;
  if (!(d->item = xc_get_simple(c, l->item))) return -1;
  if (a->f[XSD_F_LENGTH]) {
    if (list_length(c, t, a->f[XSD_F_LENGTH], &lo)) return -1;
    hi = lo;
  } else {
    if (a->f[XSD_F_MINLENGTH] && list_length(c, t, a->f[XSD_F_MINLENGTH], &lo)) return -1;
    if (a->f[XSD_F_MAXLENGTH] && list_length(c, t, a->f[XSD_F_MAXLENGTH], &hi)) return -1;
    d->unbounded = !a->f[XSD_F_MAXLENGTH];
  }
  if (!d->unbounded && hi < lo) return xsd_set_fail(c->s, "LS400285", "bim: %s: list length facet %s invalid", xc_label(t), a->f[XSD_F_MAXLENGTH]);
  d->range.min = (int64_t)lo;
  d->range.span = d->unbounded ? 0 : hi - lo;
  return 0;
}

static int enum_codec(cc_t *c, const facets_t *a, bim_simple_t *d) {
  const char **v = xc_alloc(c, sizeof *v * (size_t)a->nestep);
  int n = 0;
  if (!v) return xc_oom(c);
  for (int i = 0; i < a->nestep; i++)
    if (a->estep[i].kind == XSD_F_ENUMERATION) v[n++] = a->estep[i].value;
  qsort(v, (size_t)n, sizeof *v, xc_strp_cmp);
  d->nenums = 0;
  for (int i = 0; i < n; i++)
    if (!d->nenums || strcmp(v[d->nenums - 1], v[i])) v[d->nenums++] = v[i];
  d->enums = v;
  d->codec = BIM_ENUM;
  return 0;
}

static int union_codec(cc_t *c, const xsd_type_t *u, bim_simple_t *d) {
  const bim_simple_t **m = xc_alloc(c, sizeof *m * (size_t)u->nmembers);
  if (!m) return xc_oom(c);
  for (int i = 0; i < u->nmembers; i++)
    if (!(m[i] = xc_get_simple(c, u->members[i]))) return -1;
  d->codec = BIM_UNION;
  d->members = m;
  d->nmembers = u->nmembers;
  return 0;
}

/* ISO 23001-1 7.5.4.1, ISO 15938-1:2002 8.5.4.1 */
static bim_codec_t primitive_codec(const cc_t *c, const char *name) {
  static const char *const utf8[] = {"decimal", "string", "duration", "dateTime", "time", "date", "gYearMonth",
                                     "gYear",   "gMonthDay", "gDay", "gMonth", "anyURI", "QName"};
  if (!strcmp(name, "boolean")) return BIM_BOOLEAN;
  if (!strcmp(name, "float")) return BIM_FLOAT;
  if (!strcmp(name, "double")) return BIM_DOUBLE;
  if (!strcmp(name, "hexBinary")) return BIM_HEXBIN;
  if (!strcmp(name, "base64Binary")) return BIM_BASE64;
  for (size_t i = 0; i < sizeof utf8 / sizeof utf8[0]; i++)
    if (!strcmp(name, utf8[i])) return c->o->zlib ? BIM_ZSTRING : BIM_UTF8;
  return BIM_UNSUPPORTED;
}

/* XML Schema Part 2 4.3.4: patterns of one step ORed, steps ANDed */
static int membership(cc_t *c, const xsd_type_t *t, const facets_t *a, bim_simple_t *d) {
  const void **v;
  if (a->np) {
    if (!(v = xc_alloc(c, sizeof *v * (size_t)a->np))) return xc_oom(c);
    for (int i = 0; i < a->np; i++) {
      const char *parts[3 * 64] = {NULL};
      int k = 0;
      char *re;
      for (int j = 0; j < a->npsteps[i] && k < 3 * 64 - 3; j++) {
        if (a->psteps[i][j].kind != XSD_F_PATTERN) continue;
        parts[k] = k ? "|(" : "(";
        parts[k + 1] = a->psteps[i][j].value;
        parts[k + 2] = ")";
        k += 3;
      }
      if (!(re = xc_cat(c, parts, k))) return xc_oom(c);
      if (!(v[i] = xsd_pattern_compile(c->s, re))) return xsd_set_fail(c->s, "LS400290", "bim: %s: pattern %s unsupported or malformed", xc_label(t), re);
    }
    d->patterns = v;
    d->npatterns = a->np;
    d->match = xsd_pattern_match;
  }
  if (d->codec != BIM_LIST && (a->f[XSD_F_LENGTH] || a->f[XSD_F_MINLENGTH] || a->f[XSD_F_MAXLENGTH])) {
    uint64_t lo = 0;
    uint64_t hi = UINT64_MAX;
    if (a->f[XSD_F_LENGTH]) {
      if (list_length(c, t, a->f[XSD_F_LENGTH], &lo)) return -1;
      hi = lo;
    } else {
      if (a->f[XSD_F_MINLENGTH] && list_length(c, t, a->f[XSD_F_MINLENGTH], &lo)) return -1;
      if (a->f[XSD_F_MAXLENGTH] && list_length(c, t, a->f[XSD_F_MAXLENGTH], &hi)) return -1;
    }
    d->has_length = 1;
    d->minlen = lo;
    d->maxlen = hi;
  }
  return 0;
}

/* value codec of t and membership facets, facets in a nearer than t. exact: value type is t itself */
static int simple_from(cc_t *c, const xsd_type_t *t, int exact, facets_t *a, bim_simple_t *d) {
  return simple_codec(c, t, exact, a, d) ? -1 : membership(c, t, a, d);
}

static int simple_codec(cc_t *c, const xsd_type_t *t, int exact, facets_t *a, bim_simple_t *d) {
  const xsd_type_t *origin = t;
  int integer = 0;
  for (int depth = 0;; depth++) {
    const xsd_codec_map_t *m = codec_for(c, t, exact);
    if (m) {
      d->codec = m->codec;
      if (t->kind == XSD_SIMPLE) take_facets(a, t->facets, t->nfacets);
      return 0;
    }
    if (depth > DEPTH_MAX || t->kind != XSD_SIMPLE) {
      d->codec = BIM_UNSUPPORTED;
      return 0;
    }
    if (t->deriv == XSD_D_LIST || t->deriv == XSD_D_UNION) break;
    take_facets(a, t->facets, t->nfacets);
    if (t->builtin && !strcmp(t->q.name, "integer")) integer = 1;
    if (!t->base || (t->builtin && (!strcmp(t->q.name, "anySimpleType") || (t->base->builtin && !strcmp(t->base->q.name, "anySimpleType"))))) break;
    t = t->base;
    exact = 0;
  }
  if (a->estep) return enum_codec(c, a, d);
  if (t->deriv == XSD_D_LIST && t->builtin && (c->o->compat & BIM_COMPAT_LIST_STRING)) {
    d->codec = primitive_codec(c, "string");
    return 0;
  }
  if (t->deriv == XSD_D_LIST) return list_codec(c, origin, t, a, d);
  if (t->deriv == XSD_D_UNION) return union_codec(c, t, d);
  if (integer) return integer_codec(c, origin, a, d);
  d->codec = t->builtin ? primitive_codec(c, t->q.name) : BIM_UNSUPPORTED;
  return 0;
}

const bim_simple_t *xc_get_simple(cc_t *c, const xsd_type_t *t) {
  bim_simple_t *d = xc_pmap_get(&c->simples, t);
  facets_t a;
  if (d) return d;
  if (!(d = xc_alloc(c, sizeof *d)) || xc_pmap_put(c, &c->simples, t, d)) {
    xc_oom(c);
    return NULL;
  }
  d->codec = BIM_UNSUPPORTED;
  memset(&a, 0, sizeof a);
  if (t->kind == XSD_SIMPLE && t->base && t->base->kind == XSD_SIMPLE) {
    d->base = xc_get_simple(c, t->base);
    if (!d->base) return NULL;
  }
  return simple_from(c, t, 1, &a, d) ? NULL : d;
}

const bim_simple_t *xc_simple_content(cc_t *c, const xsd_type_t *t) {
  bim_simple_t *d = xc_pmap_get(&c->simples, t);
  facets_t a;
  int exact = 1;
  if (d) return d;
  if (!(d = xc_alloc(c, sizeof *d)) || xc_pmap_put(c, &c->simples, t, d)) {
    xc_oom(c);
    return NULL;
  }
  memset(&a, 0, sizeof a);
  for (int depth = 0; t && t->kind == XSD_COMPLEX; depth++) {
    if (depth > DEPTH_MAX) t = NULL;
    else if (t->deriv == XSD_D_RESTRICTION) {
      take_facets(&a, t->facets, t->nfacets);
      exact = 0;
      t = t->simple ? t->simple : t->base;
    } else {
      t = t->base;
    }
  }
  if (!t) {
    d->codec = BIM_UNSUPPORTED;
    return d;
  }
  if (!(d->base = xc_get_simple(c, t))) return NULL;
  return simple_from(c, t, exact, &a, d) ? NULL : d;
}
