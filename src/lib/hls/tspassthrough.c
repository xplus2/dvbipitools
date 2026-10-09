/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "tspassthrough.h"

#include <string.h>

#include "lib/helper/log.h"
#include "lib/mux/esbuild/pmtbuild.h"
#include "lib/mux/esbuild/tspacketize.h"
#include "lib/mux/tspacket_write.h"

#define ADTS_HDR_MIN 7
#define ADTS_SAMPLES_PER_FRAME 1024
#define ID3_HDR 10
#define PES_STREAM_ID_AUDIO 0xC0
#define PES_BUF_CAP 8400

static const unsigned adts_rates[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};

struct fwd_ctx {
  hls_live_t *h;
};

static void on_ts_pkt(void *ctx, const unsigned char *pkt) { hls_live_emit(((struct fwd_ctx *)ctx)->h, pkt, 188); }

static int on_ts_packet(void *ctx, const unsigned char *pkt) {
  on_ts_pkt(ctx, pkt);
  return 0;
}

/* ID3v2 at d: returns tag size, 0 if none. HLS timestamp PRIV to *ts */
static size_t id3_skip(const unsigned char *d, size_t len, uint64_t *ts, int *have_ts) {
  size_t size;
  size_t pos = ID3_HDR;
  if (len < ID3_HDR || memcmp(d, "ID3", 3)) return 0;
  size = ((size_t)(d[6] & 0x7F) << 21) | ((size_t)(d[7] & 0x7F) << 14) | ((size_t)(d[8] & 0x7F) << 7) | (d[9] & 0x7F);
  size += ID3_HDR + ((d[5] & 0x10) ? ID3_HDR : 0);
  if (size > len) return len;
  while (pos + ID3_HDR <= size) {
    size_t fsz = d[3] == 4 ? (((size_t)(d[pos + 4] & 0x7F) << 21) | ((size_t)(d[pos + 5] & 0x7F) << 14) | ((size_t)(d[pos + 6] & 0x7F) << 7) | (d[pos + 7] & 0x7F)) : (((size_t)d[pos + 4] << 24) | ((size_t)d[pos + 5] << 16) | ((size_t)d[pos + 6] << 8) | d[pos + 7]);
    const unsigned char *f = d + pos + ID3_HDR;
    static const char owner[] = "com.apple.streaming.transportStreamTimestamp";
    if (!d[pos] || pos + ID3_HDR + fsz > size) break;
    if (!memcmp(d + pos, "PRIV", 4) && fsz >= sizeof owner + 8 && !memcmp(f, owner, sizeof owner)) {
      uint64_t v = 0;
      for (int i = 0; i < 8; i++) v = (v << 8) | f[sizeof owner + i];
      *ts = v & 0x1FFFFFFFFULL;
      *have_ts = 1;
    }
    pos += ID3_HDR + fsz;
  }
  return size;
}

static void emit_psi(hls_ts_passthrough_t *p, ts_packet_cb cb, void *ctx) {
  unsigned char sec[188];
  unsigned char ptr = 0;
  codec_t codec = CODEC_AAC;
  esbuild_es_t es;
  size_t n = esbuild_build_pat(1, 0, 1, sec, sizeof sec);
  if (n) ts_packet_emit(0x0000, &p->pat_cc, &ptr, sec, n, 0, 0, cb, ctx);
  esbuild_assign_pids(&codec, 1, &es);
  n = esbuild_build_pmt(0, 1, &es, 1, sec, sizeof sec);
  if (n) ts_packet_emit(ESBUILD_PMT_PID, &p->pmt_cc, &ptr, sec, n, 0, 0, cb, ctx);
}

static void adts_to_ts(hls_ts_passthrough_t *p, const unsigned char *d, size_t len, ts_packet_cb cb, void *ctx) {
  unsigned char pesbuf[PES_BUF_CAP];
  uint64_t ts = 0;
  int have_ts = 0;
  size_t off = id3_skip(d, len, &ts, &have_ts);
  unsigned emitted = 0;

  if (have_ts) {
    p->base_pts = ts;
    p->frames = 0;
  }
  emit_psi(p, cb, ctx);
  while (off + ADTS_HDR_MIN <= len) {
    const unsigned char *f = d + off;
    unsigned sri = (f[2] >> 2) & 0xF;
    size_t flen = ((size_t)(f[3] & 3) << 11) | ((size_t)f[4] << 3) | (f[5] >> 5);
    if (f[0] != 0xFF || (f[1] & 0xF6) != 0xF0 || sri >= 13 || flen < ADTS_HDR_MIN) {
      off++;
      continue;
    }
    if (off + flen > len) break;
    esbuild_ts_packetize(ESBUILD_FIRST_ES_PID, &p->es_cc, PES_STREAM_ID_AUDIO, (p->base_pts + p->frames * ADTS_SAMPLES_PER_FRAME * 90000ULL / adts_rates[sri]) & 0x1FFFFFFFFULL, 0, 0, f, flen, 0, 0, pesbuf, sizeof pesbuf, cb, ctx);
    p->frames++;
    emitted++;
    off += flen;
  }
  if (!emitted && !p->warned) {
    log_line_ansi("\e[0;31mHLS packed audio is not ADTS AAC, skipped\e[0m");
    p->warned = 1;
  }
}

void hls_ts_passthrough_feed(void *ctx, hls_live_t *h, const unsigned char *data, size_t len) {
  hls_ts_passthrough_t *p = ctx;
  struct fwd_ctx fc = {h};
  switch (hls_live_segment_kind(data, len)) {
  case HLS_SEG_TS:
    p->tspack.acclen = 0;
    tspack_feed(&p->tspack, data, len, on_ts_packet, &fc);
    break;
  case HLS_SEG_PACKED_AUDIO:
    adts_to_ts(p, data, len, on_ts_pkt, &fc);
    break;
  case HLS_SEG_FMP4:
  case HLS_SEG_UNKNOWN:
    if (!p->warned) {
      log_line_ansi("\e[0;31mHLS segment format not supported, skipped\e[0m");
      p->warned = 1;
    }
    break;
  }
}
