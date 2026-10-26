/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/nodexml.h"
#include "lib/sys/ioutil.h"


typedef struct {
  const char *uri;
  char *prefix; /* "" default namespace */
} ns_t;

typedef struct {
  const bim_xml_opts_t *o;
  xml_put_fn put;
  void *ctx;
  ns_t *ns;
  int nns;
  int cns;
  int oom;
  const char *root_ns;
} w_t;

static void out(w_t *w, const char *s) { w->put(w->ctx, s, strlen(s)); }

static int find(const w_t *w, const char *uri) {
  for (int i = 0; i < w->nns; i++)
    if (!strcmp(w->ns[i].uri, uri)) return i;
  return -1;
}

static int used_prefix(const w_t *w, const char *p) {
  for (int i = 0; i < w->nns; i++)
    if (w->ns[i].prefix && !strcmp(w->ns[i].prefix, p)) return 1;
  return 0;
}

/* prefix declared for uri in subtree, NULL none */
static const char *declared(const bim_node_t *n, const char *uri) {
  for (int i = 0; i < n->nnsdecls; i++)
    if (!strcmp(n->nsdecls[i].uri, uri) && *n->nsdecls[i].prefix) return n->nsdecls[i].prefix;
  for (int i = 0; i < n->nkids; i++) {
    const char *p = declared(n->kids[i], uri);
    if (p) return p;
  }
  return NULL;
}

/* nsN not taken yet */
static void gen_prefix(const w_t *w, char *gen, size_t cap) {
  for (int k = w->nns;; k++) {
    snprintf(gen, cap, "ns%d", k);
    if (!used_prefix(w, gen)) return;
  }
}

static void add_ns(w_t *w, const bim_node_t *root, const char *uri, int for_attr) {
  const char *pref = NULL;
  char gen[24];
  int idx;
  if (!uri || !*uri || w->oom || !strcmp(uri, XML_NS)) return;
  idx = find(w, uri);
  if (idx >= 0) {
    ns_t *e = &w->ns[idx];
    if (for_attr && !*e->prefix) {
      /* default namespace does not apply to attributes */
      gen_prefix(w, gen, sizeof gen);
      free(e->prefix);
      e->prefix = strdup(gen);
      w->oom = w->oom || !e->prefix;
    }
    return;
  }
  for (int i = 0; i < w->o->nprefixes && !pref; i++)
    if (!strcmp(w->o->prefixes[i].uri, uri) && *w->o->prefixes[i].prefix && !used_prefix(w, w->o->prefixes[i].prefix)) pref = w->o->prefixes[i].prefix;
  if (!pref) {
    pref = declared(root, uri);
    if (pref && used_prefix(w, pref)) pref = NULL;
  }
  if (!pref && !strcmp(uri, XSI_NS) && !used_prefix(w, "xsi")) pref = "xsi";
  if (!pref && !for_attr && w->root_ns && !strcmp(w->root_ns, uri) && !used_prefix(w, "")) pref = "";
  if (!pref) {
    gen_prefix(w, gen, sizeof gen);
    pref = gen;
  }
  if (w->nns == w->cns) {
    ns_t *n = realloc(w->ns, (size_t)(w->cns ? w->cns * 2 : 8) * sizeof *n);
    if (!n) {
      w->oom = 1;
      return;
    }
    w->ns = n;
    w->cns = w->cns ? w->cns * 2 : 8;
  }
  if (!w->ns) {
    w->oom = 1;
    return;
  }
  idx = w->nns++;
  w->ns[idx].uri = uri;
  w->ns[idx].prefix = strdup(pref);
  if (!w->ns[idx].prefix) w->oom = 1;
}

static void collect(w_t *w, const bim_node_t *root, const bim_node_t *n) {
  if (n->name) add_ns(w, root, n->ns, 0);
  for (int i = 0; i < n->nattrs; i++) add_ns(w, root, n->attrs[i].ns, 1);
  if (n->cast) {
    add_ns(w, root, XSI_NS, 1);
    add_ns(w, root, n->cast->ns, 1);
  }
  for (int i = 0; i < n->nkids; i++) collect(w, root, n->kids[i]);
}

static void qname(w_t *w, const char *uri, const char *name) {
  int i = uri && *uri ? find(w, uri) : -1;
  if (uri && !strcmp(uri, XML_NS)) {
    out(w, "xml:");
  } else if (i >= 0 && *w->ns[i].prefix) {
    out(w, w->ns[i].prefix);
    out(w, ":");
  }
  out(w, name);
}

static void esc(w_t *w, const char *s) { xml_escape_to(s, w->put, w->ctx); }

static int has_text(const bim_node_t *n) {
  if (n->text) return 1;
  for (int i = 0; i < n->nkids; i++)
    if (!n->kids[i]->name) return 1;
  return 0;
}

/* dflt: default namespace in scope, NULL none */
static void write_node(w_t *w, const bim_node_t *n, int depth, int root, int pretty, const char *dflt) {
  int mixed;
  int ind = w->o->indent > 0 && pretty;
  int i = n->ns && *n->ns ? find(w, n->ns) : -1;
  if (!n->name) {
    esc(w, n->text ? n->text : "");
    return;
  }
  if (ind && depth) {
    out(w, "\n");
    for (int k = 0; k < depth * w->o->indent; k++) out(w, " ");
  }
  out(w, "<");
  qname(w, n->ns, n->name);
  if (root) {
    for (int k = 0; k < w->nns; k++) {
      out(w, *w->ns[k].prefix ? " xmlns:" : " xmlns");
      out(w, w->ns[k].prefix);
      out(w, "=\"");
      esc(w, w->ns[k].uri);
      out(w, "\"");
      if (!*w->ns[k].prefix) dflt = w->ns[k].uri;
    }
  } else if (i < 0 && dflt) {
    out(w, " xmlns=\"\"");
    dflt = NULL;
  } else if (i >= 0 && !*w->ns[i].prefix && dflt != w->ns[i].uri) {
    out(w, " xmlns=\"");
    esc(w, w->ns[i].uri);
    out(w, "\"");
    dflt = w->ns[i].uri;
  }
  for (int k = 0; k < n->nattrs; k++) {
    out(w, " ");
    qname(w, n->attrs[k].ns, n->attrs[k].name);
    out(w, "=\"");
    esc(w, n->attrs[k].value);
    out(w, "\"");
  }
  if (n->cast) {
    out(w, " ");
    qname(w, XSI_NS, "type");
    out(w, "=\"");
    qname(w, n->cast->ns, n->cast->name ? n->cast->name : "");
    out(w, "\"");
  }
  mixed = has_text(n);
  if (!n->nkids && !n->text) {
    out(w, "/>");
    return;
  }
  out(w, ">");
  if (n->text) esc(w, n->text);
  for (int k = 0; k < n->nkids; k++) write_node(w, n->kids[k], depth + 1, 0, pretty && !mixed, dflt);
  if (ind && !mixed && n->nkids) {
    out(w, "\n");
    for (int k = 0; k < depth * w->o->indent; k++) out(w, " ");
  }
  out(w, "</");
  qname(w, n->ns, n->name);
  out(w, ">");
}

int bim_node_write_xml(const bim_node_t *root, const bim_xml_opts_t *o, xml_put_fn put, void *ctx) {
  static const bim_xml_opts_t none;
  w_t w;
  int rc;
  memset(&w, 0, sizeof w);
  w.o = o ? o : &none;
  w.put = put;
  w.ctx = ctx;
  w.root_ns = root->ns && *root->ns ? root->ns : NULL;
  collect(&w, root, root);
  if (!w.oom) {
    if (w.o->declaration) out(&w, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    write_node(&w, root, 0, 1, 1, NULL);
    if (w.o->indent > 0 || w.o->declaration) out(&w, "\n");
  }
  rc = w.oom ? -1 : 0;
  for (int i = 0; i < w.nns; i++) free(w.ns[i].prefix);
  free(w.ns);
  return rc;
}

static void sink(void *ctx, const char *s, size_t n) { dstrbuf_add_n(ctx, s, n); }

int bim_node_to_xml(const bim_node_t *root, const bim_xml_opts_t *o, char **out_buf, size_t *len) {
  dstrbuf_t b;
  dstrbuf_init(&b);
  if (bim_node_write_xml(root, o, sink, &b) || !b.buf) {
    free(b.buf);
    return -1;
  }
  *out_buf = b.buf;
  if (len) *len = b.len;
  return 0;
}
