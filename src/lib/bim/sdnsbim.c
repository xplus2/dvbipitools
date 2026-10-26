/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/sdnsbim.h"
#include "lib/bim/tvainit.h"
#include "lib/bim/xsd/xsd.h"

int bim_sdnsbim_root(const void *ctx, unsigned code, bim_tvaroot_t *out) {
  const bim_sdnsroots_t *r = ctx;
  const bim_elem_t *e = code == BIM_SDNS_CONTEXT_PATH ? xsd_compiled_elem(r->c, r->ns, "ServiceDiscovery") : NULL;
  if (!e) return -1;
  *out = (bim_tvaroot_t){e->name, e->ns, e->type};
  return 0;
}

int bim_sdnsbim_decoderinit(const xsd_compiled_t *c, const char *ns, bim_decoderinit_t *di) {
  bim_di_codec_t *z;
  uint64_t *codes;
  size_t n = 0;
  if (bim_tvainit_decoderinit(ns, NULL, 0, di)) return -1;
  codes = calloc((size_t)c->schema.ntypes + 1, sizeof *codes);
  z = calloc(1, sizeof *z);
  if (!codes || !z) {
    free(codes);
    free(z);
    bim_tvainit_decoderinit_free(di);
    return -1;
  }
  for (int i = 1; i < c->schema.ntypes; i++) {
    const bim_type_t *t = c->schema.types[i];
    if (t->content == BIM_CONTENT_SIMPLE && t->simple && t->simple->codec == BIM_ZSTRING) codes[n++] = (uint64_t)i - 1;
  }
  z->uri = (char *)BIM_SDNS_ZLIB_CODEC;
  z->types = codes;
  z->ntypes = n;
  di->schemas[0].codecs = z;
  di->schemas[0].ncodecs = n ? 1 : 0;
  if (!n) {
    free(codes);
    free(z);
    di->schemas[0].codecs = NULL;
  }
  return 0;
}

void bim_sdnsbim_decoderinit_free(bim_decoderinit_t *di) {
  for (size_t i = 0; i < di->nschemas; i++) {
    for (size_t k = 0; k < di->schemas[i].ncodecs; k++) free(di->schemas[i].codecs[k].types);
    free(di->schemas[i].codecs);
  }
  bim_tvainit_decoderinit_free(di);
}

int bim_sdnsdoc_open(bim_sdnsdoc_t *d, const char *const *paths, int npaths, const char *ns, const char **culprit) {
  xsd_compile_opts_t co = {.mpegb = 1, .zlib = 1};
  char tns[sizeof d->ns];
  int rc;
  memset(d, 0, sizeof *d);
  if (!(d->set = xsd_set_new())) return -2;
  rc = xsd_load_for_ns(d->set, NULL, paths, npaths, ns, culprit, tns, sizeof tns);
  if ((rc == XSD_LOAD_NONE && !ns) || (!rc && !ns && !tns[0])) rc = -3;
  if (!rc && xsd_compile(d->set, &co, &d->c)) rc = -1;
  if (rc) return rc;
  snprintf(d->ns, sizeof d->ns, "%s", ns ? ns : tns);
  return 0;
}

void bim_sdnsdoc_close(bim_sdnsdoc_t *d) {
  xsd_set_free(d->set);
  memset(d, 0, sizeof *d);
}

int bim_sdnsdoc_to_xml(const bim_sdnsdoc_t *d, const unsigned char *au, size_t len, char **xml, size_t *xml_len) {
  bim_sdnsroots_t r = {d->c, d->ns};
  bim_decode_opts_t o;
  bim_xml_opts_t xo = {.prefixes = d->prefixes, .nprefixes = d->nprefixes, .declaration = 1};
  bim_err_t err;
  bitreader_t br;
  dvbtva_fuu_t *f = NULL;
  size_t n = 0;
  int rc = -1;
  memset(&o, 0, sizeof o);
  o.schemas = &d->c->schema;
  o.nschemas = 1;
  o.mpegb = 1;
  o.lax_schema = -1;
  o.inflate = d->inflate;
  bitreader_init(&br, au, len);
  if (dvbtva_au_parse(bim_sdnsbim_root, &r, &o, &br, NULL, NULL, 0, &f, &n, &err)) return -1;
  if (n == 1 && !bim_node_to_xml(f[0].root, &xo, xml, xml_len)) rc = 0;
  dvbtva_au_free(f, n);
  return rc;
}

int bim_sdnsdoc_segment_xml(const bim_sdnsdoc_t *d, unsigned compr, const unsigned char *data, size_t len, const char **xml, size_t *xml_len, char **owned) {
  *owned = NULL;
  if (compr > 1) return -2;
  if (compr == 1) {
    if (bim_sdnsdoc_to_xml(d, data, len, owned, xml_len)) return -1;
    *xml = *owned;
    return 0;
  }
  *xml = (const char *)data;
  *xml_len = len;
  return 0;
}
