/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_MKV_PRIV_H
#define DVBIPITOOLS_LIB_MUX_MKV_PRIV_H

#include <stdint.h>

#include "../ebml.h"
#include "../teletext.h"
#include "lib/demux/pes.h"
#include "lib/demux/psi/psi.h"
#include "mkv.h"

#define MKV_MAX_TRACKS 8
#define MKV_MAX_PROGRAMS 8
#define MKV_PEND_MAX 1024
#define MKV_PEND_BYTES (8u * 1024 * 1024)
#define MKV_REM_MAX 65536
#define MKV_AU_MAX 8192
#define MKV_PS_MAX 512 /* SPS/PPS/VPS */
#define CLUSTER_MS 30000

typedef struct {
  unsigned pid;
  int num;
  codec_t codec;
  pid_class_t cls;
  char lang[4];
  char codecid[24];
  unsigned rate, channels;   /* a */
  unsigned width, height;    /* v */
  unsigned char cpriv[1024]; /* codecpriv: ASC/avcC/hvcC */
  size_t cpriv_len;
  int hdr_parsed;
  int64_t ts_ms;
  pts_unwrap_t pts;
  int psi_idx; /* owning m->psi[] index; TrackName looked up live at write_head(), not setup(); sdt may still be inbound */
  unsigned char *rem; /* audio: partial frame carry-over */
  size_t remlen, remcap;
  int latm_cfg_ok, latm_flt;
  unsigned char au[MKV_AU_MAX];
  /* video parameter sets */
  unsigned char vps[MKV_PS_MAX], sps[MKV_PS_MAX], pps[MKV_PS_MAX];
  size_t vpslen, spslen, ppslen;
  unsigned char ptl[12]; /* HEVC profile_tier_level */
  unsigned chroma;
  unsigned char *vbuf; /* video: length-prefixed AU */
  size_t vbuflen, vbufcap;
  int got_key; /* first keyframe seen */
  ttx_t *ttx;  /* damn teletext */
} track_t;

typedef struct {
  int num, key;
  int64_t ts, dur;
  unsigned char *data;
  size_t len;
} pend_t;

struct mkv {
  int fd;
  const mkv_opts_t *opts;
  int video_ok;
  unsigned long long *bytes;
  psi_t *psi[MKV_MAX_PROGRAMS];
  int npsi;
  pes_t *pes;
  track_t trk[MKV_MAX_TRACKS];
  int ntrk;
  int setup, started, err;
  int flushing; /* EOS: last PES partial */
  int ready_seen;
  int64_t ready_ms; /* when tracks first had headers */
  int64_t t0;
  pend_t pend[MKV_PEND_MAX];
  int npend;
  size_t pend_bytes;
  ebuf_t cl;
  int64_t cl_base;
  int cl_open;
};

typedef struct {
  size_t consumed;
  const unsigned char *out;
  size_t outlen;
  unsigned rate, ch, samples;
  int layer;
} frame_t;

typedef struct {
  const unsigned char *d;
  size_t len, bit;
  int err;
} br_t;

/* bitreader.c */
unsigned br_u(br_t *b, int n);
unsigned br_ue(br_t *b);
int br_se(br_t *b);
size_t br_slice(const br_t *b, size_t from, size_t to, unsigned char *out, size_t cap);
size_t rbsp_unescape(const unsigned char *s, size_t len, unsigned char *d, size_t cap);

/* audio.c */
int next_frame(track_t *t, const unsigned char *d, size_t len, frame_t *f);

/* video.c */
const char *codec_id_for(const track_t *t, const frame_t *f);
int h264_dims(const unsigned char *nal, size_t len, unsigned *w, unsigned *h);
int hevc_info(const unsigned char *nal, size_t len, unsigned char *ptl, unsigned *chroma, unsigned *w, unsigned *h);
size_t build_avcc(const track_t *t, unsigned char *o, size_t cap);
size_t build_hvcc(const track_t *t, unsigned char *o, size_t cap);

/* write.c */
void wfd(mkv_t *m, const void *p, size_t n);
track_t *find_track(mkv_t *m, unsigned pid);
void cluster_flush(mkv_t *m);
void put_block(mkv_t *m, int num, int64_t rel, const unsigned char *d, size_t n, int key, int64_t dur);
void start(mkv_t *m);
void pend_add(mkv_t *m, int num, int64_t ts, const unsigned char *d, size_t n, int key, int64_t dur);
void emit(mkv_t *m, track_t *t, const unsigned char *d, size_t n, int key);

/* feed.c */
void all_ready(mkv_t *m);
void on_pes(void *ctx, unsigned pid, int has_pts, uint64_t pts, const unsigned char *data, size_t len);
void setup(mkv_t *m);

#endif
