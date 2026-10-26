/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_XSD_MODEL_PRIV_H
#define DVBIPITOOLS_LIB_BIM_XSD_MODEL_PRIV_H

#include <stddef.h>
#include <stdint.h>

#include "lib/bim/codec.h"
#include "lib/bim/xsd/xsd.h"
#include "lib/sys/ioutil.h"

#define ARENA_CHUNK 65536

typedef struct arena_chunk {
  struct arena_chunk *next;
  size_t used;
  size_t cap;
  max_align_t data[];
} arena_chunk_t;

/* components keyed by expanded name, q is first member of each */
typedef struct {
  xsd_qname_t **slots;
  size_t nslots;
  size_t nused;
  void **order; /* insertion order */
  int n;
  int cap;
} cmap_t;

/* pointer list */
typedef struct {
  void **v;
  int n;
  int cap;
} plist_t;

struct xsd_set {
  arena_chunk_t *arena;
  cmap_t types;
  cmap_t elems;
  cmap_t attrs;
  cmap_t groups;
  cmap_t agroups;
  plist_t all_types;
  plist_t all_elems;
  plist_t all_attrs;
  plist_t all_attrsets; /* xsd_attrs_t of types and attribute groups */
  plist_t particles; /* top particles of types and groups */
  plist_t docs;
  xsd_ref_t *refs; /* of last added document */
  int nrefs;
  int rcap;
  int oom;
  const char *code;
  char err[512];
};

/* per document */
typedef struct {
  xsd_set_t *s;
  const char *path;
  const char *tns;
  int elem_qualified;
  int attr_qualified;
} ctx_t;

void *xm_arena_alloc(xsd_set_t *s, size_t n);
char *xm_arena_strndup(xsd_set_t *s, const char *p, size_t n);
int xm_plist_add(xsd_set_t *s, plist_t *l, void *p);
int xm_qeq(const xsd_qname_t *a, const char *ns, const char *name);
void *xm_cmap_get(const cmap_t *m, const char *ns, const char *name);
/* -1 oom, -2 duplicate */
int xm_cmap_put(xsd_set_t *s, cmap_t *m, xsd_qname_t *q);
void xm_cmap_replace(cmap_t *m, const xsd_qname_t *old, xsd_qname_t *q);
int xm_is_xs(const bim_node_t *n, const char *name);
/* unqualified attribute of an XSD element */
const char *xm_xattr(const bim_node_t *n, const char *name);
xsd_type_t *xm_parse_simple_type(ctx_t *c, const bim_node_t *n);
xsd_type_t *xm_parse_complex_type(ctx_t *c, const bim_node_t *n);
int xm_parse_group(ctx_t *c, const bim_node_t *k, xsd_group_t **out);
int xm_parse_agroup(ctx_t *c, const bim_node_t *k, xsd_agroup_t **out);
int xm_add_component(ctx_t *c, const bim_node_t *k);

#endif
