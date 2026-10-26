/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/decoderinit/priv.h"

typedef struct {
  bim_rd_t r;
  bim_decoderinit_t *di;
  bim_di_counts_fn counts;
  void *ctx;
} rd_t;

uint64_t bim_di_nall(const bim_decoderinit_t *di) {
  uint64_t a = di->nschemas;
  uint64_t b = di->add_schema ? di->nadditional : 0;
  return a + b < a ? UINT64_MAX : a + b;
}

static int range(rd_t *r, const char *what) { return bim_fail(r->r.err, BIM_DI_RANGE, "bim: DecoderInit %s out of range", what); }

static void *alloc_n(size_t n, size_t sz) { return n ? calloc(n, sz) : NULL; }

static int rd_codecs(rd_t *r, bim_di_schema_t *s) {
  if (bim_rd_count(&r->r, 16, &s->ncodecs)) return -1;
  if (s->ncodecs && !(s->codecs = alloc_n(s->ncodecs, sizeof *s->codecs))) {
    s->ncodecs = 0;
    return bim_oom(r->r.err);
  }
  for (size_t i = 0; i < s->ncodecs; i++) {
    bim_di_codec_t *c = &s->codecs[i];
    /* ISO 15938-1:2002 Cor 1 7.2.3 */
    if (bim_rd_str(&r->r, r->di->mpegb ? NULL : BIM_DI_ZERO, "TypeCodecURI_Length", &c->uri) || bim_rd_count(&r->r, 8, &c->ntypes)) return -1;
    if (c->ntypes && !(c->types = alloc_n(c->ntypes, sizeof *c->types))) {
      c->ntypes = 0;
      return bim_oom(r->r.err);
    }
    for (size_t j = 0; j < c->ntypes; j++)
      if (bim_rd_v8(&r->r, &c->types[j])) return -1;
  }
  return 0;
}

/* uri NULL: initial schema (6.2.2). counts needed for code lists only */
static int rd_ext(rd_t *r, const char *uri, int elems, bim_ext_t *x) {
  uint64_t nt;
  uint64_t ne;
  bitreader_t peek = r->r.br;
  uint64_t present;
  uint64_t all;
  if (bitreader_get(&peek, 1, &present) || !present || bitreader_get(&peek, 1, &all) || all) return bim_rd_ext(&r->r, 0, BIM_DI_RANGE, x);
  if (!r->counts || r->counts(r->ctx, uri, &nt, &ne))
    return bim_fail(r->r.err, BIM_DI_COUNTS, "bim: DecoderInit %s count of %s unknown", elems ? "global element" : "global type", uri ? uri : "initial schema");
  return bim_rd_ext(&r->r, elems ? ne : nt, BIM_DI_RANGE, x);
}

static int rd_features(rd_t *r) {
  bim_decoderinit_t *di = r->di;
  int *flags[] = {&di->insert, &di->aod, &di->add_schema, &di->add_schema_only, &di->frag_ref, &di->mpc_only, &di->hier, &di->cpt, &di->prefix_table};
  uint64_t len;
  size_t start;
  size_t avail;
  if (bim_rd_v8(&r->r, &len)) return -1;
  if (len > bitreader_bits_left(&r->r.br) / 8) return bim_rd_trunc(&r->r);
  avail = (size_t)len * 8;
  start = bim_rd_pos(&r->r);
  /* flags beyond AdvancedFeatureFlags_Length absent (Cor 1) */
  for (size_t i = 0; i < sizeof flags / sizeof flags[0] && i < avail; i++)
    if (bim_rd_flag(&r->r, flags[i])) return -1;
  if (di->prefix_table) {
    if (avail < FLAG_BITS) return bim_fail(r->r.err, BIM_DI_LENGTH, "bim: DecoderInit %s mismatch", "AdvancedFeatureFlags_Length");
    if (bim_rd_zero(&r->r, FLAG_BITS - 9, BIM_DI_RESERVED) || bim_rd_count(&r->r, 16, &di->nprefixes)) return -1;
    if (di->nprefixes && !(di->prefixes = alloc_n(di->nprefixes, sizeof *di->prefixes))) {
      di->nprefixes = 0;
      return bim_oom(r->r.err);
    }
    for (size_t i = 0; i < di->nprefixes; i++)
      if (bim_rd_str(&r->r, BIM_DI_ZERO, "SchemaURI_Length", &di->prefixes[i].uri) || bim_rd_str(&r->r, NULL, NULL, &di->prefixes[i].prefix)) return -1;
    if (bim_rd_pos(&r->r) - start > avail) return bim_fail(r->r.err, BIM_DI_LENGTH, "bim: DecoderInit %s mismatch", "PrefixTable");
  }
  if (bim_rd_zero(&r->r, avail - (bim_rd_pos(&r->r) - start), BIM_DI_RESERVED)) return -1;
  if (di->add_schema_only && !di->add_schema)
    return bim_fail(r->r.err, BIM_DI_FLAGS, "bim: DecoderInit %s requires %s", "AdditionalSchemaUpdatesOnlyFlag", "AdditionalSchemaFlag");
  if (di->add_schema && !di->hier)
    return bim_fail(r->r.err, BIM_DI_FLAGS, "bim: DecoderInit %s requires %s", "AdditionalSchemaFlag", "HierarchyBasedSubstitutionCodingFlag");
  return 0;
}

static int rd_cpt(rd_t *r) {
  bim_decoderinit_t *di = r->di;
  uint64_t len;
  size_t start;
  size_t used;
  if (bim_rd_v8(&r->r, &len)) return -1;
  if (len > bitreader_bits_left(&r->r.br) / 8) return bim_rd_trunc(&r->r);
  start = bim_rd_pos(&r->r);
  if (bim_rd_v8(&r->r, &di->cpt_code_len) || bim_rd_count(&r->r, 6, &di->ncpaths) || bim_rd_flag(&r->r, &di->cpt_complete)) return -1;
  if (di->cpt_code_len > 64) {
    di->ncpaths = 0;
    return range(r, "ContextPathCode_Length");
  }
  if (di->ncpaths && !(di->cpaths = alloc_n(di->ncpaths, sizeof *di->cpaths))) {
    di->ncpaths = 0;
    return bim_oom(r->r.err);
  }
  for (size_t i = 0; i < di->ncpaths; i++) {
    bim_di_cpath_t *p = &di->cpaths[i];
    bim_bits_t b;
    if (bim_rd_v5(&r->r, &p->nbits) || bim_rd_raw(&r->r, p->nbits, &b)) return -1;
    p->bits = b.bits;
    p->code = i + 1;
    if (!di->cpt_complete) {
      if (bim_rd_bits(&r->r, (int)di->cpt_code_len, &p->code)) return -1;
      if (!p->code) return range(r, "ContextPathCode");
    }
  }
  used = bim_rd_pos(&r->r) - start;
  /* length: bytes after ContextPathTable_Length */
  if ((used + 7) / 8 != len) return bim_fail(r->r.err, BIM_DI_LENGTH, "bim: DecoderInit %s mismatch", "ContextPathTable_Length");
  return bitreader_skip(&r->r.br, (size_t)len * 8 - used) ? bim_rd_trunc(&r->r) : 0;
}

static int rd_known(rd_t *r) {
  bim_decoderinit_t *di = r->di;
  uint64_t end = bim_di_nall(di);
  if (bim_rd_count(&r->r, 24, &di->nknown)) return -1;
  if (di->nknown && !(di->known = alloc_n(di->nknown, sizeof *di->known))) {
    di->nknown = 0;
    return bim_oom(r->r.err);
  }
  for (size_t i = 0; i < di->nknown; i++) {
    bim_di_schema_t *s = &di->known[i];
    if (bim_rd_v8(&r->r, &s->id)) return -1;
    /* 6.2.3: NumberOfSchemas no-namespace attributes, last ISO reserved */
    if (s->id <= di->nschemas || s->id + 1 >= end) return range(r, "KnownAdditionalSchemaID");
    if (bim_rd_str(&r->r, BIM_DI_ZERO, "AdditionalSchemaURI_Length", &s->uri) || bim_rd_str(&r->r, NULL, NULL, &s->hint) || rd_codecs(r, s) ||
        rd_ext(r, s->uri, 0, &s->castable) || rd_ext(r, s->uri, 1, &s->substitutable))
      return -1;
  }
  return 0;
}

static int rd_additional(rd_t *r) {
  bim_decoderinit_t *di = r->di;
  uint64_t v;
  if (bim_rd_v8(&r->r, &di->nadditional) || rd_known(r) || bim_rd_bits(&r->r, 8, &v)) return -1;
  di->schema_encoding = (unsigned)v;
  if (rd_ext(r, NULL, 0, &di->castable) || rd_ext(r, NULL, 1, &di->substitutable)) return -1;
  return bim_rd_zero(&r->r, 7, BIM_DI_RESERVED);
}

static int rd_instances(rd_t *r) {
  bim_decoderinit_t *di = r->di;
  int tw = bim_width(di->naod_types);
  if (bim_rd_count(&r->r, 8, &di->ninstances)) return -1;
  if (di->ninstances && !(di->instances = alloc_n(di->ninstances, sizeof *di->instances))) {
    di->ninstances = 0;
    return bim_oom(r->r.err);
  }
  for (size_t i = 0; i < di->ninstances; i++) {
    bim_di_instance_t *in = &di->instances[i];
    uint64_t n;
    bitreader_t sub;
    if (bim_rd_v8(&r->r, &n) || bim_rd_bytes(&r->r, n, &in->raw)) return -1;
    in->nraw = (size_t)n;
    bitreader_init(&sub, in->raw, in->nraw);
    if (bitreader_get(&sub, tw, &in->type)) return bim_fail(r->r.err, BIM_DI_LENGTH, "bim: DecoderInit %s mismatch", "AdvancedOptimisedDecoderInstantiationLength");
    if (in->type >= di->naod_types) return range(r, "AdvancedOptimisedDecoderInstances[i].Type");
  }
  return 0;
}

/* SchemaID range checked in check_mappings, NumberOfAdditionalSchemas may follow */
static int rd_mappings(rd_t *r, int sw) {
  bim_decoderinit_t *di = r->di;
  int iw = bim_width(di->ninstances);
  if (bim_rd_count(&r->r, 16, &di->nmappings)) return -1;
  if (di->nmappings && !(di->mappings = alloc_n(di->nmappings, sizeof *di->mappings))) {
    di->nmappings = 0;
    return bim_oom(r->r.err);
  }
  for (size_t k = 0; k < di->nmappings; k++) {
    bim_di_mapping_t *m = &di->mappings[k];
    uint64_t v;
    if (bim_rd_flag(&r->r, &m->preserve_default) || bim_rd_bits(&r->r, 7, &v) || bim_rd_count(&r->r, (uint64_t)iw, &m->ninstances)) return -1;
    m->reserved = (unsigned)v;
    if (m->ninstances && !(m->instances = alloc_n(m->ninstances, sizeof *m->instances))) {
      m->ninstances = 0;
      return bim_oom(r->r.err);
    }
    for (size_t i = 0; i < m->ninstances; i++) {
      if (bim_rd_bits(&r->r, iw, &m->instances[i])) return -1;
      if (m->instances[i] >= di->ninstances) return range(r, "AdvancedOptimisedDecoderInstanceID");
    }
    if (bim_rd_count(&r->r, (uint64_t)sw + 8, &m->ntypes)) return -1;
    if (m->ntypes && !(m->types = alloc_n(m->ntypes, sizeof *m->types))) {
      m->ntypes = 0;
      return bim_oom(r->r.err);
    }
    for (size_t i = 0; i < m->ntypes; i++)
      if (bim_rd_bits(&r->r, sw, &m->types[i].schema) || bim_rd_v8(&r->r, &m->types[i].type)) return -1;
  }
  return 0;
}

static int check_mappings(rd_t *r) {
  for (size_t k = 0; k < r->di->nmappings; k++)
    for (size_t i = 0; i < r->di->mappings[k].ntypes; i++)
      if (r->di->mappings[k].types[i].schema >= bim_di_nall(r->di)) return range(r, "SchemaID");
  return 0;
}

static void free_schema(bim_di_schema_t *s) {
  free(s->uri);
  free(s->hint);
  for (size_t i = 0; i < s->ncodecs; i++) {
    free(s->codecs[i].uri);
    free(s->codecs[i].types);
  }
  free(s->codecs);
  bim_ext_free(&s->castable);
  bim_ext_free(&s->substitutable);
}

/* parts depending on mapping SchemaID width */
static void free_tail(bim_decoderinit_t *di) {
  for (size_t i = 0; i < di->ninstances; i++) free(di->instances[i].raw);
  for (size_t i = 0; i < di->nmappings; i++) {
    free(di->mappings[i].instances);
    free(di->mappings[i].types);
  }
  for (size_t i = 0; i < di->nknown; i++) free_schema(&di->known[i]);
  free(di->instances);
  free(di->mappings);
  free(di->frag_formats);
  free(di->known);
  free(di->initial);
  bim_ext_free(&di->castable);
  bim_ext_free(&di->substitutable);
  di->instances = NULL;
  di->mappings = NULL;
  di->frag_formats = NULL;
  di->known = NULL;
  di->initial = NULL;
  di->ninstances = di->nmappings = di->nfrag_formats = di->nknown = di->ninitial = 0;
  di->nadditional = 0;
  di->schema_encoding = 0;
}

static int rd_tail(rd_t *r, int sw) {
  bim_decoderinit_t *di = r->di;
  uint64_t v;
  if (!di->add_schema_only) {
    if (di->aod && (rd_instances(r) || rd_mappings(r, sw))) return -1;
    if (di->frag_ref) {
      if (bim_rd_bits(&r->r, 8, &v) || bim_rd_bytes(&r->r, v, &di->frag_formats)) return -1;
      di->nfrag_formats = (size_t)v;
    }
  }
  if (di->add_schema && rd_additional(r)) return -1;
  if (!di->add_schema_only) {
    if (bim_rd_v8(&r->r, &v) || bim_rd_bytes(&r->r, v, &di->initial)) return -1;
    di->ninitial = (size_t)v;
  }
  return check_mappings(r);
}

/* 8.3.1: width needs later NumberOfAdditionalSchemas (6.2.3), unique consistent width taken */
static int rd_tail_lookahead(rd_t *r) {
  bim_decoderinit_t *di = r->di;
  bitreader_t mark = r->r.br;
  bim_err_t last = {0};
  size_t far = 0;
  int lsem = -1;
  int hit = -1;
  int nhits = 0;
  if (!(di->aod && di->add_schema && !di->add_schema_only)) return rd_tail(r, bim_width(di->nschemas));
  for (int w = 0; w <= 64; w++) {
    bim_err_t e = {0};
    rd_t t = *r;
    t.r.br = mark;
    t.r.err = &e;
    if (!rd_tail(&t, w) && bim_width(bim_di_nall(di)) == w) {
      hit = w;
      nhits++;
    } else if (e.code) {
      int sem = strcmp(e.code, BIM_DI_TRUNC) != 0;
      if (!strcmp(e.code, "LE500101")) {
        free_tail(di);
        *r->r.err = e;
        return -1;
      }
      /* semantic error before truncation, furthest attempt wins */
      if (sem > lsem || (sem == lsem && bim_rd_pos(&t.r) >= far)) {
        far = bim_rd_pos(&t.r);
        lsem = sem;
        last = e;
      }
    }
    free_tail(di);
  }
  if (nhits == 1) {
    r->r.br = mark;
    return rd_tail(r, hit);
  }
  if (!nhits && last.code) {
    *r->r.err = last;
    return -1;
  }
  return bim_fail(r->r.err, BIM_DI_WIDTH, "bim: DecoderInit SchemaID width ambiguous (%d)", nhits);
}

static int rd_all(rd_t *r) {
  bim_decoderinit_t *di = r->di;
  uint64_t v;
  if (bim_rd_v8(&r->r, &di->profile) || bim_rd_bits(&r->r, 3, &v)) return -1;
  di->unit_size_code = (unsigned)v;
  if (di->mpegb) {
    if (bim_rd_flag(&r->r, &di->no_advanced) || bim_rd_bits(&r->r, 4, &v)) return -1;
    di->reserved = (unsigned)v;
    if (!di->no_advanced && rd_features(r)) return -1;
  } else {
    if (bim_rd_bits(&r->r, 5, &v)) return -1;
    di->reserved = (unsigned)v;
    di->no_advanced = 1;
  }
  if (!di->add_schema_only) {
    if (bim_rd_count(&r->r, 24, &di->nschemas)) return -1;
    if (!di->nschemas) return bim_fail(r->r.err, BIM_DI_ZERO, "bim: DecoderInit %s zero", "NumberOfSchemas");
    if (!(di->schemas = calloc(di->nschemas, sizeof *di->schemas))) {
      di->nschemas = 0;
      return bim_oom(r->r.err);
    }
    for (size_t k = 0; k < di->nschemas; k++) {
      di->schemas[k].id = k;
      if (bim_rd_str(&r->r, BIM_DI_ZERO, "SchemaURI_Length", &di->schemas[k].uri) || bim_rd_str(&r->r, NULL, NULL, &di->schemas[k].hint) ||
          rd_codecs(r, &di->schemas[k]))
        return -1;
    }
    if (di->cpt && rd_cpt(r)) return -1;
    if (di->aod) {
      if (bim_rd_count(&r->r, 8, &di->naod_types)) return -1;
      if (di->naod_types && !(di->aod_types = alloc_n(di->naod_types, sizeof *di->aod_types))) {
        di->naod_types = 0;
        return bim_oom(r->r.err);
      }
      for (size_t i = 0; i < di->naod_types; i++)
        if (bim_rd_str(&r->r, NULL, NULL, &di->aod_types[i])) return -1;
    }
  }
  return rd_tail_lookahead(r);
}

static void init_rd(rd_t *r, bim_decoderinit_t *di, bim_err_t *err) {
  memset(r, 0, sizeof *r);
  r->di = di;
  r->r.err = err;
  r->r.trunc = BIM_DI_TRUNC;
  r->r.what = "DecoderInit";
}

int bim_decoderinit_parse(const unsigned char *buf, size_t len, int mpegb, bim_di_counts_fn counts, void *ctx, bim_decoderinit_t *di,
                          size_t *used, bim_err_t *err) {
  rd_t r;
  memset(di, 0, sizeof *di);
  memset(err, 0, sizeof *err);
  di->mpegb = mpegb;
  init_rd(&r, di, err);
  bitreader_init(&r.r.br, buf, len);
  r.counts = counts;
  r.ctx = ctx;
  if (rd_all(&r)) {
    bim_decoderinit_free(di);
    return -1;
  }
  *used = (bim_rd_pos(&r.r) + 7) / 8;
  return 0;
}

int bim_di_aod_config_parse(bitreader_t *br, const bim_decoderinit_t *di, bim_decoderinit_t *out, bim_err_t *err) {
  rd_t r;
  memset(out, 0, sizeof *out);
  memset(err, 0, sizeof *err);
  out->nschemas = di->nschemas;
  out->add_schema = di->add_schema;
  out->nadditional = di->nadditional;
  out->naod_types = di->naod_types;
  init_rd(&r, out, err);
  r.r.br = *br;
  if (rd_instances(&r) || rd_mappings(&r, bim_width(bim_di_nall(out))) || check_mappings(&r)) {
    out->naod_types = out->nschemas = 0;
    bim_decoderinit_free(out);
    return -1;
  }
  out->naod_types = out->nschemas = 0;
  *br = r.r.br;
  return 0;
}

void bim_decoderinit_free(bim_decoderinit_t *di) {
  free_tail(di);
  for (size_t i = 0; i < di->nschemas; i++) free_schema(&di->schemas[i]);
  for (size_t i = 0; i < di->ncpaths; i++) free(di->cpaths[i].bits);
  for (size_t i = 0; i < di->naod_types; i++) free(di->aod_types[i]);
  for (size_t i = 0; i < di->nprefixes; i++) {
    free(di->prefixes[i].uri);
    free(di->prefixes[i].prefix);
  }
  free(di->schemas);
  free(di->cpaths);
  free(di->aod_types);
  free(di->prefixes);
  memset(di, 0, sizeof *di);
}
