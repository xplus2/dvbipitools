/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/aod.h"

#define CS_2003 "urn:mpeg:mpeg7:systems:SystemsAdvancedOptimisedDecodersCS:2003:"
#define CS_2004 "urn:mpeg:mpeg7:systems:SystemsAdvancedOptimisedDecodersCS:2004:"
#define CS_2007 "urn:mpeg:mpegb:bim:SystemsAdvancedOptimisedDecodersCS:2007:"

const bim_aod_map_t *bim_aod_find(const bim_aod_t *a, const bim_simple_t *s) {
  for (; s; s = s->base)
    for (int i = a->nmaps - 1; i >= 0; i--)
      if (a->maps[i].simple == s) return &a->maps[i];
  return NULL;
}

void bim_aod_free(bim_aod_t *a) {
  if (!a) return;
  for (int i = 0; i < a->ninsts; i++) free(a->insts[i].matrix);
  for (int i = 0; i < a->nmaps; i++) free((int *)a->maps[i].inst);
  free(a->insts);
  free(a->maps);
  free(a);
}

double bim_aod_dequant(const bim_aod_inst_t *in, uint64_t vq, int nbits) { return in->vmin + (double)vq * (in->vmax - in->vmin) / (double)((1ull << nbits) - 1); }

static int bad(bim_err_t *err, const char *what) { return bim_fail(err, BIM_AOD_ERR, "bim: advanced optimised decoder %s", what); }

static bim_aod_kind_t kind_of(const char *uri) {
  const char *p = NULL;
  const char *pre[] = {CS_2003, CS_2004, CS_2007};
  for (int i = 0; i < 3 && !p; i++)
    if (!strncmp(uri, pre[i], strlen(pre[i]))) p = uri + strlen(pre[i]);
  if (!p || !p[0] || p[1]) return 0;
  return *p >= '1' && *p <= '4' ? (bim_aod_kind_t)(*p - '0') : 0;
}

/* sign + vluimsbf5 magnitude */
static int rd_signed(bim_rd_t *r, double *v) {
  int neg;
  uint64_t m;
  if (bim_rd_flag(r, &neg) || bim_rd_v5(r, &m)) return -1;
  *v = neg ? -(double)m : (double)m;
  return 0;
}

static int rd_uniform(bim_rd_t *r, bim_aod_inst_t *in) {
  uint64_t v;
  if (rd_signed(r, &in->vmin) || rd_signed(r, &in->vmax) || bim_rd_bits(r, 5, &v)) return -1;
  in->nbits = (int)v + 1;
  return 0;
}

static int rd_nonuniform(bim_rd_t *r, bim_aod_inst_t *in) {
  uint64_t v;
  size_t n;
  if (bim_rd_v5(r, &v) || v > 31) return v > 31 ? bad(r->err, "nBitsData above 32") : -1;
  in->nbits_data = (int)v + 1;
  if (bim_rd_bits(r, 5, &v)) return -1;
  in->nchar = (int)v;
  if (!in->nchar) return bad(r->err, "nCharacteristics zero");
  if (rd_uniform(r, in)) return -1;
  if (in->nbits_data > 20) return bad(r->err, "quantMatrix too large");
  n = (size_t)in->nchar << in->nbits_data;
  if (n && !(in->matrix = calloc(n, sizeof *in->matrix))) return bim_oom(r->err);
  for (size_t i = 0; i < n; i++) {
    if (bim_rd_bits(r, in->nbits, &v)) return -1;
    in->matrix[i] = (uint32_t)v;
  }
  return 0;
}

static int build_inst(const bim_decoderinit_t *di, const bim_di_instance_t *src, bim_aod_inst_t *in, bim_err_t *err) {
  bim_rd_t r = {.err = err, .trunc = BIM_AOD_ERR, .what = "advanced optimised decoder"};
  const char *uri = src->type < di->naod_types ? di->aod_types[src->type] : NULL;
  uint64_t v;
  if (!uri || !(in->kind = kind_of(uri))) return bad(err, "type unknown");
  bitreader_init(&r.br, src->raw, src->nraw);
  if (bim_rd_bits(&r, bim_width(di->naod_types), &v)) return -1;
  if (in->kind == BIM_AOD_UQ) return rd_uniform(&r, in);
  return in->kind == BIM_AOD_NUQ ? rd_nonuniform(&r, in) : 0;
}

static const bim_simple_t *mapped_simple(const bim_schema_t *sc, int n, const bim_di_typeref_t *t) {
  if (t->schema >= (uint64_t)n || t->type + 1 >= (uint64_t)sc[t->schema].ntypes) return NULL;
  return sc[t->schema].types[t->type + 1]->simple;
}

static int build_map(const bim_di_mapping_t *src, const bim_schema_t *sc, int n, bim_aod_t *a, int *nm, bim_err_t *err) {
  for (size_t i = 0; i < src->ntypes; i++) {
    const bim_simple_t *s = mapped_simple(sc, n, &src->types[i]);
    bim_aod_map_t *m = &a->maps[*nm];
    int *inst = NULL;
    if (!s) continue;
    for (int k = 0; k < *nm; k++)
      if (a->maps[k].simple == s) return bad(err, "type mapped twice");
    if (src->ninstances && !(inst = calloc(src->ninstances, sizeof *inst))) return bim_oom(err);
    for (size_t k = 0; k < src->ninstances; k++) inst[k] = (int)src->instances[k];
    m->simple = s;
    m->preserve = src->preserve_default;
    m->inst = inst;
    m->ninst = (int)src->ninstances;
    (*nm)++;
  }
  return 0;
}

int bim_aod_build(const bim_decoderinit_t *di, const bim_decoderinit_t *cfg, const bim_schema_t *schemas, int nschemas, bim_aod_t **out, bim_err_t *err) {
  bim_aod_t *a = calloc(1, sizeof *a);
  size_t nmax = 0;
  int nm = 0;
  if (!a) return bim_oom(err);
  for (size_t k = 0; k < cfg->nmappings; k++) nmax += cfg->mappings[k].ntypes;
  if (cfg->ninstances && !(a->insts = calloc(cfg->ninstances, sizeof *a->insts))) goto oom;
  if (nmax && !(a->maps = calloc(nmax, sizeof *a->maps))) goto oom;
  a->ninsts = (int)cfg->ninstances;
  for (size_t i = 0; i < cfg->ninstances; i++)
    if (build_inst(di, &cfg->instances[i], &a->insts[i], err)) goto fail;
  for (size_t k = 0; k < cfg->nmappings; k++)
    if (build_map(&cfg->mappings[k], schemas, nschemas, a, &nm, err)) goto fail;
  a->nmaps = nm;
  *out = a;
  return 0;
oom:
  bim_oom(err);
fail:
  a->nmaps = nm;
  bim_aod_free(a);
  return -1;
}
