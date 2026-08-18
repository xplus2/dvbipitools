/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "../amf.h"
#include "../ebml.h" /* ebuf_t/eb_bytes: growable buffer, reused for tag-body assembly */
#include "priv.h"

#define FLV_FRAME_KEY 1
#define FLV_FRAME_INTER 2

#define FLV_CODECID_AVC 7
#define FLV_AVC_SEQHDR 0
#define FLV_AVC_NALU 1

#define FLV_SOUNDFMT_AAC 10
#define FLV_SOUNDFMT_EXHEADER 9
#define FLV_AAC_SEQHDR 0
#define FLV_AAC_RAW 1

/* Enhanced RTMP (github.com/veovera/enhanced-rtmp) PacketType/AudioPacketType */
#define FLV_EX_PT_SEQ_START 0
#define FLV_EX_PT_CODED_FRAMES_X 3 /* CompositionTime implied zero, no DTS tracked separately from PTS */
#define FLV_EX_APT_SEQ_START 0
#define FLV_EX_APT_CODED_FRAMES 1

static const unsigned char FOURCC_HVC1[4] = {'h', 'v', 'c', '1'};
static const unsigned char FOURCC_AC3[4] = {'a', 'c', '-', '3'};
static const unsigned char FOURCC_EAC3[4] = {'e', 'c', '-', '3'};

void flv_send_tag(flv_t *f, flv_tag_type_t type, uint32_t ts, const unsigned char *hdr, size_t hn, const unsigned char *payload, size_t pn) {
  ebuf_t *b = &f->tagbuf;

  b->len = 0; /* keep f->tagbuf's allocation, this tag reuses it */
  b->err = 0;
  eb_bytes(b, hdr, hn);
  if (pn)
    eb_bytes(b, payload, pn);
  if (b->err) {
    f->err = 1;
  } else {
    if (f->bytes)
      *f->bytes += b->len;
    if (f->cb)
      f->cb(f->cb_ctx, type, ts, b->p, b->len);
  }
}

/* only known facts: duration=0, videocodecid=0 iff no video track */
void flv_emit_metadata(flv_t *f) {
  ebuf_t b;

  memset(&b, 0, sizeof b);
  amf_string(&b, "onMetaData");
  amf_ecma_array_start(&b, f->have_v ? (f->have_a ? 3u : 2u) : (f->have_a ? 2u : 1u));
  amf_object_key(&b, "duration");
  amf_number(&b, 0.0);
  if (!f->have_v) {
    amf_object_key(&b, "videocodecid");
    amf_number(&b, 0.0);
  } else if (f->vtrk.es.codec == CODEC_H264) {
    amf_object_key(&b, "videocodecid");
    amf_number(&b, FLV_CODECID_AVC);
  }
  if (f->have_a && (f->atrk.es.codec == CODEC_AAC || f->atrk.es.codec == CODEC_AAC_LATM)) {
    amf_object_key(&b, "audiocodecid");
    amf_number(&b, FLV_SOUNDFMT_AAC);
  }
  amf_ecma_array_end(&b);
  if (b.err) {
    f->err = 1;
  } else {
    if (f->bytes)
      *f->bytes += b.len;
    if (f->cb)
      f->cb(f->cb_ctx, FLV_TAG_SCRIPT, 0, b.p, b.len);
  }
  ebuf_free(&b);
}

static void video_seqhdr(flv_t *f, flv_track_t *t) {
  unsigned char hdr[5];
  size_t hn = 0;

  if (t->es.codec == CODEC_H264) {
    hdr[hn++] = (unsigned char)((FLV_FRAME_KEY << 4) | FLV_CODECID_AVC);
    hdr[hn++] = FLV_AVC_SEQHDR;
    hdr[hn++] = 0;
    hdr[hn++] = 0;
    hdr[hn++] = 0; /* CompositionTime = 0 */
  } else {          /* HEVC: no classic CodecID slot, Enhanced RTMP only */
    hdr[hn++] = (unsigned char)(0x80 | (FLV_FRAME_KEY << 4) | FLV_EX_PT_SEQ_START);
    memcpy(hdr + hn, FOURCC_HVC1, 4);
    hn += 4;
  }
  t->seqhdr_sent = 1;
  flv_send_tag(f, FLV_TAG_VIDEO, 0, hdr, hn, t->es.cpriv, t->es.cpriv_len);
}

void flv_emit_video(flv_t *f, flv_track_t *t, const unsigned char *d, size_t n, int key) {
  unsigned char hdr[5];
  size_t hn = 0;
  int64_t rel = t->ts_ms - f->t0;
  uint32_t ts = rel > 0 ? (uint32_t)rel : 0;
  int frame_type = key ? FLV_FRAME_KEY : FLV_FRAME_INTER;

  if (!t->seqhdr_sent)
    video_seqhdr(f, t);

  if (t->es.codec == CODEC_H264) {
    hdr[hn++] = (unsigned char)((frame_type << 4) | FLV_CODECID_AVC);
    hdr[hn++] = FLV_AVC_NALU;
    hdr[hn++] = 0;
    hdr[hn++] = 0;
    hdr[hn++] = 0; /* CompositionTime = 0 */
  } else {
    hdr[hn++] = (unsigned char)(0x80 | (frame_type << 4) | FLV_EX_PT_CODED_FRAMES_X);
    memcpy(hdr + hn, FOURCC_HVC1, 4);
    hn += 4;
  }
  flv_send_tag(f, FLV_TAG_VIDEO, ts, hdr, hn, d, n);
}

static void audio_seqhdr(flv_t *f, flv_track_t *t) {
  unsigned char hdr[5];
  size_t hn = 0;

  if (t->es.codec == CODEC_AAC || t->es.codec == CODEC_AAC_LATM) {
    hdr[hn++] = (unsigned char)((FLV_SOUNDFMT_AAC << 4) | 0x0F); /* rate/size/type bits: ignored by AAC decoders, 44k/16-bit/stereo by convention */
    hdr[hn++] = FLV_AAC_SEQHDR;
    t->seqhdr_sent = 1;
    flv_send_tag(f, FLV_TAG_AUDIO, 0, hdr, hn, t->es.cpriv, t->es.cpriv_len);
    return;
  }
  /* AC-3/E-AC-3: no real config data, empty SequenceStart still expected */
  hdr[hn++] = (unsigned char)((FLV_SOUNDFMT_EXHEADER << 4) | FLV_EX_APT_SEQ_START);
  memcpy(hdr + hn, t->es.codec == CODEC_AC3 ? FOURCC_AC3 : FOURCC_EAC3, 4);
  hn += 4;
  t->seqhdr_sent = 1;
  flv_send_tag(f, FLV_TAG_AUDIO, 0, hdr, hn, NULL, 0);
}

void flv_emit_audio(flv_t *f, flv_track_t *t, const unsigned char *d, size_t n) {
  unsigned char hdr[5];
  size_t hn = 0;
  int64_t rel = t->ts_ms - f->t0;
  uint32_t ts = rel > 0 ? (uint32_t)rel : 0;

  if (!t->seqhdr_sent)
    audio_seqhdr(f, t);

  if (t->es.codec == CODEC_AAC || t->es.codec == CODEC_AAC_LATM) {
    hdr[hn++] = (unsigned char)((FLV_SOUNDFMT_AAC << 4) | 0x0F);
    hdr[hn++] = FLV_AAC_RAW;
  } else {
    hdr[hn++] = (unsigned char)((FLV_SOUNDFMT_EXHEADER << 4) | FLV_EX_APT_CODED_FRAMES);
    memcpy(hdr + hn, t->es.codec == CODEC_AC3 ? FOURCC_AC3 : FOURCC_EAC3, 4);
    hn += 4;
  }
  flv_send_tag(f, FLV_TAG_AUDIO, ts, hdr, hn, d, n);
}
