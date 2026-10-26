/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/bim/decoderinit/priv.h"

typedef struct {
  bim_wr_t w;
  const bim_decoderinit_t *di;
  bim_di_counts_fn counts;
  void *ctx;
} wr_t;

static int range(wr_t *w, const char *what) { return bim_fail(w->w.err, BIM_DI_RANGE, "bim: DecoderInit %s out of range", what); }

static int flags(wr_t *w, const char *a, const char *b) { return bim_fail(w->w.err, BIM_DI_FLAGS, "bim: DecoderInit %s requires %s", a, b); }

static int fits(uint64_t v, int bits) { return bits >= 64 || v < (1ull << bits); }

static int w_codecs(wr_t *w, const bim_di_schema_t *s) {
  if (bim_wr_v8(&w->w, s->ncodecs)) return -1;
  for (size_t i = 0; i < s->ncodecs; i++) {
    const bim_di_codec_t *c = &s->codecs[i];
    if (bim_wr_str(&w->w, w->di->mpegb ? NULL : BIM_DI_ZERO, "DecoderInit TypeCodecURI_Length", c->uri) || bim_wr_v8(&w->w, c->ntypes)) return -1;
    for (size_t j = 0; j < c->ntypes; j++)
      if (bim_wr_v8(&w->w, c->types[j])) return -1;
  }
  return 0;
}

static int w_ext(wr_t *w, const char *uri, int elems, const bim_ext_t *x) {
  uint64_t nt = 0;
  uint64_t ne = 0;
  if (x->present && !x->all && (!w->counts || w->counts(w->ctx, uri, &nt, &ne)))
    return bim_fail(w->w.err, BIM_DI_COUNTS, "bim: DecoderInit %s count of %s unknown", elems ? "global element" : "global type", uri ? uri : "initial schema");
  return bim_wr_ext(&w->w, elems ? ne : nt, BIM_DI_RANGE, x);
}

static int w_features(wr_t *w) {
  const bim_decoderinit_t *di = w->di;
  int f[] = {di->insert, di->aod, di->add_schema, di->add_schema_only, di->frag_ref, di->mpc_only, di->hier, di->cpt, di->prefix_table};
  bitwriter_t pt;
  bim_wr_t t = {&pt, w->w.err};
  size_t bits;
  if (di->add_schema_only && !di->add_schema) return flags(w, "AdditionalSchemaUpdatesOnlyFlag", "AdditionalSchemaFlag");
  if (di->add_schema && !di->hier) return flags(w, "AdditionalSchemaFlag", "HierarchyBasedSubstitutionCodingFlag");
  bitwriter_init(&pt);
  if (di->prefix_table) {
    if (bim_wr_v8(&t, di->nprefixes)) goto fail;
    for (size_t i = 0; i < di->nprefixes; i++)
      if (bim_wr_str(&t, BIM_DI_ZERO, "DecoderInit SchemaURI_Length", di->prefixes[i].uri) || bim_wr_str(&t, NULL, NULL, di->prefixes[i].prefix)) goto fail;
  }
  bits = FLAG_BITS + pt.len * 8;
  if (bim_wr_v8(&w->w, (bits + 7) / 8)) goto fail;
  for (size_t i = 0; i < sizeof f / sizeof f[0]; i++)
    if (bim_wr_put(&w->w, (uint64_t)!!f[i], 1)) goto fail;
  if (bim_wr_put(&w->w, 0, FLAG_BITS - 9)) goto fail;
  if (bim_wr_sub(&w->w, &pt)) return -1;
  return bim_wr_put(&w->w, 0, (int)((8 - bits % 8) % 8));
fail:
  bitwriter_free(&pt);
  return -1;
}

static int w_cpt(wr_t *w) {
  const bim_decoderinit_t *di = w->di;
  bitwriter_t sub;
  bim_wr_t t = {&sub, w->w.err};
  if (di->cpt_code_len > 64) return range(w, "ContextPathCode_Length");
  bitwriter_init(&sub);
  if (bim_wr_v8(&t, di->cpt_code_len) || bim_wr_v8(&t, di->ncpaths) || bim_wr_put(&t, (uint64_t)!!di->cpt_complete, 1)) goto fail;
  for (size_t i = 0; i < di->ncpaths; i++) {
    const bim_di_cpath_t *p = &di->cpaths[i];
    bim_bits_t b = {p->bits, p->nbits};
    if (bim_wr_v5(&t, p->nbits) || bim_wr_bits(&t, &b)) goto fail;
    if (!di->cpt_complete) {
      if (!p->code || !fits(p->code, (int)di->cpt_code_len)) {
        range(w, "ContextPathCode");
        goto fail;
      }
      if (bim_wr_put(&t, p->code, (int)di->cpt_code_len)) goto fail;
    }
  }
  if (bitwriter_flush(&sub)) {
    bim_oom(w->w.err);
    goto fail;
  }
  if (bim_wr_v8(&w->w, sub.len)) goto fail;
  return bim_wr_sub(&w->w, &sub);
fail:
  bitwriter_free(&sub);
  return -1;
}

static int w_instances(wr_t *w) {
  const bim_decoderinit_t *di = w->di;
  int tw = bim_width(di->naod_types);
  if (bim_wr_v8(&w->w, di->ninstances)) return -1;
  for (size_t i = 0; i < di->ninstances; i++) {
    const bim_di_instance_t *in = &di->instances[i];
    bitreader_t sub;
    uint64_t type;
    bitreader_init(&sub, in->raw, in->nraw);
    if (bitreader_get(&sub, tw, &type) || type != in->type || type >= di->naod_types) return range(w, "AdvancedOptimisedDecoderInstances[i].Type");
    if (bim_wr_v8(&w->w, in->nraw) || bim_wr_bytes(&w->w, in->raw, in->nraw)) return -1;
  }
  return 0;
}

static int w_mappings(wr_t *w) {
  const bim_decoderinit_t *di = w->di;
  int iw = bim_width(di->ninstances);
  uint64_t nall = bim_di_nall(di);
  if (bim_wr_v8(&w->w, di->nmappings)) return -1;
  for (size_t k = 0; k < di->nmappings; k++) {
    const bim_di_mapping_t *m = &di->mappings[k];
    if (m->reserved > 127) return range(w, "Reserved");
    if (bim_wr_put(&w->w, (uint64_t)!!m->preserve_default, 1) || bim_wr_put(&w->w, m->reserved, 7) || bim_wr_v8(&w->w, m->ninstances)) return -1;
    for (size_t i = 0; i < m->ninstances; i++) {
      if (m->instances[i] >= di->ninstances) return range(w, "AdvancedOptimisedDecoderInstanceID");
      if (bim_wr_put(&w->w, m->instances[i], iw)) return -1;
    }
    if (bim_wr_v8(&w->w, m->ntypes)) return -1;
    for (size_t i = 0; i < m->ntypes; i++) {
      if (m->types[i].schema >= nall) return range(w, "SchemaID");
      if (bim_wr_put(&w->w, m->types[i].schema, bim_width(nall)) || bim_wr_v8(&w->w, m->types[i].type)) return -1;
    }
  }
  return 0;
}

static int w_aod(wr_t *w) {
  const bim_decoderinit_t *di = w->di;
  if (bim_wr_v8(&w->w, di->naod_types)) return -1;
  for (size_t i = 0; i < di->naod_types; i++)
    if (bim_wr_str(&w->w, NULL, NULL, di->aod_types[i])) return -1;
  return w_instances(w) || w_mappings(w);
}

int bim_di_aod_config_write(bitwriter_t *bw, const bim_decoderinit_t *di, const bim_decoderinit_t *cfg, bim_err_t *err) {
  bim_decoderinit_t t = *cfg;
  wr_t w = {{bw, err}, &t, NULL, NULL};
  memset(err, 0, sizeof *err);
  t.nschemas = di->nschemas;
  t.add_schema = di->add_schema;
  t.nadditional = di->nadditional;
  t.naod_types = di->naod_types;
  return w_instances(&w) || w_mappings(&w) ? -1 : 0;
}

static int w_additional(wr_t *w) {
  const bim_decoderinit_t *di = w->di;
  uint64_t end = bim_di_nall(di);
  if (bim_wr_v8(&w->w, di->nadditional) || bim_wr_v8(&w->w, di->nknown)) return -1;
  for (size_t i = 0; i < di->nknown; i++) {
    const bim_di_schema_t *s = &di->known[i];
    if (s->id <= di->nschemas || s->id + 1 >= end) return range(w, "KnownAdditionalSchemaID");
    if (bim_wr_v8(&w->w, s->id) || bim_wr_str(&w->w, BIM_DI_ZERO, "DecoderInit AdditionalSchemaURI_Length", s->uri) || bim_wr_str(&w->w, NULL, NULL, s->hint) ||
        w_codecs(w, s) || w_ext(w, s->uri, 0, &s->castable) || w_ext(w, s->uri, 1, &s->substitutable))
      return -1;
  }
  if (di->schema_encoding > 255) return range(w, "SchemaEncodingMethod");
  return bim_wr_put(&w->w, di->schema_encoding, 8) || w_ext(w, NULL, 0, &di->castable) || w_ext(w, NULL, 1, &di->substitutable) || bim_wr_put(&w->w, 0, 7);
}

static int w_all(wr_t *w) {
  const bim_decoderinit_t *di = w->di;
  int adv = di->insert || di->aod || di->add_schema || di->add_schema_only || di->frag_ref || di->mpc_only || di->hier || di->cpt || di->prefix_table;
  if (di->unit_size_code > 7 || di->reserved >= (di->mpegb ? 16u : 32u)) return range(w, "UnitSizeCode/ReservedBits");
  if (adv && (!di->mpegb || di->no_advanced)) return flags(w, "advanced features", "ISO 23001-1");
  if (bim_wr_v8(&w->w, di->profile) || bim_wr_put(&w->w, di->unit_size_code, 3)) return -1;
  if (di->mpegb) {
    if (bim_wr_put(&w->w, (uint64_t)!!di->no_advanced, 1) || bim_wr_put(&w->w, di->reserved, 4)) return -1;
    if (!di->no_advanced && w_features(w)) return -1;
  } else if (bim_wr_put(&w->w, di->reserved, 5)) {
    return -1;
  }
  if (!di->add_schema_only) {
    if (!di->nschemas) return bim_fail(w->w.err, BIM_DI_ZERO, "bim: DecoderInit %s zero", "NumberOfSchemas");
    if (bim_wr_v8(&w->w, di->nschemas)) return -1;
    for (size_t k = 0; k < di->nschemas; k++)
      if (bim_wr_str(&w->w, BIM_DI_ZERO, "DecoderInit SchemaURI_Length", di->schemas[k].uri) || bim_wr_str(&w->w, NULL, NULL, di->schemas[k].hint) ||
          w_codecs(w, &di->schemas[k]))
        return -1;
    if (di->cpt && w_cpt(w)) return -1;
    if (di->aod && w_aod(w)) return -1;
    if (di->frag_ref) {
      if (di->nfrag_formats > 255) return range(w, "NumOfSupportedFragmentReferenceFormat");
      if (bim_wr_put(&w->w, di->nfrag_formats, 8) || bim_wr_bytes(&w->w, di->frag_formats, di->nfrag_formats)) return -1;
    }
  }
  if (di->add_schema && w_additional(w)) return -1;
  if (!di->add_schema_only && (bim_wr_v8(&w->w, di->ninitial) || bim_wr_bytes(&w->w, di->initial, di->ninitial))) return -1;
  return 0;
}

int bim_decoderinit_write(const bim_decoderinit_t *di, bim_di_counts_fn counts, void *ctx, bitwriter_t *bw, bim_err_t *err) {
  wr_t w = {{bw, err}, di, counts, ctx};
  memset(err, 0, sizeof *err);
  if (w_all(&w)) return -1;
  return bitwriter_flush(bw) ? bim_oom(err) : 0;
}
