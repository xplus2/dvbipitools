/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/bim/xsd/model/priv.h"

static void ctx_init(ctx_t *c, xsd_set_t *s, const char *path, const bim_node_t *root, const char *tns) {
  const char *efd = xm_xattr(root, "elementFormDefault");
  const char *afd = xm_xattr(root, "attributeFormDefault");
  c->s = s;
  c->path = path;
  c->tns = tns;
  c->elem_qualified = efd && !strcmp(efd, "qualified");
  c->attr_qualified = afd && !strcmp(afd, "qualified");
}

static int add_doc(xsd_set_t *s, xml_doc_t *doc, const char *path, int include, const char *tns) {
  const bim_node_t *root = xml_doc_root(doc);
  const char *own;
  ctx_t c;
  int rc = 0;
  s->nrefs = 0;
  if (xm_plist_add(s, &s->docs, doc)) {
    xml_doc_free(doc);
    return -2;
  }
  if (!xm_is_xs(root, "schema")) return xsd_set_fail(s, "LS400250", "bim: %s: root is not xs:schema", path);
  own = xm_xattr(root, "targetNamespace");
  if (include && own && (!tns || strcmp(own, tns))) return xsd_set_fail(s, "LS400275", "bim: %s: included targetNamespace %s differs", path, own);
  ctx_init(&c, s, path, root, include ? tns : own);
  for (int i = 0; i < root->nkids && !rc; i++)
    if (root->kids[i]->name) rc = xm_add_component(&c, root->kids[i]);
  if (s->oom) return -2;
  return rc ? -1 : 0;
}

int xsd_set_add_doc(xsd_set_t *s, xml_doc_t *doc, const char *path) { return add_doc(s, doc, path, 0, NULL); }

int xsd_set_include_doc(xsd_set_t *s, xml_doc_t *doc, const char *path, const char *tns) { return add_doc(s, doc, path, 1, tns); }

static void bind_self_group(xsd_particle_t *p, const xsd_qname_t *q, xsd_group_t *orig) {
  if (p->kind == XSD_P_GROUP && !p->group && xm_qeq(&p->ref, q->ns, q->name)) p->group = orig;
  for (int i = 0; i < p->nkids; i++) bind_self_group(&p->kids[i], q, orig);
}

static int redefine_one(ctx_t *c, const bim_node_t *k) {
  xsd_set_t *s = c->s;
  cmap_t *m;
  xsd_qname_t *nq;
  void *orig;
  int rc;
  if (xm_is_xs(k, "simpleType") || xm_is_xs(k, "complexType")) {
    xsd_type_t *t = xm_is_xs(k, "simpleType") ? xm_parse_simple_type(c, k) : xm_parse_complex_type(c, k);
    if (!t || !t->q.name) return t ? xsd_set_fail(s, "LS400277", "bim: %s: redefined type without name", c->path) : -1;
    if (!(orig = xm_cmap_get(&s->types, t->q.ns, t->q.name))) return xsd_set_fail(s, "LS400278", "bim: %s: redefine of unknown type %s", c->path, t->q.name);
    if (!t->base_ref.name || !xm_qeq(&t->base_ref, t->q.ns, t->q.name)) return xsd_set_fail(s, "LS400279", "bim: %s: redefined type %s not derived from itself", c->path, t->q.name);
    t->base = orig;
    t->base_ref.name = NULL;
    m = &s->types;
    nq = &t->q;
  } else if (xm_is_xs(k, "group")) {
    xsd_group_t *g;
    if ((rc = xm_parse_group(c, k, &g))) return rc;
    if (!(orig = xm_cmap_get(&s->groups, g->q.ns, g->q.name))) return xsd_set_fail(s, "LS400280", "bim: %s: redefine of unknown group %s", c->path, g->q.name);
    bind_self_group(g->particle, &g->q, orig);
    m = &s->groups;
    nq = &g->q;
  } else if (xm_is_xs(k, "attributeGroup")) {
    xsd_agroup_t *g;
    if ((rc = xm_parse_agroup(c, k, &g))) return rc;
    if (!(orig = xm_cmap_get(&s->agroups, g->q.ns, g->q.name))) return xsd_set_fail(s, "LS400281", "bim: %s: redefine of unknown attributeGroup %s", c->path, g->q.name);
    for (int i = 0; i < g->attrs.ngroups; i++)
      if (xm_qeq(&g->attrs.group_refs[i], g->q.ns, g->q.name)) g->attrs.groups[i] = orig;
    m = &s->agroups;
    nq = &g->q;
  } else {
    return 0;
  }
  xm_cmap_replace(m, orig, nq);
  return 0;
}

int xsd_set_redefine(xsd_set_t *s, const xsd_ref_t *ref, const char *path) {
  const bim_node_t *root = ref->node;
  ctx_t c;
  int rc = 0;
  while (root->parent) root = root->parent;
  ctx_init(&c, s, path, root, ref->ns);
  for (int i = 0; i < ref->node->nkids && !rc; i++)
    if (ref->node->kids[i]->name) rc = redefine_one(&c, ref->node->kids[i]);
  if (s->oom) return -2;
  return rc ? -1 : 0;
}

int xsd_set_doc_refs(const xsd_set_t *s, const xsd_ref_t **refs) {
  *refs = s->refs;
  return s->nrefs;
}
