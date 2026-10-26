/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/aod.h"
#include "lib/bim/codec/encode/priv.h"

/* 8.5.3, 8.6.3: nearest representable value */
static int quant(enc_t *e, bim_aod_inst_t *in, const char *v) {
  double x;
  if (enc_parse_double(v, &x) || isnan(x)) return -1;
  if (in->kind == BIM_AOD_UQ) {
    double span = in->vmax - in->vmin;
    if (x < in->vmin || x > in->vmax || !(span > 0)) return -1;
    return bitwriter_put(e->bw, (uint64_t)((x - in->vmin) / span * (double)((1ull << in->nbits) - 1) + 0.5), in->nbits);
  }
  {
    uint64_t best = 0;
    double dist = INFINITY;
    for (uint64_t q = 0; q < (1ull << in->nbits_data); q++) {
      double d = fabs(bim_aod_dequant(in, in->matrix[((size_t)in->counter << in->nbits_data) + q], in->nbits) - x);
      if (d < dist) {
        dist = d;
        best = q;
      }
    }
    in->counter = (in->counter + 1) % in->nchar;
    return bitwriter_put(e->bw, best, in->nbits_data);
  }
}

/* 8.8.2: schema_prefix to "N:" SchemaID */
static int qname(enc_t *e, const char *v) {
  dstrbuf_t out;
  int rc;
  dstrbuf_init(&out);
  for (size_t i = 0; out.buf && v[i];) {
    size_t j = i;
    int id = -1;
    char num[16];
    while (v[j] && qn_name_char((unsigned char)v[j])) j++;
    if (v[j] == ':' && j > i && (!i || !qn_name_char((unsigned char)v[i - 1])) && e->o->schema_prefix)
      for (int s = 0; s < e->nschemas && id < 0; s++)
        if (e->o->schema_prefix[s] && strlen(e->o->schema_prefix[s]) == j - i && !strncmp(e->o->schema_prefix[s], v + i, j - i)) id = s;
    if (id >= 0) {
      snprintf(num, sizeof num, "%d:", id);
      dstrbuf_add(&out, num);
      i = j + 1;
      continue;
    }
    dstrbuf_add_n(&out, v + i++, 1);
  }
  rc = out.buf ? enc_plain(e, &bim_xs_string, out.buf) : -1;
  free(out.buf);
  return rc;
}

int enc_aod(enc_t *e, const bim_simple_t *s, const bim_aod_map_t *m, const char *v) {
  int n = m->ninst + m->preserve;
  int id = m->preserve && (e->o->aod_plain || !m->ninst) ? 0 : m->preserve;
  bim_aod_inst_t *in;
  if (!n || bitwriter_put(e->bw, (uint64_t)id, bim_width((uint64_t)n))) return -1;
  if (m->preserve && !id) return enc_plain(e, s, v);
  in = &e->o->aod->insts[m->inst[id - m->preserve]];
  switch (in->kind) {
    case BIM_AOD_ZLIB: return enc_zchunks(e, v);
    case BIM_AOD_QNAME: return qname(e, v);
    default: return quant(e, in, v);
  }
}
