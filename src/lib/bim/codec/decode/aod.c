/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/codec/decode/priv.h"

/* 8.5.3, 8.6.3: UQ or NUQ code dequantized to float text */
static int quant_text(dec_t *d, const bim_simple_t *s, bim_aod_inst_t *in, dstrbuf_t *b) {
  char t[40];
  uint64_t q;
  double v;
  if (in->kind == BIM_AOD_UQ) {
    if (bitreader_get(d->br, in->nbits, &q)) return -1;
    v = bim_aod_dequant(in, q, in->nbits);
  } else {
    if (bitreader_get(d->br, in->nbits_data, &q)) return -1;
    v = bim_aod_dequant(in, in->matrix[((size_t)in->counter << in->nbits_data) + q], in->nbits);
    in->counter = (in->counter + 1) % in->nchar;
  }
  dec_float_render(v, s->codec == BIM_DOUBLE ? 17 : 9, t, sizeof t);
  return dec_add(b, t);
}

/* 8.8.2: "N:" SchemaID to schema_prefix[N] */
static int qname_text(dec_t *d, dstrbuf_t *b) {
  char *raw;
  uint64_t n;
  int rc = 0;
  if (bitreader_get_vluimsbf5(d->br, &n) || n > bitreader_bits_left(d->br) / 8 || !(raw = malloc((size_t)n + 1))) return -1;
  if (bitreader_get_bytes(d->br, (unsigned char *)raw, (size_t)n)) {
    free(raw);
    return -1;
  }
  raw[n] = '\0';
  for (size_t i = 0; !rc && i < n;) {
    size_t k = i;
    while (k < n && isdigit((unsigned char)raw[k])) k++;
    if (k > i && k < n && raw[k] == ':' && (!i || !qn_name_char((unsigned char)raw[i - 1])) && k - i < 10) {
      unsigned long id = strtoul(raw + i, NULL, 10);
      const char *pfx = d->opts && d->opts->schema_prefix && id < (unsigned long)d->nschemas ? d->opts->schema_prefix[id] : NULL;
      if (pfx) {
        rc = dec_add(b, pfx) || (*pfx && dec_add(b, ":"));
        i = k + 1;
        continue;
      }
    }
    rc = dec_add_n(b, raw + i, 1);
    i++;
  }
  free(raw);
  return rc ? -1 : 0;
}

int dec_aod(dec_t *d, const bim_simple_t *s, const bim_aod_map_t *m, dstrbuf_t *b) {
  bim_aod_inst_t *in;
  uint64_t id;
  int n = m->ninst + m->preserve;
  if (bitreader_get(d->br, bim_width((uint64_t)n), &id) || id >= (uint64_t)n) return -1;
  if (m->preserve && !id) return dec_plain(d, s, b);
  in = &d->opts->aod->insts[m->inst[id - (uint64_t)m->preserve]];
  switch (in->kind) {
    case BIM_AOD_ZLIB: return dec_zstring(d, b);
    case BIM_AOD_QNAME: return qname_text(d, b);
    default: return quant_text(d, s, in, b);
  }
}
