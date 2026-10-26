/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/bimio.h"

int bim_width(uint64_t n) {
  int b = 0;
  while (b < 64 && (1ull << b) < n) b++;
  return b;
}

int bim_fail(bim_err_t *err, const char *code, const char *fmt, ...) {
  va_list ap;
  err->code = code;
  va_start(ap, fmt);
  vsnprintf(err->msg, sizeof err->msg, fmt, ap);
  va_end(ap);
  return -1;
}

int bim_oom(bim_err_t *err) { return bim_fail(err, "LE500101", "out of memory"); }

size_t bim_rd_pos(const bim_rd_t *r) { return r->br.byte_pos * 8 + (size_t)r->br.bit_pos; }

int bim_rd_trunc(bim_rd_t *r) { return bim_fail(r->err, r->trunc, "bim: %s truncated", r->what); }

int bim_rd_bits(bim_rd_t *r, int n, uint64_t *v) { return bitreader_get(&r->br, n, v) ? bim_rd_trunc(r) : 0; }

int bim_rd_flag(bim_rd_t *r, int *f) {
  uint64_t v;
  if (bim_rd_bits(r, 1, &v)) return -1;
  *f = (int)v;
  return 0;
}

int bim_rd_v8(bim_rd_t *r, uint64_t *v) { return bitreader_get_vluimsbf8(&r->br, v) ? bim_rd_trunc(r) : 0; }

int bim_rd_v5(bim_rd_t *r, uint64_t *v) { return bitreader_get_vluimsbf5(&r->br, v) ? bim_rd_trunc(r) : 0; }

int bim_rd_count(bim_rd_t *r, uint64_t min_bits, size_t *n) {
  uint64_t v;
  if (bim_rd_v8(r, &v)) return -1;
  if (min_bits && v > bitreader_bits_left(&r->br) / min_bits) return bim_rd_trunc(r);
  *n = (size_t)v;
  return 0;
}

int bim_rd_bytes(bim_rd_t *r, uint64_t n, unsigned char **out) {
  if (n > bitreader_bits_left(&r->br) / 8) return bim_rd_trunc(r);
  if (!(*out = malloc((size_t)n + 1))) return bim_oom(r->err);
  if (bitreader_get_bytes(&r->br, *out, (size_t)n)) return bim_rd_trunc(r);
  (*out)[n] = 0;
  return 0;
}

int bim_rd_str(bim_rd_t *r, const char *zero, const char *name, char **out) {
  uint64_t n;
  if (bim_rd_v8(r, &n)) return -1;
  if (!n && zero) return bim_fail(r->err, zero, "bim: %s %s zero", r->what, name);
  return bim_rd_bytes(r, n, (unsigned char **)out);
}

int bim_rd_zero(bim_rd_t *r, uint64_t nbits, const char *code) {
  while (nbits) {
    int k = nbits > 64 ? 64 : (int)nbits;
    uint64_t v;
    if (bim_rd_bits(r, k, &v)) return -1;
    if (v) return bim_fail(r->err, code, "bim: %s reserved bits not zero", r->what);
    nbits -= (uint64_t)k;
  }
  return 0;
}

int bim_rd_raw(bim_rd_t *r, uint64_t nbits, bim_bits_t *b) {
  size_t nb = (size_t)((nbits + 7) / 8);
  if (nbits > bitreader_bits_left(&r->br)) return bim_rd_trunc(r);
  if (!(b->bits = calloc(nb ? nb : 1, 1))) return bim_oom(r->err);
  b->nbits = nbits;
  for (uint64_t k = 0; k < nbits; k += 8) {
    uint64_t v;
    int n = nbits - k < 8 ? (int)(nbits - k) : 8;
    if (bim_rd_bits(r, n, &v)) return -1;
    b->bits[k / 8] = (unsigned char)(v << (8 - n));
  }
  return 0;
}

int bim_rd_rational(bim_rd_t *r, uint64_t *v, const char *range) {
  int n = 0;
  uint64_t b;
  do {
    if (bim_rd_bits(r, 1, &b)) return -1;
    n++;
  } while (b && n <= 16);
  if (n > 16) return bim_fail(r->err, range, "bim: %s rational position code out of range", r->what);
  if (bim_rd_bits(r, 4 * n, &b)) return -1;
  *v = 4 * n == 64 ? b : b << (64 - 4 * n);
  return 0;
}

int bim_rd_ext(bim_rd_t *r, uint64_t count, const char *range, bim_ext_t *x) {
  uint64_t n;
  int w = bim_width(count);
  if (bim_rd_flag(r, &x->present)) return -1;
  if (!x->present) return 0;
  if (bim_rd_flag(r, &x->all)) return -1;
  if (x->all) return 0;
  if (bim_rd_v5(r, &n)) return -1;
  if (n > count || (w && n > bitreader_bits_left(&r->br) / (uint64_t)w))
    return bim_fail(r->err, range, "bim: %s externally castable/substitutable code out of range", r->what);
  x->ncodes = (size_t)n;
  if (n && !(x->codes = calloc((size_t)n, sizeof *x->codes))) return bim_oom(r->err);
  for (size_t i = 0; i < x->ncodes; i++) {
    if (bim_rd_bits(r, w, &x->codes[i])) return -1;
    if (x->codes[i] >= count) return bim_fail(r->err, range, "bim: %s externally castable/substitutable code out of range", r->what);
  }
  return 0;
}

void bim_ext_free(bim_ext_t *x) {
  free(x->codes);
  memset(x, 0, sizeof *x);
}

int bim_wr_put(bim_wr_t *w, uint64_t v, int n) { return bitwriter_put(w->bw, v, n) ? bim_oom(w->err) : 0; }

int bim_wr_v8(bim_wr_t *w, uint64_t v) { return bitwriter_put_vluimsbf8(w->bw, v) ? bim_oom(w->err) : 0; }

int bim_wr_v5(bim_wr_t *w, uint64_t v) { return bitwriter_put_vluimsbf5(w->bw, v) ? bim_oom(w->err) : 0; }

int bim_wr_bytes(bim_wr_t *w, const unsigned char *p, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (bim_wr_put(w, p[i], 8)) return -1;
  return 0;
}

int bim_wr_str(bim_wr_t *w, const char *zero, const char *name, const char *s) {
  size_t n = s ? strlen(s) : 0;
  if (!n && zero) return bim_fail(w->err, zero, "bim: %s zero", name);
  return bim_wr_v8(w, n) || bim_wr_bytes(w, (const unsigned char *)s, n);
}

int bim_wr_bits(bim_wr_t *w, const bim_bits_t *b) {
  for (uint64_t k = 0; k < b->nbits; k += 8) {
    int n = b->nbits - k < 8 ? (int)(b->nbits - k) : 8;
    if (bim_wr_put(w, (uint64_t)(b->bits[k / 8] >> (8 - n)), n)) return -1;
  }
  return 0;
}

int bim_wr_rational(bim_wr_t *w, uint64_t v) {
  int n = 1;
  while (n < 16 && v << (4 * n)) n++;
  for (int i = 1; i < n; i++)
    if (bim_wr_put(w, 1, 1)) return -1;
  return bim_wr_put(w, 0, 1) || bim_wr_put(w, v >> (64 - 4 * n), 4 * n);
}

int bim_wr_sub(bim_wr_t *w, bitwriter_t *sub) {
  size_t n;
  const unsigned char *p = bitwriter_data(sub, &n);
  int rc = !p && n ? bim_oom(w->err) : bim_wr_bytes(w, p, n);
  bitwriter_free(sub);
  return rc;
}

int bim_wr_ext(bim_wr_t *w, uint64_t count, const char *range, const bim_ext_t *x) {
  if (bim_wr_put(w, (uint64_t)!!x->present, 1)) return -1;
  if (!x->present) return 0;
  if (bim_wr_put(w, (uint64_t)!!x->all, 1)) return -1;
  if (x->all) return 0;
  if (bim_wr_v5(w, x->ncodes)) return -1;
  for (size_t i = 0; i < x->ncodes; i++) {
    if (x->codes[i] >= count) return bim_fail(w->err, range, "bim: externally castable/substitutable code out of range");
    if (bim_wr_put(w, x->codes[i], bim_width(count))) return -1;
  }
  return 0;
}
