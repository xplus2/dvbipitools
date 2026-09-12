/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_FMP4_BOX_H
#define DVBIPITOOLS_LIB_MUX_FMP4_BOX_H

#include <stddef.h>
#include <stdint.h>

#include "lib/demux/psi/psi.h"
#include "lib/mux/growbuf.h"

/* growable ISOBMFF box build buffer */
typedef muxbuf_t mp4buf_t;

void mp4buf_free(mp4buf_t *b);
void mb_bytes(mp4buf_t *b, const void *data, size_t n);
void mb_u8(mp4buf_t *b, unsigned v);
void mb_u16(mp4buf_t *b, unsigned v);
void mb_u24(mp4buf_t *b, unsigned v);
void mb_u32(mp4buf_t *b, uint32_t v);
void mb_u64(mp4buf_t *b, uint64_t v);
void mb_fourcc(mp4buf_t *b, const char fourcc[4]);

/* overwrites 4 bytes already written at pos with v (big-endian) */
void mb_patch_u32(mp4buf_t *b, size_t pos, uint32_t v);

/* frees child */
void mb_box(mp4buf_t *parent, const char fourcc[4], mp4buf_t *child);

/* identity matrix, tkhd/mvhd matrix field */
void put_matrix_unity(mp4buf_t *b);

/* MPEG-4 descriptor length, base-128 continuation encoding */
void put_desc_size(mp4buf_t *out, size_t len);
/* frees payload */
void put_desc(mp4buf_t *out, unsigned tag, mp4buf_t *payload);

typedef struct {
  unsigned track_id;
  pid_class_t cls;
  unsigned width;
  unsigned height;
  codec_t codec;
  const unsigned char *cpriv;
  size_t cpriv_len;
  unsigned rate;
  unsigned channels;
  unsigned char ac3_bsid;
  unsigned char ac3_bsmod;
  unsigned char ac3_acmod;
  unsigned char ac3_lfeon;
  unsigned ac3_bitrate_code;
} trak_meta_t;

void trak_build_hdlr(mp4buf_t *out, pid_class_t cls);
void trak_build_vmhd(mp4buf_t *out);
void trak_build_smhd(mp4buf_t *out);
void trak_build_dinf(mp4buf_t *out);
void trak_build_stsd(mp4buf_t *out, const trak_meta_t *t);
/* tref/sbas: depends_on_track_id */
void trak_build_tref(mp4buf_t *out, unsigned depends_on_track_id);

#endif
