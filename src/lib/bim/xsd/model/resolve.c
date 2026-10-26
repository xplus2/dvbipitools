/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/bim/xsd/model/priv.h"

static int lookup(xsd_set_t *s, const cmap_t *m, const xsd_qname_t *q, const char *what, void **out) {
  *out = xm_cmap_get(m, q->ns, q->name);
  const char *ns = q->ns ? q->ns : "";
  return *out ? 0 : xsd_set_fail(s, "LS400282", "bim: unresolved %s {%s}%s", what, ns, q->name);
}

static int resolve_particle(xsd_set_t *s, xsd_particle_t *p) {
  void *v;
  if (p->kind == XSD_P_ELEM && !p->elem) {
    if (lookup(s, &s->elems, &p->ref, "element", &v)) return -1;
    p->elem = v;
  } else if (p->kind == XSD_P_GROUP && !p->group) {
    if (lookup(s, &s->groups, &p->ref, "group", &v)) return -1;
    p->group = v;
  }
  for (int i = 0; i < p->nkids; i++)
    if (resolve_particle(s, &p->kids[i])) return -1;
  return 0;
}

static int resolve_type(xsd_set_t *s, xsd_type_t *t) {
  void *v;
  if (t->base_ref.name && !t->base) {
    if (lookup(s, &s->types, &t->base_ref, "type", &v)) return -1;
    t->base = v;
  }
  if (t->item_ref.name && !t->item) {
    if (lookup(s, &s->types, &t->item_ref, "type", &v)) return -1;
    t->item = v;
  }
  for (int i = 0; i < t->nmember_refs; i++) {
    if (t->members[i]) continue;
    if (lookup(s, &s->types, &t->member_refs[i], "type", &v)) return -1;
    t->members[i] = v;
  }
  if (t->kind == XSD_SIMPLE && !t->base && !t->builtin) t->base = xm_cmap_get(&s->types, XSD_NS, "anySimpleType");
  return 0;
}

static int resolve_attrs(xsd_set_t *s, xsd_attrs_t *as) {
  void *v;
  for (int i = 0; i < as->nuses; i++) {
    xsd_attr_use_t *u = &as->uses[i];
    if (u->attr) continue;
    if (lookup(s, &s->attrs, &u->ref, "attribute", &v)) return -1;
    u->attr = v;
  }
  for (int i = 0; i < as->ngroups; i++) {
    if (as->groups[i]) continue;
    if (lookup(s, &s->agroups, &as->group_refs[i], "attributeGroup", &v)) return -1;
    as->groups[i] = v;
  }
  return 0;
}

int xsd_set_resolve(xsd_set_t *s) {
  void *v;
  xsd_type_t *any_type = xm_cmap_get(&s->types, XSD_NS, "anyType");
  xsd_type_t *any_simple = xm_cmap_get(&s->types, XSD_NS, "anySimpleType");
  int changed = 1;
  for (int i = 0; i < s->all_types.n; i++)
    if (resolve_type(s, s->all_types.v[i])) return -1;
  for (int i = 0; i < s->all_attrsets.n; i++)
    if (resolve_attrs(s, s->all_attrsets.v[i])) return -1;
  for (int i = 0; i < s->particles.n; i++)
    if (resolve_particle(s, s->particles.v[i])) return -1;
  for (int i = 0; i < s->all_attrs.n; i++) {
    xsd_attr_t *a = s->all_attrs.v[i];
    if (a->type) continue;
    if (!a->type_ref.name) {
      a->type = any_simple;
      continue;
    }
    if (lookup(s, &s->types, &a->type_ref, "type", &v)) return -1;
    a->type = v;
  }
  for (int i = 0; i < s->all_elems.n; i++) {
    xsd_elem_t *e = s->all_elems.v[i];
    if (e->subst_ref.name) {
      if (lookup(s, &s->elems, &e->subst_ref, "element", &v)) return -1;
      e->subst_head = v;
    }
    if (!e->type && e->type_ref.name) {
      if (lookup(s, &s->types, &e->type_ref, "type", &v)) return -1;
      e->type = v;
    }
  }
  /* XML Schema Part 1 3.3.2: no type, substitution group head's type */
  for (int round = 0; changed && round < s->all_elems.n + 1; round++) {
    changed = 0;
    for (int i = 0; i < s->all_elems.n; i++) {
      xsd_elem_t *e = s->all_elems.v[i];
      if (!e->type && e->subst_head && e->subst_head->type) {
        e->type = e->subst_head->type;
        changed = 1;
      }
    }
  }
  for (int i = 0; i < s->all_elems.n; i++) {
    xsd_elem_t *e = s->all_elems.v[i];
    if (!e->type) e->type = any_type;
  }
  return s->oom ? -2 : 0;
}
