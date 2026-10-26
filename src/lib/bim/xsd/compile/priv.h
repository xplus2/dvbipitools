/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_XSD_COMPILE_PRIV_H
#define DVBIPITOOLS_LIB_BIM_XSD_COMPILE_PRIV_H

#include <stddef.h>
#include <stdint.h>

#include "lib/bim/codec.h"
#include "lib/bim/xsd/xsd.h"
#include "lib/sys/ioutil.h"

#define DEPTH_MAX 256

#define M7_NS "urn:mpeg:mpeg7:schema:2001"

/* open addressing */
typedef struct {
  const void **k;
  void **v;
  size_t cap;
  size_t n;
} pmap_t;

/* syntax tree node, ISO 23001-1 7.5.2.2.3 */
typedef struct snode {
  xsd_pkind_t kind; /* ELEM, SEQ, CHOICE, ALL, ANY */
  uint64_t min;
  uint64_t max;
  const xsd_elem_t *elem;
  const xsd_wildcard_t *wc;
  struct snode **kids;
  int nkids;
  char *sig;
} snode_t;

typedef struct {
  const xsd_attr_t *attr;
  xsd_use_t use;
  const char *fixed;
} ause_t;

typedef struct {
  ause_t *v;
  int n;
  int cap;
} alist_t;

/* facets by kind, nearest wins, patterns per step */
typedef struct {
  const char *f[XSD_F_COUNT];
  const xsd_facet_t *estep;
  int nestep;
  const xsd_facet_t *psteps[DEPTH_MAX + 1];
  int npsteps[DEPTH_MAX + 1];
  int np;
} facets_t;

typedef struct {
  xsd_set_t *s;
  const xsd_compile_opts_t *o;
  xsd_codec_map_t *codecs;
  int ncodecs;
  pmap_t types; /* xsd_type_t -> bim_type_t */
  pmap_t simples; /* xsd_type_t -> bim_simple_t */
  pmap_t elems; /* global xsd_elem_t -> bim_elem_t */
  pmap_t sbc_done;
  const xsd_type_t **queue;   int nq;
  int qcap;
  int nfilled;
  int nsbc;
} cc_t;

/* sign + magnitude, -0 normalized */
typedef struct {
  int neg;
  uint64_t mag;
} sint_t;

typedef struct {
  bim_sbc_t *v;
  int n;
  int cap;
  char **keys; /* attribute sort keys */
} sbclist_t;

typedef struct {
  const char *ns;
  const char *name;
} ename_t;

typedef struct {
  int *first; /* children of i: kids[first[i] .. first[i + 1]), roots at slot n */
  int *kids;
  int n;
} forest_t;

int xc_oom(cc_t *c);
void *xc_alloc(cc_t *c, size_t n);
const char *xc_label(const xsd_type_t *t);
int xc_is_any_type(const xsd_type_t *t);
void *xc_pmap_get(const pmap_t *m, const void *k);
int xc_pmap_put(cc_t *c, pmap_t *m, const void *k, void *v);
/* order of "ns:name" (7.2), no ns: local name (conformance streams) */
int xc_ename_cmp(const char *ans, const char *a, const char *bns, const char *b);
char *xc_cat(cc_t *c, const char *const *parts, int n);
char *xc_ename(cc_t *c, const char *ns, const char *name);
int xc_strp_cmp(const void *a, const void *b);
int xc_ns_eq(const char *a, const char *b);
int xc_add_codec(cc_t *c, const char *ns, const char *name, bim_codec_t codec, int derived);
const bim_simple_t *xc_get_simple(cc_t *c, const xsd_type_t *t);
/* XML Schema 1 3.4.2 */
const bim_simple_t *xc_simple_content(cc_t *c, const xsd_type_t *t);
const bim_wildcard_t *xc_wc_from(cc_t *c, const xsd_wildcard_t *x);
/* with attribute group wildcards intersected (3.4.2). *out NULL: none */
int xc_local_wildcard(cc_t *c, const xsd_type_t *t, const xsd_attrs_t *as, int depth, const bim_wildcard_t **out);
int xc_own_attrs(cc_t *c, const xsd_type_t *t, const xsd_attrs_t *as, alist_t *l, int depth);
int xc_battr_cmp(const void *a, const void *b);
/* 7.5.3.1 (Cor 1), 15938-1 8.5.3. minus: its uses left out (7.4.4) */
int xc_compile_attrs(cc_t *c, const xsd_type_t *t, const xsd_type_t *minus, bim_type_t *b);
/* group and element refs realized (7.5.2.2.2) */
snode_t *xc_build(cc_t *c, const xsd_type_t *t, const xsd_particle_t *p, int depth);
/* 3.4.2 */
int xc_content_empty(const xsd_particle_t *p);
int xc_has_simple_content(const xsd_type_t *t);
/* 7.5.2.2.4 */
char *xc_wc_sig(cc_t *c, const xsd_wildcard_t *w);
/* children of choice and all sorted by signature */
int xc_sign(cc_t *c, snode_t *n);
/* 7.4.4: additions of sub over base */
const bim_type_t *xc_delta_type(cc_t *c, const xsd_type_t *base, const xsd_type_t *sub);
int xc_fill_type(cc_t *c, const xsd_type_t *t, bim_type_t *b);
/* 6.6.5.2.2 */
int xc_compile_sbc(cc_t *c, const xsd_type_t *t, int depth);
/* 6.6.5.4 */
int xc_type_codes(cc_t *c, xsd_compiled_t *out);
/* 7.5.3.3 */
int xc_global_attrs(cc_t *c, xsd_compiled_t *res);
/* 6.6.5.3, 6.6.5.2.3 */
int xc_elem_codes(cc_t *c, xsd_compiled_t *out);
bim_type_t *xc_get_type(cc_t *c, const xsd_type_t *t);

#endif
