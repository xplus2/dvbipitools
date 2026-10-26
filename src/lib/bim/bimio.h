/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_BIMIO_H
#define DVBIPITOOLS_LIB_BIM_BIMIO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "bimreader.h"
#include "bitwriter.h"

/* syntax element readers + writers with doc/error_codes.md errors */

typedef struct {
  const char *code;
  char msg[160];
} bim_err_t;

typedef struct {
  const unsigned char *bits;
  uint64_t nbits;
  unsigned char *heap_bits;
} bim_bits_t;

/* ExternallyCastableTypeTable / ExternallySubstitutableElementTable, ISO 23001-1 6.7.5.4, 6.7.6.4 (Cor 2) */
typedef struct {
  int present;
  int all;
  uint64_t *codes;
  size_t ncodes;
} bim_ext_t;

/* ceil(log2(n)) */
int bim_width(uint64_t n);

int bim_fail_str(bim_err_t *err, const char *code, const char *msg);
#define bim_fail(err, code, ...) \
  ({ \
    char bim_fail_msg_[sizeof(((bim_err_t *)0)->msg)]; \
    snprintf(bim_fail_msg_, sizeof bim_fail_msg_, __VA_ARGS__); \
    bim_fail_str((err), (code), bim_fail_msg_); \
  })
int bim_oom(bim_err_t *err);

/* trunc: code of truncation, what: syntax structure in messages */
typedef struct {
  bitreader_t br;
  bim_err_t *err;
  const char *trunc;
  const char *what;
} bim_rd_t;

size_t bim_rd_pos(const bim_rd_t *r);
int bim_rd_trunc(bim_rd_t *r);
int bim_rd_bits(bim_rd_t *r, int n, uint64_t *v);
int bim_rd_flag(bim_rd_t *r, int *f);
int bim_rd_v8(bim_rd_t *r, uint64_t *v);
int bim_rd_v5(bim_rd_t *r, uint64_t *v);
/* vluimsbf8 count of entries of at least min_bits each */
int bim_rd_count(bim_rd_t *r, uint64_t min_bits, size_t *n);
int bim_rd_bytes(bim_rd_t *r, uint64_t n, unsigned char **out);
/* vluimsbf8 length + UTF-8. zero: code when length 0 forbidden, else NULL */
int bim_rd_str(bim_rd_t *r, const char *zero, const char *name, char **out);
int bim_rd_zero(bim_rd_t *r, uint64_t nbits, const char *code);
int bim_rd_raw(bim_rd_t *r, uint64_t nbits, bim_bits_t *b);
/* vlurmsbf5 as binary fraction, bit 63 = 1/2 */
int bim_rd_rational(bim_rd_t *r, uint64_t *v, const char *range);
/* count: global types or elements of the schema, range: code of out-of-range */
int bim_rd_ext(bim_rd_t *r, uint64_t count, const char *range, bim_ext_t *x);
void bim_ext_free(bim_ext_t *x);

typedef struct {
  bitwriter_t *bw;
  bim_err_t *err;
} bim_wr_t;

int bim_wr_put(bim_wr_t *w, uint64_t v, int n);
int bim_wr_v8(bim_wr_t *w, uint64_t v);
int bim_wr_v5(bim_wr_t *w, uint64_t v);
int bim_wr_bytes(bim_wr_t *w, const unsigned char *p, size_t n);
int bim_wr_str(bim_wr_t *w, const char *zero, const char *name, const char *s);
int bim_wr_bits(bim_wr_t *w, const bim_bits_t *b);
/* vlurmsbf5 of a binary fraction, bit 63 = 1/2 */
int bim_wr_rational(bim_wr_t *w, uint64_t v);
/* appends sub bytes, frees sub */
int bim_wr_sub(bim_wr_t *w, bitwriter_t *sub);
int bim_wr_ext(bim_wr_t *w, uint64_t count, const char *range, const bim_ext_t *x);

#endif
