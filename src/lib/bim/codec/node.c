/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <errno.h>
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

void bim_node_free(bim_node_t *n) {
  if (!n) return;
  free(n->text);
  for (int i = 0; i < n->nattrs; i++) free(n->attrs[i].value);
  free(n->attrs);
  for (int i = 0; i < n->nkids; i++) bim_node_free(n->kids[i]);
  free(n->kids);
  free(n);
}

/* Takes ownership of v, also on failure */
int node_put_attr(bim_node_t *n, const char *name, char *v) {
  void *np;
  for (int i = 0; i < n->nattrs; i++) {
    if (strcmp(n->attrs[i].name, name)) continue;
    free(n->attrs[i].value);
    n->attrs[i].value = v;
    return 0;
  }
  np = realloc(n->attrs, sizeof *n->attrs * (size_t)(n->nattrs + 1));
  if (!np) {
    free(v);
    return -1;
  }
  n->attrs = np;
  n->attrs[n->nattrs].name = name;
  n->attrs[n->nattrs].value = v;
  n->nattrs++;
  return 0;
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
    if (!strcmp(n->kids[i]->name, name)) return n->kids[i];
  return NULL;
}
