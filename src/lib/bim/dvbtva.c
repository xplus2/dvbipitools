/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/dvbtva.h"
#include "lib/helper/log.h"

static int fail(bim_err_t *err, const char *what) { return bim_fail(err, BIM_DVBTVA_ERR, "bim: DVB-TVA %s", what); }

int dvbtva_tva_root(void *ctx, unsigned code, bim_tvaroot_t *out) {
  const dvbtva_roots_t *r = ctx;
  return bim_tvafrag_root(r->c, r->ns, code, out);
}

int dvbtva_au_write(dvbtva_root_fn root, void *rctx, const bim_encode_opts_t *eo, const dvbtva_fuu_t *f, size_t n,
  bitwriter_t *bw, strrepo_writer_t *sw, bim_fragref_t *refs, bim_err_t *err) {
  return dvbtva_au_write_ex(root, rctx, eo, f, n, bw, sw, refs, NULL, NULL, err);
}

int dvbtva_au_write_ex(dvbtva_root_fn root, void *rctx, const bim_encode_opts_t *eo, const dvbtva_fuu_t *f, size_t n,
  bitwriter_t *bw, strrepo_writer_t *sw, bim_fragref_t *refs, dvbtva_wpost_fn post, void *pctx, bim_err_t *err) {
  bim_wr_t w = {bw, err};
  memset(err, 0, sizeof *err);
  if (bim_wr_v8(&w, n)) return -1;
  for (size_t i = 0; i < n; i++) {
    bim_tvaroot_t r;
    bim_encode_opts_t o;
    bitwriter_t fb;
    const unsigned char *p;
    size_t len;
    int rc;
    if (f[i].code > 0xFFFF || root(rctx, f[i].code, &r)) return fail(err, "context path unknown");
    if (refs) {
      refs[i].au_off = (uint32_t)bw->len;
      refs[i].str_off = (uint32_t)sw->len;
    }
    bitwriter_init(&fb);
    o = *eo;
    o.cast |= bim_tree_has_cast(f[i].root);
    rc = bim_encode_payload_opts(r.type, f[i].root, &o, &fb, sw);
    if (!rc && post && post(pctx, i, sw)) rc = -1;
    p = rc ? NULL : bitwriter_data(&fb, &len);
    rc = rc ? fail(err, "fragment not encodable") : bim_wr_v8(&w, len) || bim_wr_put(&w, f[i].code, 16) || bim_wr_bytes(&w, p, len);
    bitwriter_free(&fb);
    if (rc) return -1;
  }
  return 0;
}

void dvbtva_au_free(dvbtva_fuu_t *f, size_t n) {
  for (size_t i = 0; f && i < n; i++) bim_node_free(f[i].root);
  free(f);
}

int dvbtva_au_parse(dvbtva_root_fn root, void *rctx, const bim_decode_opts_t *o, bitreader_t *br, strrepo_reader_t *sr, const bim_fragref_t *refs, size_t nrefs,
                    dvbtva_fuu_t **out, size_t *n, bim_err_t *err) {
  return dvbtva_au_parse_ex(root, rctx, o, br, sr, refs, nrefs, NULL, NULL, out, n, err);
}

static int ref_off_cmp(const void *a, const void *b) {
  uint32_t x = ((const bim_fragref_t *)a)->au_off;
  uint32_t y = ((const bim_fragref_t *)b)->au_off;
  return (x > y) - (x < y);
}

/* first of refs sorted by au_off with au_off at, n if none */
static size_t ref_at(const bim_fragref_t *refs, size_t n, size_t at) {
  size_t lo = 0;
  size_t hi = n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (refs[mid].au_off < at) lo = mid + 1;
    else hi = mid;
  }
  return lo < n && refs[lo].au_off == at ? lo : n;
}

int dvbtva_au_parse_ex(dvbtva_root_fn root, void *rctx, const bim_decode_opts_t *o, bitreader_t *br, strrepo_reader_t *sr, const bim_fragref_t *refs_in, size_t nrefs,
                       dvbtva_rpost_fn post, void *pctx, dvbtva_fuu_t **out, size_t *n, bim_err_t *err) {
  const unsigned char *base = br->buf;
  dvbtva_fuu_t *f;
  bim_fragref_t *refs = NULL;
  uint64_t nf;
  size_t k = 0;
  int lost = 0; /* strings of a skipped fragment not consumed */
  int rc = -1;
  memset(err, 0, sizeof *err);
  *out = NULL;
  *n = 0;
  if (bitreader_get_vluimsbf8(br, &nf) || nf > bitreader_bits_left(br) / 24) return fail(err, "access unit malformed");
  if (sr && nrefs) {
    if (!(refs = malloc(sizeof *refs * nrefs))) return bim_oom(err);
    memcpy(refs, refs_in, sizeof *refs * nrefs);
    qsort(refs, nrefs, sizeof *refs, ref_off_cmp);
  }
  if (!(f = calloc((size_t)nf + 1, sizeof *f))) {
    free(refs);
    return bim_oom(err);
  }
  for (uint64_t i = 0; i < nf; i++) {
    uint64_t len;
    uint64_t code;
    size_t start = br->byte_pos;
    size_t off;
    size_t spos;
    bim_tvaroot_t r;
    bitreader_t sub;
    if (bitreader_fuu_header(br, &len, &code)) {
      fail(err, "access unit malformed");
      goto out;
    }
    off = br->byte_pos;
    bitreader_skip(br, (size_t)len * 8);
    if (root(rctx, (unsigned)code, &r)) {
      lost = 1;
      continue;
    }
    for (size_t j = refs ? ref_at(refs, nrefs, start) : nrefs; j < nrefs && refs[j].au_off == start; j++) {
      if (strrepo_reader_seek(sr, refs[j].str_off)) {
        fail(err, "string offset malformed");
        goto out;
      }
      lost = 0;
    }
    spos = sr ? sr->pos : 0;
    bitreader_init(&sub, base + off, (size_t)len);
    f[k].code = (unsigned)code;
    if (bim_decode_payload(r.type, r.name, o, &sub, sr, &f[k].root)) {
      fail(err, "fragment malformed");
      goto out;
    }
    if (r.ns && !(f[k].root->ns = bim_node_keep(f[k].root, r.ns))) {
      bim_node_free(f[k].root);
      f[k].root = NULL;
      bim_oom(err);
      goto out;
    }
    if (post && post(pctx, k, &f[k], sr)) {
      k++;
      fail(err, "fragment strings malformed");
      goto out;
    }
    k++;
    if (lost && sr && sr->pos != spos) {
      fail(err, "fragment strings unknown after skipped fragment");
      goto out;
    }
  }
  *out = f;
  *n = k;
  f = NULL;
  rc = 0;
out:
  if (f) dvbtva_au_free(f, k);
  free(refs);
  return rc;
}

int dvbtva_codecs(const bim_tvans_t *ns, xsd_codec_map_t out[DVBTVA_CODECS]) {
  int n = 0;
  out[n++] = (xsd_codec_map_t){XSD_NS, "string", BIM_STRING, 1};
  out[n++] = (xsd_codec_map_t){XSD_NS, "anyURI", BIM_LOCATOR, 1};
  out[n++] = (xsd_codec_map_t){XSD_NS, "dateTime", BIM_DATETIME, 1};
  out[n++] = (xsd_codec_map_t){XSD_NS, "duration", BIM_DURATION, 1};
  if (ns->tva) out[n++] = (xsd_codec_map_t){ns->tva, "termReferenceType", BIM_TERMREF, 1};
  return n;
}

int dvbtva_schemas_init(dvbtva_schemas_t *s, const xsd_compiled_t *c, size_t n, const xsd_compiled_t *const *known) {
  if (!n || n > DVBTVA_SCHEMAS) return -1;
  memset(s, 0, sizeof *s);
  s->n = (int)n;
  s->v[0] = c->schema;
  for (size_t k = 1; k < n; k++)
    if (known && known[k]) s->v[k] = known[k]->schema;
  return 0;
}

int dvbtva_extras_load(xsd_set_t **sets, const xsd_compiled_t **known, const char *const *uris, size_t n, const char *const *paths, int npaths, xsd_index_t *x,
                       int enc, const dvbtva_extra_codes_t *codes, dvbtva_extra_compile_t fn, void *arg) {
  for (size_t k = 1; k < n; k++) {
    const char *culprit = NULL;
    int rc;
    if (!(sets[k] = xsd_set_new())) {
      log_code(codes->oom, "out of memory");
      return -1;
    }
    rc = xsd_load_extra(sets[k], x, paths, npaths, uris[k], &culprit);
    if (rc == XSD_LOAD_NONE) {
      if (!enc && !xsd_helper_ns(uris[k])) log_code(codes->warn, "schema %s of the DecoderInit not found, its chunks are skipped", uris[k]);
      continue;
    }
    if (rc || (rc = fn(sets[k], &known[k], arg))) {
      xsd_load_report(codes->start, codes->run, rc, culprit, uris[k], sets[k]);
      return -1;
    }
  }
  return 0;
}

size_t dvbtva_schemas_pack(const xsd_compiled_t **known, const char **uris, size_t n) {
  size_t m = 1;
  for (size_t k = 1; k < n; k++)
    if (known[k]) {
      known[m] = known[k];
      uris[m++] = uris[k];
    }
  return m;
}

int dvbtva_cast_bind(const xsd_compiled_t *c, const xsd_compiled_t *const *known, size_t n, bim_node_t *root, const char **bad) {
  const xsd_compiled_t *cs[DVBTVA_SCHEMAS] = {c};
  size_t m = 1;
  for (size_t k = 1; k < n && k < DVBTVA_SCHEMAS; k++)
    if (known[k]) cs[m++] = known[k];
  return xsd_cast_bind(cs, m, root, bad);
}

int dvbtva_compile(xsd_set_t *s, const char *tva_ns, const xsd_compiled_t **c) {
  xsd_codec_map_t codecs[DVBTVA_CODECS];
  bim_tvans_t ns = {.tva = tva_ns};
  xsd_compile_opts_t o = {.codecs = codecs};
  o.ncodecs = dvbtva_codecs(&ns, codecs);
  return xsd_compile(s, &o, c);
}
