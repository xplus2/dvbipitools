/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_TVAINIT_H
#define DVBIPITOOLS_LIB_BIM_TVAINIT_H

#include <stddef.h>
#include <stdint.h>

#include "bimio.h"
#include "decoderinit.h"

/* TS 102 822-3-2 4.4.1 table 54 of TS 102 323 9.4.2.1: message before DecoderInit */

#define BIM_TVAINIT_TVA 0x01
#define BIM_TVAINIT_DVB 0xF0
/* BufferSize when absent, TS 102 822-3-2 4.4.1.1 */
#define BIM_TVAINIT_BUFFER_DEFAULT 1000u

typedef struct {
  unsigned encoding_version; /* TVA 0x01, DVB 0xF0 */
  int indexing; /* IndexingFlag */
  unsigned indexing_version;
  int has_buffer_size;
  uint32_t buffer_size; /* 24 bit */
  int position_code; /* PositionCodeFlag, 0 for DVB */
  unsigned char_encoding; /* TS 102 822-3-2 table 2 */
} bim_tvainit_t;

/* Zlib BufferSize of the stream, 4.4.1.1 */
static inline size_t bim_tvainit_buffer_size(const bim_tvainit_t *i) { return i->has_buffer_size ? i->buffer_size : BIM_TVAINIT_BUFFER_DEFAULT; }

/* header up to the DecoderInit, caller appends it. 0 ok, -1 error (err) */
int bim_tvainit_write(const bim_tvainit_t *i, bitwriter_t *bw, bim_err_t *err);
/* *used: DecoderInitptr, offset of the DecoderInit. 0 ok, -1 error (err) */
int bim_tvainit_parse(const unsigned char *buf, size_t len, bim_tvainit_t *i, size_t *used, bim_err_t *err);

/* TS 102 323 9.4.2.2, TS 102 822-3-2 4.4.2.1 DecoderInit: SchemaURI[0] = uri, more: SchemaURI[1..], no type codecs, empty InitialDescription.
   Strings borrowed, bim_decoderinit_write, no bim_decoderinit_free. */
int bim_tvainit_decoderinit(const char *uri, const char *const *more, size_t nmore, bim_decoderinit_t *di);
void bim_tvainit_decoderinit_free(bim_decoderinit_t *di);

#define BIM_TVAINIT_TRUNC "LE400308"
#define BIM_TVAINIT_RANGE "LE400309"

#endif
