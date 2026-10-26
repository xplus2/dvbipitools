/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/bim/codec.h"
#include "lib/bim/codec/codec_int.h"

bim_node_t *bim_node_new(const char *name) {
  bim_node_t *n = calloc(1, sizeof *n);
  if (n) n->name = name;
  return n;
}

int bim_tree_has_cast(const bim_node_t *n) {
  if (n->cast) return 1;
  for (int i = 0; i < n->nkids; i++)
    if (n->kids[i]->name && bim_tree_has_cast(n->kids[i])) return 1;
  return 0;
}

void bim_node_free(bim_node_t *n) {
  if (!n) return;
  free(n->text);
  for (int i = 0; i < n->nattrs; i++) free(n->attrs[i].value);
  free(n->attrs);
  for (int i = 0; i < n->nkids; i++) bim_node_free(n->kids[i]);
  free(n->kids);
  free(n->nsdecls);
  free(n->href);
  for (int i = 0; i < n->npool; i++) free(n->pool[i]);
  free(n->pool);
  free(n);
}

/* pool capacity implicit: 4, then doubled when full */
const char *bim_node_keep(bim_node_t *n, const char *s) {
  char *dup;
  if (!n->npool || (n->npool >= 4 && !(n->npool & (n->npool - 1)))) {
    char **p = realloc(n->pool, sizeof *p * (size_t)(n->npool ? n->npool * 2 : 4));
    if (!p) return NULL;
    n->pool = p;
  }
  if (!(dup = strdup(s))) return NULL;
  n->pool[n->npool++] = dup;
  return dup;
}

static int attr_append(bim_node_t *n, const char *ns, const char *name, char *v) {
  void *np = realloc(n->attrs, sizeof *n->attrs * (size_t)(n->nattrs + 1));
  if (!np) {
    free(v);
    return -1;
  }
  n->attrs = np;
  n->attrs[n->nattrs].name = name;
  n->attrs[n->nattrs].ns = ns;
  n->attrs[n->nattrs].value = v;
  n->nattrs++;
  return 0;
}

int bim_node_add_attr_ns(bim_node_t *n, const char *ns, const char *name, char *value) {
  for (int i = 0; i < n->nattrs; i++)
    if (!strcmp(n->attrs[i].name, name) && (n->attrs[i].ns == ns || (n->attrs[i].ns && ns && !strcmp(n->attrs[i].ns, ns)))) {
      free(value);
      return -2;
    }
  return attr_append(n, ns, name, value);
}

const char *bim_node_ns_uri(const bim_node_t *n, const char *prefix) {
  if (!strcmp(prefix, "xml")) return XML_NS;
  for (; n; n = n->parent)
    for (int i = n->nnsdecls - 1; i >= 0; i--)
      if (!strcmp(n->nsdecls[i].prefix, prefix)) return *n->nsdecls[i].uri ? n->nsdecls[i].uri : NULL;
  return NULL;
}

int bim_node_qname(const bim_node_t *n, const char *qn, const char **ns, const char **local) {
  const char *colon = strchr(qn, ':');
  char pfx[256];
  size_t pl = colon ? (size_t)(colon - qn) : 0;
  if (pl >= sizeof pfx || !*qn || (colon && (!pl || !colon[1]))) return -1;
  memcpy(pfx, qn, pl);
  pfx[pl] = '\0';
  *ns = bim_node_ns_uri(n, pfx);
  if (pl && !*ns) return -2;
  *local = colon ? colon + 1 : qn;
  return 0;
}

/* Takes ownership of v, also on failure */
int node_put_attr(bim_node_t *n, const char *name, char *v) {
  for (int i = 0; i < n->nattrs; i++) {
    if (strcmp(n->attrs[i].name, name)) continue;
    free(n->attrs[i].value);
    n->attrs[i].value = v;
    return 0;
  }
  return attr_append(n, NULL, name, v);
}

int bim_node_set_attr(bim_node_t *n, const char *name, const char *value) {
  char *v = strdup(value);
  if (!v) return -1;
  return node_put_attr(n, name, v);
}

const char *bim_node_attr(const bim_node_t *n, const char *name) {
  for (int i = 0; i < n->nattrs; i++)
    if (!strcmp(n->attrs[i].name, name)) return n->attrs[i].value;
  return NULL;
}

const char *bim_node_attr_ns(const bim_node_t *n, const char *ns, const char *name) {
  for (int i = 0; i < n->nattrs; i++)
    if (!strcmp(n->attrs[i].name, name) && (ns && n->attrs[i].ns ? !strcmp(ns, n->attrs[i].ns) : ns == n->attrs[i].ns)) return n->attrs[i].value;
  return NULL;
}

int bim_node_is(const bim_node_t *n, const char *ns, const char *name) {
  return n->name && !strcmp(n->name, name) && (ns && n->ns ? !strcmp(ns, n->ns) : ns == n->ns);
}

int bim_node_set_text(bim_node_t *n, const char *text) {
  char *t = NULL;
  if (text) {
    t = strdup(text);
    if (!t) return -1;
  }
  free(n->text);
  n->text = t;
  return 0;
}

int bim_node_add(bim_node_t *n, bim_node_t *kid) {
  if (n->nkids >= n->kcap) {
    void *np = array_grow(n->kids, &n->kcap, n->nkids + 1, sizeof *n->kids);
    if (!np) return -1;
    n->kids = np;
  }
  n->kids[n->nkids++] = kid;
  kid->parent = n;
  return 0;
}

bim_node_t *bim_node_add_new(bim_node_t *n, const char *name, const char *text) {
  bim_node_t *k = bim_node_new(name);
  if (!k) return NULL;
  if ((text && bim_node_set_text(k, text)) || bim_node_add(n, k)) {
    bim_node_free(k);
    return NULL;
  }
  return k;
}

const bim_node_t *bim_node_child(const bim_node_t *n, const char *name) {
  for (int i = 0; i < n->nkids; i++)
    if (n->kids[i]->name && !strcmp(n->kids[i]->name, name)) return n->kids[i];
  return NULL;
}
