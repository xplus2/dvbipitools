/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include <stdlib.h>

#include "lib/bim/tvainit.h"

static int range(bim_err_t *err, const char *what) { return bim_fail(err, BIM_TVAINIT_RANGE, "bim: TVA-init %s out of range", what); }

static int has_basic(unsigned v) { return v == BIM_TVAINIT_TVA || v == BIM_TVAINIT_DVB; }

/* tables 1, 2, 3 of TS 102 822-3-2, table 55 of TS 102 323 */
static int check(const bim_tvainit_t *i, bim_err_t *err) {
  if (!has_basic(i->encoding_version)) return range(err, "EncodingVersion");
  if (i->encoding_version == BIM_TVAINIT_DVB && i->position_code) return range(err, "PositionCodeFlag");
  if ((i->char_encoding > 0x06 && i->char_encoding < 0xE1) || i->char_encoding > 0xFF) return range(err, "CharacterEncoding");
  if (i->has_buffer_size && i->buffer_size > 0xFFFFFFu) return range(err, "BufferSize");
  if (i->indexing && i->indexing_version != 0x01 && (i->indexing_version < 0xF0 || i->indexing_version > 0xFF)) return range(err, "IndexingVersion");
  return 0;
}

int bim_tvainit_write(const bim_tvainit_t *i, bitwriter_t *bw, bim_err_t *err) {
  bim_wr_t w = {bw, err};
  size_t ptr = 3 + 2 + (size_t)(i->has_buffer_size ? 3 : 0) + (size_t)(i->indexing ? 1 : 0);
  memset(err, 0, sizeof *err);
  if (check(i, err)) return -1;
  if (bim_wr_put(&w, i->encoding_version, 8) || bim_wr_put(&w, (uint64_t)!!i->indexing, 1) || bim_wr_put(&w, 0x7F, 7) || bim_wr_put(&w, ptr, 8)) return -1;
  if (bim_wr_put(&w, (uint64_t)!!i->has_buffer_size, 1) || bim_wr_put(&w, (uint64_t)!!i->position_code, 1) || bim_wr_put(&w, 0x3F, 6) || bim_wr_put(&w, i->char_encoding, 8)) return -1;
  if (i->has_buffer_size && bim_wr_put(&w, i->buffer_size, 24)) return -1;
  return i->indexing ? bim_wr_put(&w, i->indexing_version, 8) : 0;
}

int bim_tvainit_parse(const unsigned char *buf, size_t len, bim_tvainit_t *i, size_t *used, bim_err_t *err) {
  bim_rd_t r = {.err = err, .trunc = BIM_TVAINIT_TRUNC, .what = "TVA-init"};
  uint64_t v;
  uint64_t ptr;
  memset(err, 0, sizeof *err);
  memset(i, 0, sizeof *i);
  bitreader_init(&r.br, buf, len);
  if (bim_rd_bits(&r, 8, &v)) return -1;
  i->encoding_version = (unsigned)v;
  if (bim_rd_flag(&r, &i->indexing) || bim_rd_bits(&r, 7, &v) || bim_rd_bits(&r, 8, &ptr)) return -1;
  if (has_basic(i->encoding_version)) {
    if (bim_rd_flag(&r, &i->has_buffer_size) || bim_rd_flag(&r, &i->position_code) || bim_rd_bits(&r, 6, &v) || bim_rd_bits(&r, 8, &v)) return -1;
    i->char_encoding = (unsigned)v;
    if (i->has_buffer_size) {
      if (bim_rd_bits(&r, 24, &v)) return -1;
      i->buffer_size = (uint32_t)v;
    }
  }
  if (i->indexing) {
    if (bim_rd_bits(&r, 8, &v)) return -1;
    i->indexing_version = (unsigned)v;
  }
  if (check(i, err)) return -1;
  if (ptr < bim_rd_pos(&r) / 8 || ptr > len) return range(err, "DecoderInitptr");
  *used = (size_t)ptr;
  return 0;
}

int bim_tvainit_decoderinit(const char *uri, const char *const *more, size_t nmore, bim_decoderinit_t *di) {
  memset(di, 0, sizeof *di);
  di->reserved = 0x1F;
  di->nschemas = 1 + nmore;
  if (!(di->schemas = calloc(di->nschemas, sizeof *di->schemas))) return -1;
  di->schemas[0].uri = (char *)uri;
  for (size_t i = 0; i < nmore; i++) di->schemas[1 + i].uri = (char *)more[i];
  return 0;
}

void bim_tvainit_decoderinit_free(bim_decoderinit_t *di) {
  free(di->schemas);
  memset(di, 0, sizeof *di);
}
