/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/nodexml.h"
#include "lib/helper/strutil.h"
#include "lib/helper/growutil.h"

#define WBUF 4096

typedef struct {
  const char *uri;
  char *prefix; /* "" default namespace */
} ns_t;

typedef struct {
  const char *uri;
  const char *prefix;
} decl_t;

typedef struct {
  const bim_xml_opts_t *o;
  xml_put_fn put;
  void *ctx;
  ns_t *ns;
  int nns;
  int cns;
  decl_t *decl;
  int ndecl;
  int cdecl;
  int decl_done;
  int oom;
  const char *root_ns;
  size_t blen;
  char buf[WBUF];
} w_t;

static void flush(w_t *w) {
  if (w->blen) w->put(w->ctx, w->buf, w->blen);
  w->blen = 0;
}

static void emit(w_t *w, const char *s, size_t n) {
  if (n > WBUF - w->blen) flush(w);
  if (n >= WBUF) {
    w->put(w->ctx, s, n);
    return;
  }
  memcpy(w->buf + w->blen, s, n);
  w->blen += n;
}

static void wput(void *ctx, const char *s, size_t n) { emit(ctx, s, n); }

static void out(w_t *w, const char *s) { emit(w, s, strlen(s)); }

static void spaces(w_t *w, int n) {
  static const char sp[] = "                                ";
  while (n > 0) {
    int k = n < (int)sizeof sp - 1 ? n : (int)sizeof sp - 1;
    emit(w, sp, (size_t)k);
    n -= k;
  }
}

static int find(const w_t *w, const char *uri) {
  for (int i = 0; i < w->nns; i++)
    if (w->ns[i].uri == uri || !strcmp(w->ns[i].uri, uri)) return i;
  return -1;
}

static int used_prefix(const w_t *w, const char *p) {
  for (int i = 0; i < w->nns; i++)
    if (w->ns[i].prefix && !strcmp(w->ns[i].prefix, p)) return 1;
  return 0;
}

static void decls_of(w_t *w, const bim_node_t *n) {
  for (int i = 0; i < n->nnsdecls && !w->oom; i++) {
    int k = 0;
    if (!*n->nsdecls[i].prefix) continue;
    while (k < w->ndecl && strcmp(w->decl[k].uri, n->nsdecls[i].uri)) k++;
    if (k < w->ndecl) continue;
    if (array_reserve((void **)&w->decl, &w->cdecl, w->ndecl + 1, sizeof *w->decl)) {
      w->oom = 1;
      return;
    }
    w->decl[w->ndecl++] = (decl_t){n->nsdecls[i].uri, n->nsdecls[i].prefix};
  }
  for (int i = 0; i < n->nkids; i++) decls_of(w, n->kids[i]);
}

/* prefix declared for uri in subtree, NULL none */
static const char *declared(w_t *w, const bim_node_t *root, const char *uri) {
  if (!w->decl_done) {
    decls_of(w, root);
    w->decl_done = 1;
  }
  for (int i = 0; i < w->ndecl; i++)
    if (!strcmp(w->decl[i].uri, uri)) return w->decl[i].prefix;
  return NULL;
}

/* nsN not taken yet */
static void gen_prefix(const w_t *w, char *gen) {
  for (unsigned k = (unsigned)w->nns;; k++) {
    gen[0] = 'n';
    gen[1] = 's';
    uint_to_str(gen + 2, k);
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
      gen_prefix(w, gen);
      free(e->prefix);
      e->prefix = strdup(gen);
      w->oom = w->oom || !e->prefix;
    }
    return;
  }
  for (int i = 0; i < w->o->nprefixes && !pref; i++)
    if (!strcmp(w->o->prefixes[i].uri, uri) && *w->o->prefixes[i].prefix && !used_prefix(w, w->o->prefixes[i].prefix)) pref = w->o->prefixes[i].prefix;
  if (!pref) {
    pref = declared(w, root, uri);
    if (pref && used_prefix(w, pref)) pref = NULL;
  }
  if (!pref && !strcmp(uri, XSI_NS) && !used_prefix(w, "xsi")) pref = "xsi";
  if (!pref && !for_attr && w->root_ns && !strcmp(w->root_ns, uri) && !used_prefix(w, "")) pref = "";
  if (!pref) {
    gen_prefix(w, gen);
    pref = gen;
  }
  if (array_reserve((void **)&w->ns, &w->cns, w->nns + 1, sizeof *w->ns)) {
    w->oom = 1;
    return;
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

static void esc(w_t *w, const char *s) { xml_escape_to(s, wput, w); }

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
    spaces(w, depth * w->o->indent);
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
    spaces(w, depth * w->o->indent);
  }
  out(w, "</");
  qname(w, n->ns, n->name);
  out(w, ">");
}

int bim_node_write_xml(const bim_node_t *root, const bim_xml_opts_t *o, xml_put_fn put, void *ctx) {
  static const bim_xml_opts_t none;
  w_t *w = calloc(1, sizeof *w);
  int rc;
  if (!w) return -1;
  w->o = o ? o : &none;
  w->put = put;
  w->ctx = ctx;
  w->root_ns = root->ns && *root->ns ? root->ns : NULL;
  collect(w, root, root);
  if (!w->oom) {
    if (w->o->declaration) out(w, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    write_node(w, root, 0, 1, 1, NULL);
    if (w->o->indent > 0 || w->o->declaration) out(w, "\n");
    flush(w);
  }
  rc = w->oom ? -1 : 0;
  for (int i = 0; i < w->nns; i++) free(w->ns[i].prefix);
  free(w->ns);
  free(w->decl);
  free(w);
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
