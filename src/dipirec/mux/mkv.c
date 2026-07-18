/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../version.h"
#include "ebml.h"
#include "lib/demux/pes.h"
#include "lib/demux/psi.h"
#include "lib/log.h"
#include "mkv.h"
#include "teletext.h"

#define MKV_MAX_TRACKS 8
#define MKV_PEND_MAX 1024
#define MKV_PEND_BYTES (8u * 1024 * 1024)
#define MKV_REM_MAX 65536
#define MKV_AU_MAX 8192
#define MKV_PS_MAX 512 /* SPS/PPS/VPS */
#define CLUSTER_MS 30000

/* AC-3 frame size in words: [frmsizecod][fscod 48/44.1/32k] */
static const unsigned short ac3_fsz[38][3] = {
    {64, 69, 96},       {64, 70, 96},       {80, 87, 120},
    {80, 88, 120},      {96, 104, 144},     {96, 105, 144},
    {112, 121, 168},    {112, 122, 168},    {128, 139, 192},
    {128, 140, 192},    {160, 174, 240},    {160, 175, 240},
    {192, 208, 288},    {192, 209, 288},    {224, 243, 336},
    {224, 244, 336},    {256, 278, 384},    {256, 279, 384},
    {320, 348, 480},    {320, 349, 480},    {384, 417, 576},
    {384, 418, 576},    {448, 487, 672},    {448, 488, 672},
    {512, 557, 768},    {512, 558, 768},    {640, 696, 960},
    {640, 697, 960},    {768, 835, 1152},   {768, 836, 1152},
    {896, 975, 1344},   {896, 976, 1344},   {1024, 1114, 1536},
    {1024, 1115, 1536}, {1152, 1253, 1728}, {1152, 1254, 1728},
    {1280, 1393, 1920}, {1280, 1394, 1920}};
static const unsigned ac3_rate[3] = {48000, 44100, 32000};
static const unsigned ac3_ch[8] = {2, 1, 2, 3, 3, 4, 4, 5};

/* MPEG audio bitrate kbps: [mpeg1?0:1][layer-1][index] */
static const unsigned short mpa_br[2][3][16] = {
    {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0},
     {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},
     {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}},
    {{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},
     {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
     {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}}};
static const unsigned mpa_sample_rates[4][3] = {{11025, 12000, 8000},{0, 0, 0},{22050, 24000, 16000},{44100, 48000, 32000}};
static const unsigned aac_sample_rates[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000,  7350};

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
  ttx_t *ttx;  /* the damn teletext */
} track_t;

typedef struct {
  int num, key;
  int64_t ts, dur;
  unsigned char *data;
  size_t len;
} pend_t;

struct mkv {
  int fd;
  const config_t *cfg;
  int video_ok;
  unsigned long long *bytes;
  psi_t *psi;
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

static int64_t now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void wfd(mkv_t *m, const void *p, size_t n) {
  const unsigned char *b = p;

  while (n && !m->err) {
    ssize_t w = write(m->fd, b, n);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      log_line("mkv write: %s", strerror(errno));
      m->err = 1;
      return;
    }
    b += w;
    n -= (size_t)w;
    *m->bytes += (unsigned long long)w;
  }
}

typedef struct {
  const unsigned char *d;
  size_t len, bit;
  int err;
} br_t;

static unsigned br_u(br_t *b, int n) {
  unsigned v = 0;
  while (n-- > 0) {
    size_t byi = b->bit >> 3;
    if (byi >= b->len) {
      b->err = 1;
      return v;
    }
    v = (v << 1) | ((b->d[byi] >> (7 - (b->bit & 7))) & 1);
    b->bit++;
  }
  return v;
}

static unsigned br_ue(br_t *b) {
  int lz = 0;
  while (lz < 32 && !b->err && !br_u(b, 1))
    lz++;
  return lz ? ((1u << lz) - 1 + br_u(b, lz)) : 0;
}

static int br_se(br_t *b) {
  unsigned v = br_ue(b);
  return (v & 1) ? (int)((v + 1) / 2) : -(int)(v / 2);
}

static size_t br_slice(const br_t *b, size_t from, size_t to, unsigned char *out, size_t cap) {
  size_t nbits = to - from, nb = (nbits + 7) / 8, i;

  if (!nb || nb > cap)
    return 0;
  memset(out, 0, nb);
  for (i = 0; i < nbits; i++) {
    size_t s = from + i;
    unsigned bit = (b->d[s >> 3] >> (7 - (s & 7))) & 1;
    out[i >> 3] |= (unsigned char)(bit << (7 - (i & 7)));
  }
  return nb;
}

/* strip emu-prevention */
static size_t rbsp_unescape(const unsigned char *s, size_t len, unsigned char *d, size_t cap) {
  size_t i, o = 0, zeros = 0;
  for (i = 0; i < len && o < cap; i++) {
    if (zeros >= 2 && s[i] == 0x03) {
      zeros = 0;
      continue;
    }
    d[o++] = s[i];
    zeros = (s[i] == 0) ? zeros + 1 : 0;
  }
  return o;
}

static int next_ac3(track_t *t, const unsigned char *d, size_t len, frame_t *f) {
  unsigned fscod, frmsizecod;

  (void)t;
  if (len < 7)
    return 1;
  if (d[0] != 0x0B || d[1] != 0x77)
    return -1;
  fscod = d[4] >> 6;
  frmsizecod = d[4] & 0x3F;
  if (fscod > 2 || frmsizecod > 37)
    return -1;
  f->consumed = (size_t)ac3_fsz[frmsizecod][fscod] * 2;
  if (len < f->consumed)
    return 1;
  f->out = d;
  f->outlen = f->consumed;
  f->rate = ac3_rate[fscod];
  f->ch = ac3_ch[d[6] >> 5];
  f->samples = 1536;
  return 0;
}

static int next_eac3(track_t *t, const unsigned char *d, size_t len, frame_t *f) {
  static const unsigned blk[4] = {1, 2, 3, 6};
  static const unsigned rate2[3] = {24000, 22050, 16000};
  unsigned frmsiz, fscod, numblkscod;

  (void)t;
  if (len < 6)
    return 1;
  if (d[0] != 0x0B || d[1] != 0x77)
    return -1;
  frmsiz = ((unsigned)(d[2] & 0x07) << 8) | d[3];
  f->consumed = ((size_t)frmsiz + 1) * 2;
  if (f->consumed < 6)
    return -1;
  if (len < f->consumed)
    return 1;
  fscod = d[4] >> 6;
  numblkscod = (d[4] >> 4) & 3;
  if (fscod == 3) {
    if (numblkscod > 2)
      return -1;
    f->rate = rate2[numblkscod];
    f->samples = 6 * 256;
  } else {
    f->rate = ac3_rate[fscod];
    f->samples = blk[numblkscod] * 256;
  }
  f->ch = ac3_ch[(d[4] >> 1) & 7];
  f->out = d;
  f->outlen = f->consumed;
  return 0;
}

static int next_mpa(track_t *t, const unsigned char *d, size_t len, frame_t *f) {
  unsigned ver, lay, bri, sri, pad, br, sr;
  int mpeg1, ly;

  (void)t;
  if (len < 4)
    return 1;
  if (d[0] != 0xFF || (d[1] & 0xE0) != 0xE0)
    return -1;
  ver = (d[1] >> 3) & 3;
  lay = (d[1] >> 1) & 3;
  bri = (d[2] >> 4) & 0x0F;
  sri = (d[2] >> 2) & 3;
  pad = (d[2] >> 1) & 1;
  if (ver == 1 || lay == 0 || bri == 0 || bri == 15 || sri == 3)
    return -1;
  mpeg1 = (ver == 3);
  ly = 4 - (int)lay;
  br = mpa_br[mpeg1 ? 0 : 1][ly - 1][bri] * 1000u;
  sr = mpa_sample_rates[ver][sri];
  if (!br || !sr)
    return -1;
  if (ly == 1)
    f->consumed = (12 * br / sr + pad) * 4;
  else if (ly == 2)
    f->consumed = 144 * br / sr + pad;
  else
    f->consumed = (mpeg1 ? 144u : 72u) * br / sr + pad;
  if (f->consumed < 4)
    return -1;
  if (len < f->consumed)
    return 1;
  f->samples = (ly == 1) ? 384 : (ly == 2) ? 1152 : (mpeg1 ? 1152 : 576);
  f->rate = sr;
  f->ch = (((d[3] >> 6) & 3) == 3) ? 1 : 2;
  f->layer = ly;
  f->out = d;
  f->outlen = f->consumed;
  return 0;
}

static int next_aac(track_t *t, const unsigned char *d, size_t len, frame_t *f) {
  unsigned prof, sfi, chcfg, fl, hl;

  if (len < 7)
    return 1;
  if (d[0] != 0xFF || (d[1] & 0xF6) != 0xF0)
    return -1;
  prof = (d[2] >> 6) & 3;
  sfi = (d[2] >> 2) & 0x0F;
  chcfg = (unsigned)((d[2] & 1) << 2) | ((d[3] >> 6) & 3);
  fl = ((unsigned)(d[3] & 3) << 11) | ((unsigned)d[4] << 3) | (d[5] >> 5);
  hl = (d[1] & 1) ? 7 : 9;
  if (sfi > 12 || fl <= hl)
    return -1;
  if (len < fl)
    return 1;
  if (!t->cpriv_len) {
    unsigned aot = prof + 1;
    t->cpriv[0] = (unsigned char)((aot << 3) | (sfi >> 1));
    t->cpriv[1] = (unsigned char)(((sfi & 1) << 7) | (chcfg << 3));
    t->cpriv_len = 2;
  }
  f->consumed = fl;
  f->out = d + hl;
  f->outlen = fl - hl;
  f->rate = aac_sample_rates[sfi];
  f->ch = chcfg ? chcfg : 2;
  f->samples = 1024;
  return 0;
}

static int latm_cfg(br_t *b, track_t *t) {
  unsigned amv, sfi, ch, aot;
  size_t asc_start, asc_end;

  amv = br_u(b, 1);
  if (amv) {
    if (br_u(b, 1))
      return -1;
    {
      unsigned n = br_u(b, 2), i;
      for (i = 0; i <= n; i++)
        br_u(b, 8);
    }
  }
  if (!br_u(b, 1))
    return -1;
  if (br_u(b, 6) || br_u(b, 4) || br_u(b, 3))
    return -1;

  asc_start = b->bit;
  aot = br_u(b, 5);
  if (aot == 31)
    aot = 32 + br_u(b, 6);
  sfi = br_u(b, 4);
  if (sfi == 15)
    return -1;
  ch = br_u(b, 4);
  br_u(b, 1);
  if (br_u(b, 1))
    br_u(b, 14);
  if (br_u(b, 1))
    return -1;
  if (aot == 5 || aot == 29) {
    if (br_u(b, 11) == 0x2B7) {
      unsigned ext = br_u(b, 5);
      if (ext == 5 && br_u(b, 1)) {
        if (br_u(b, 4) == 15)
          br_u(b, 24);
      }
    }
  }
  asc_end = b->bit;
  if (b->err || sfi > 12)
    return -1;
  t->cpriv_len = br_slice(b, asc_start, asc_end, t->cpriv, sizeof t->cpriv);
  if (!t->cpriv_len)
    return -1;
  t->rate = aac_sample_rates[sfi];
  t->channels = ch ? ch : 2;

  t->latm_flt = (int)br_u(b, 3);
  if (t->latm_flt == 0)
    br_u(b, 8);
  else
    return -1;
  if (br_u(b, 1)) {
    unsigned esc;
    do {
      esc = br_u(b, 1);
      br_u(b, 8);
    } while (esc && !b->err);
  }
  if (br_u(b, 1))
    br_u(b, 8);
  return b->err ? -1 : 0;
}

static int next_latm(track_t *t, const unsigned char *d, size_t len, frame_t *f) {
  br_t b;
  size_t total, plen = 0, i;
  unsigned v;

  if (len < 3)
    return 1;
  if (d[0] != 0x56 || (d[1] & 0xE0) != 0xE0)
    return -1;
  total = 3 + (size_t)((((unsigned)d[1] & 0x1F) << 8) | d[2]);
  if (len < total)
    return 1;
  b.d = d + 3;
  b.len = total - 3;
  b.bit = 0;
  b.err = 0;
  if (br_u(&b, 1) == 0) {
    if (latm_cfg(&b, t))
      return -1;
    t->latm_cfg_ok = 1;
  } else if (!t->latm_cfg_ok) {
    return -1;
  }
  do {
    v = br_u(&b, 8);
    plen += v;
  } while (v == 255 && !b.err);
  if (b.err || !plen || plen > sizeof t->au)
    return -1;
  for (i = 0; i < plen; i++)
    t->au[i] = (unsigned char)br_u(&b, 8);
  if (b.err)
    return -1;
  f->consumed = total;
  f->out = t->au;
  f->outlen = plen;
  f->rate = t->rate;
  f->ch = t->channels;
  f->samples = 1024;
  return 0;
}

static int next_frame(track_t *t, const unsigned char *d, size_t len, frame_t *f) {
  memset(f, 0, sizeof *f);
  f->layer = 2;
  switch (t->codec) {
    case CODEC_AC3:         return next_ac3(t, d, len, f);
    case CODEC_EAC3:        return next_eac3(t, d, len, f);
    case CODEC_MP2A:        return next_mpa(t, d, len, f);
    case CODEC_AAC:         return next_aac(t, d, len, f);
    case CODEC_AAC_LATM:    return next_latm(t, d, len, f);
    default:                return -1;
  }
}

static const char *codec_id_for(const track_t *t, const frame_t *f) {
  static char buf[24];
  /* might be incomplete, reflects what's available for testing ...
     "some ipi-providers are always trying to ice skate uphill" */
  switch (t->codec) {
    case CODEC_AC3:           return "A_AC3";
    case CODEC_EAC3:          return "A_EAC3";
    case CODEC_AAC:
    case CODEC_AAC_LATM:      return "A_AAC";
    case CODEC_H264:          return "V_MPEG4/ISO/AVC";
    case CODEC_HEVC:          return "V_MPEGH/ISO/HEVC";
    case CODEC_MPEG2V:        return "V_MPEG2";
    case CODEC_MP2A:          snprintf(buf, sizeof buf, "A_MPEG/L%d", f ? f->layer : 2);
                              return buf;
    default:                  return "S_UNKNOWN";
  }
}

/* H.264 SPS -> dimensions */
static int h264_dims(const unsigned char *nal, size_t len, unsigned *w, unsigned *h) {
  unsigned char rb[MKV_PS_MAX];
  br_t b;
  unsigned profile, chroma = 1, wmbs, hmus, fmo, poc;
  unsigned cl = 0, cr = 0, ct = 0, cb = 0, subw, subh;
  b.len = rbsp_unescape(nal, len, rb, sizeof rb);
  b.d = rb;
  b.bit = 0;
  b.err = 0;
  br_u(&b, 8); /* NAL header */
  profile = br_u(&b, 8);
  br_u(&b, 16); /* constraints + level */
  br_ue(&b);    /* sps id */
  if (profile == 100 || profile == 110 || profile == 122 || profile == 244 ||
      profile == 44 || profile == 83 || profile == 86 || profile == 118 ||
      profile == 128 || profile == 138 || profile == 139 || profile == 134 ||
      profile == 135) {
    chroma = br_ue(&b);
    if (chroma == 3)
      br_u(&b, 1);
    br_ue(&b);
    br_ue(&b);
    br_u(&b, 1);
    if (br_u(&b, 1)) { /* scaling matrices */
      int n = (chroma != 3) ? 8 : 12, k, j;
      for (k = 0; k < n; k++)
        if (br_u(&b, 1)) {
          int sz = (k < 6) ? 16 : 64, last = 8, next = 8;
          for (j = 0; j < sz; j++) {
            if (next)
              next = (last + br_se(&b) + 256) % 256;
            last = next ? next : last;
          }
        }
    }
  }
  br_ue(&b); /* log2_max_frame_num_minus4 */
  poc = br_ue(&b);
  if (poc == 0) {
    br_ue(&b);
  } else if (poc == 1) {
    unsigned n, k;
    br_u(&b, 1);
    br_se(&b);
    br_se(&b);
    n = br_ue(&b);
    for (k = 0; k < n && !b.err; k++)
      br_se(&b);
  }
  br_ue(&b); /* max_num_ref_frames */
  br_u(&b, 1);
  wmbs = br_ue(&b);
  hmus = br_ue(&b);
  fmo = br_u(&b, 1);
  if (!fmo)
    br_u(&b, 1);
  br_u(&b, 1);
  if (br_u(&b, 1)) {
    cl = br_ue(&b);
    cr = br_ue(&b);
    ct = br_ue(&b);
    cb = br_ue(&b);
  }
  if (b.err)
    return -1;
  subw = (chroma == 1 || chroma == 2) ? 2 : 1;
  subh = (chroma == 1) ? 2 : 1;
  if (chroma == 0)
    subw = subh = 1;
  *w = (wmbs + 1) * 16 - (cl + cr) * subw;
  *h = (2 - fmo) * (hmus + 1) * 16 - (ct + cb) * subh * (2 - fmo);
  return (*w && *h) ? 0 : -1;
}

/* HEVC SPS -> profile_tier_level, chroma, dimensions */
static int hevc_info(const unsigned char *nal, size_t len, unsigned char *ptl, unsigned *chroma, unsigned *w, unsigned *h) {
  unsigned char rb[MKV_PS_MAX];
  br_t b;
  unsigned maxsub, i, sp[8], sl[8];
  b.len = rbsp_unescape(nal, len, rb, sizeof rb);
  b.d = rb;
  b.bit = 0;
  b.err = 0;
  br_u(&b, 16); /* 2-byte NAL header */
  br_u(&b, 4);  /* sps_video_parameter_set_id */
  maxsub = br_u(&b, 3);
  br_u(&b, 1);
  for (i = 0; i < 12; i++)
    ptl[i] = (unsigned char)br_u(&b, 8);
  if (maxsub > 0) {
    if (maxsub > 7)
      return -1;
    for (i = 0; i < maxsub; i++) {
      sp[i] = br_u(&b, 1);
      sl[i] = br_u(&b, 1);
    }
    for (i = maxsub; i < 8; i++)
      br_u(&b, 2);
    for (i = 0; i < maxsub; i++) {
      if (sp[i]) {
        br_u(&b, 32);
        br_u(&b, 32);
        br_u(&b, 24);
      }
      if (sl[i])
        br_u(&b, 8);
    }
  }
  br_ue(&b); /* sps_seq_parameter_set_id */
  *chroma = br_ue(&b);
  if (*chroma == 3)
    br_u(&b, 1);
  *w = br_ue(&b);
  *h = br_ue(&b);
  if (br_u(&b, 1)) { /* conformance window */
    unsigned l = br_ue(&b), r = br_ue(&b), t = br_ue(&b), bo = br_ue(&b);
    unsigned subw = (*chroma == 1 || *chroma == 2) ? 2 : 1;
    unsigned subh = (*chroma == 1) ? 2 : 1;
    *w -= (l + r) * subw;
    *h -= (t + bo) * subh;
  }
  if (b.err || !*w || !*h)
    return -1;
  return 0;
}

static size_t build_avcc(const track_t *t, unsigned char *o, size_t cap) {
  size_t n = 0;

  if (t->spslen < 4 || 11 + t->spslen + t->ppslen > cap)
    return 0;
  o[n++] = 1;
  o[n++] = t->sps[1]; /* profile */
  o[n++] = t->sps[2]; /* compat */
  o[n++] = t->sps[3]; /* level */
  o[n++] = 0xFF;      /* 4-byte NALU length */
  o[n++] = 0xE1;      /* 1 SPS */
  o[n++] = (unsigned char)(t->spslen >> 8);
  o[n++] = (unsigned char)t->spslen;
  memcpy(o + n, t->sps, t->spslen);
  n += t->spslen;
  o[n++] = 1; /* 1 PPS */
  o[n++] = (unsigned char)(t->ppslen >> 8);
  o[n++] = (unsigned char)t->ppslen;
  memcpy(o + n, t->pps, t->ppslen);
  n += t->ppslen;
  return n;
}

static size_t build_hvcc(const track_t *t, unsigned char *o, size_t cap) {
  static const unsigned char types[3] = {32, 33, 34};
  const unsigned char *ps[3];
  size_t pl[3], n = 0, i;

  ps[0] = t->vps;
  pl[0] = t->vpslen;
  ps[1] = t->sps;
  pl[1] = t->spslen;
  ps[2] = t->pps;
  pl[2] = t->ppslen;
  if (23 + pl[0] + pl[1] + pl[2] + 15 > cap)
    return 0;
  o[n++] = 1;
  memcpy(o + n, t->ptl, 12);
  n += 12;
  o[n++] = 0xF0; /* min_spatial_segmentation_idc = 0 */
  o[n++] = 0x00;
  o[n++] = 0xFC; /* parallelismType 0 */
  o[n++] = (unsigned char)(0xFC | (t->chroma & 3));
  o[n++] = 0xF8; /* bitDepthLumaMinus8 0 */
  o[n++] = 0xF8; /* bitDepthChromaMinus8 0 */
  o[n++] = 0x00; /* avgFrameRate */
  o[n++] = 0x00;
  o[n++] = 0x03; /* 4-byte NALU length */
  o[n++] = 3;    /* numOfArrays */
  for (i = 0; i < 3; i++) {
    o[n++] = types[i];
    o[n++] = 0;
    o[n++] = 1;
    o[n++] = (unsigned char)(pl[i] >> 8);
    o[n++] = (unsigned char)pl[i];
    memcpy(o + n, ps[i], pl[i]);
    n += pl[i];
  }
  return n;
}

static track_t *find_track(mkv_t *m, unsigned pid) {
  int i;
  for (i = 0; i < m->ntrk; i++)
    if (m->trk[i].pid == pid)
      return &m->trk[i];
  return NULL;
}

static void cluster_flush(mkv_t *m) {
  ebuf_t out;

  if (!m->cl_open)
    return;
  memset(&out, 0, sizeof out);
  eb_id(&out, 0x1F43B675);
  eb_size(&out, m->cl.len);
  eb_bytes(&out, m->cl.p, m->cl.len);
  wfd(m, out.p, out.len);
  ebuf_free(&out);
  ebuf_free(&m->cl);
  m->cl_open = 0;
}

static void put_block(mkv_t *m, int num, int64_t rel, const unsigned char *d, size_t n, int key, int64_t dur) {
  unsigned char hdr[4];
  int16_t tc;

  /* new cluster once offset leaves int16 range */
  if (!m->cl_open || rel - m->cl_base >= CLUSTER_MS || rel - m->cl_base < -30000) {
    cluster_flush(m);
    if (rel < 0)
      rel = 0;
    eb_uint(&m->cl, 0xE7, (uint64_t)rel);
    m->cl_base = rel;
    m->cl_open = 1;
  }
  tc = (int16_t)(rel - m->cl_base);
  hdr[0] = (unsigned char)(0x80 | num);
  hdr[1] = (unsigned char)(tc >> 8);
  hdr[2] = (unsigned char)tc;
  hdr[3] = key ? 0x80 : 0x00;
  if (dur > 0) { /* explicit duration */
    ebuf_t bg;
    memset(&bg, 0, sizeof bg);
    hdr[3] = 0x00; /* Block: no keyframe flag */
    eb_id(&bg, 0xA1);
    eb_size(&bg, sizeof hdr + n);
    eb_bytes(&bg, hdr, sizeof hdr);
    eb_bytes(&bg, d, n);
    eb_uint(&bg, 0x9B, (uint64_t)dur); /* BlockDuration */
    eb_master(&m->cl, 0xA0, &bg);      /* BlockGroup */
    return;
  }
  eb_id(&m->cl, 0xA3);
  eb_size(&m->cl, sizeof hdr + n);
  eb_bytes(&m->cl, hdr, sizeof hdr);
  eb_bytes(&m->cl, d, n);
}

static void simpletag(ebuf_t *tag, const char *name, const char *val) {
  ebuf_t st;
  if (!val || !*val)
    return;
  memset(&st, 0, sizeof st);
  eb_str(&st, 0x45A3, name);
  eb_str(&st, 0x447A, "und");
  eb_str(&st, 0x4487, val);
  eb_master(tag, 0x67C8, &st);
}

static void write_head(mkv_t *m) {
  static const unsigned char seg[] = {0x18, 0x53, 0x80, 0x67, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  char app[64], srcuri[1024], date[32];
  ebuf_t b, e;
  time_t now = time(NULL);
  struct tm tm;
  int i;

  snprintf(app, sizeof app, "%s %s", TOOL_NAME, TOOL_VERSION);
  source_describe(&m->cfg->source, srcuri, sizeof srcuri);
  gmtime_r(&now, &tm);
  strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", &tm);
  memset(&b, 0, sizeof b);
  memset(&e, 0, sizeof e);
  eb_uint(&e, 0x4286, 1);
  eb_uint(&e, 0x42F7, 1);
  eb_uint(&e, 0x42F2, 4);
  eb_uint(&e, 0x42F3, 8);
  eb_str(&e, 0x4282, "matroska");
  eb_uint(&e, 0x4287, 4);
  eb_uint(&e, 0x4285, 2);
  eb_master(&b, 0x1A45DFA3, &e);
  wfd(m, b.p, b.len);
  ebuf_free(&b);

  wfd(m, seg, sizeof seg);
  memset(&b, 0, sizeof b);
  memset(&e, 0, sizeof e);
  eb_uint(&e, 0x2AD7B1, 1000000);
  eb_str(&e, 0x4D80, app);
  eb_str(&e, 0x5741, app);
  eb_uint(&e, 0x4461, (uint64_t)((int64_t)(now - 978307200) * 1000000000LL));
  if (*psi_service_name(m->psi))
    eb_str(&e, 0x7BA9, psi_service_name(m->psi));
  eb_master(&b, 0x1549A966, &e);
  wfd(m, b.p, b.len);
  ebuf_free(&b);

  memset(&b, 0, sizeof b);
  memset(&e, 0, sizeof e);
  for (i = 0; i < m->ntrk; i++) {
    track_t *t = &m->trk[i];
    ebuf_t te, sub;
    int video = (t->cls == PID_VIDEO);
    memset(&te, 0, sizeof te);
    memset(&sub, 0, sizeof sub);
    eb_uint(&te, 0xD7, (uint64_t)t->num);
    eb_uint(&te, 0x73C5, (uint64_t)t->num);
    eb_uint(&te, 0x83, (t->cls == PID_TELETEXT) ? 17 : (video ? 1 : 2));
    eb_uint(&te, 0x9C, 0);
    eb_str(&te, 0x22B59C, t->lang[0] ? t->lang : "und");
    eb_str(&te, 0x86, t->codecid);
    if (t->cpriv_len)
      eb_bin(&te, 0x63A2, t->cpriv, t->cpriv_len);
    if (t->cls == PID_TELETEXT) {
      ebuf_free(&sub); /* no settings element */
    } else if (video) {
      eb_uint(&sub, 0xB0, t->width ? t->width : 720);
      eb_uint(&sub, 0xBA, t->height ? t->height : 576);
      eb_master(&te, 0xE0, &sub);
    } else {
      eb_float(&sub, 0xB5, (double)(t->rate ? t->rate : 48000));
      eb_uint(&sub, 0x9F, t->channels ? t->channels : 2);
      eb_master(&te, 0xE1, &sub);
    }
    eb_master(&e, 0xAE, &te);
  }
  eb_master(&b, 0x1654AE6B, &e);
  wfd(m, b.p, b.len);
  ebuf_free(&b);
  memset(&b, 0, sizeof b);
  memset(&e, 0, sizeof e);
  {
    ebuf_t tag, tgt;
    memset(&tag, 0, sizeof tag);
    memset(&tgt, 0, sizeof tgt);
    eb_uint(&tgt, 0x68CA, 50);
    eb_master(&tag, 0x63C0, &tgt);
    simpletag(&tag, "TITLE", psi_service_name(m->psi));
    simpletag(&tag, "NETWORK", psi_network_name(m->psi));
    simpletag(&tag, "PROVIDER", psi_provider_name(m->psi));
    simpletag(&tag, "SOURCE", srcuri);
    simpletag(&tag, "DATE_RECORDED", date);
    eb_master(&e, 0x7373, &tag);
  }
  eb_master(&b, 0x1254C367, &e);
  wfd(m, b.p, b.len);
  ebuf_free(&b);
}

static void start(mkv_t *m) {
  int i;

  if (m->started || !m->ntrk)
    return;
  m->t0 = 0;
  if (m->npend) {
    int vnum = 0, found = 0;
    int64_t vt = 0;
    for (i = 0; i < m->ntrk; i++)
      if (m->trk[i].cls == PID_VIDEO) {
        vnum = m->trk[i].num;
        break;
      }
    m->t0 = m->pend[0].ts;
    for (i = 1; i < m->npend; i++)
      if (m->pend[i].ts < m->t0)
        m->t0 = m->pend[i].ts;
    /* with video: start at first keyframe */
    for (i = 0; vnum && i < m->npend; i++)
      if (m->pend[i].num == vnum && (!found || m->pend[i].ts < vt)) {
        vt = m->pend[i].ts;
        found = 1;
      }
    if (found)
      m->t0 = vt;
  }
  write_head(m);
  m->started = 1;
  for (i = 0; i < m->npend; i++) {
    if (m->pend[i].ts >= m->t0)
      put_block(m, m->pend[i].num, m->pend[i].ts - m->t0, m->pend[i].data, m->pend[i].len, m->pend[i].key, m->pend[i].dur);
    free(m->pend[i].data);
  }
  m->npend = 0;
  m->pend_bytes = 0;
}

static void pend_add(mkv_t *m, int num, int64_t ts, const unsigned char *d, size_t n, int key, int64_t dur) {
  pend_t *p;

  if (m->npend >= MKV_PEND_MAX || m->pend_bytes + n > MKV_PEND_BYTES) {
    start(m); /* give up on silent track */
    if (m->started)
      put_block(m, num, ts - m->t0, d, n, key, dur);
    return;
  }
  p = &m->pend[m->npend];
  p->data = malloc(n);
  if (!p->data)
    return;
  memcpy(p->data, d, n);
  p->num = num;
  p->ts = ts;
  p->len = n;
  p->key = key;
  p->dur = dur;
  m->npend++;
  m->pend_bytes += n;
}

static void emit(mkv_t *m, track_t *t, const unsigned char *d, size_t n, int key) {
  if (!m->started) {
    pend_add(m, t->num, t->ts_ms, d, n, key, 0);
    return;
  }
  if (t->ts_ms < m->t0) /* before start point */
    return;
  put_block(m, t->num, t->ts_ms - m->t0, d, n, key, 0);
}

/* finished subtitle -> one timed block */
static void on_cue(void *ctx, const ttx_cue_t *cue) {
  mkv_t *m = ctx;
  int64_t dur = cue->end_ms - cue->start_ms;
  size_t n = strlen(cue->text);
  int i;

  if (!n || dur <= 0)
    return;
  for (i = 0; i < m->ntrk; i++) {
    track_t *t = &m->trk[i];
    if (t->cls != PID_TELETEXT)
      continue;
    if (!m->started) {
      pend_add(m, t->num, cue->start_ms, (const unsigned char *)cue->text, n, 1, dur);
      return;
    }
    if (cue->end_ms <= m->t0) /* wholly before the recording */
      return;
    if (cue->start_ms < m->t0) { /* --sub-lead pulled it in: clip, keep it */
      dur = cue->end_ms - m->t0;
      put_block(m, t->num, 0, (const unsigned char *)cue->text, n, 1, dur);
      return;
    }
    put_block(m, t->num, cue->start_ms - m->t0, (const unsigned char *)cue->text, n, 1, dur);
    return;
  }
}

/* start: all track headers present, video keyframe seen.
   brief SDT wait -> service name for Title/tags */
static void all_ready(mkv_t *m) {
  int i;

  for (i = 0; i < m->ntrk; i++) {
    const track_t *t = &m->trk[i];
    if (!t->hdr_parsed)
      return;
    if (t->cls == PID_VIDEO && !t->got_key)
      return;
  }
  if (!m->ready_seen) {
    m->ready_seen = 1;
    m->ready_ms = now_ms();
  }
  if (!psi_have_sdt(m->psi) && now_ms() - m->ready_ms < 2000)
    return;
  start(m);
}

static int rem_append(track_t *t, const unsigned char *d, size_t n) {
  if (t->remlen + n > t->remcap) {
    size_t nc = t->remcap ? t->remcap * 2 : 8192;
    unsigned char *np;
    while (nc < t->remlen + n)
      nc *= 2;
    np = realloc(t->rem, nc);
    if (!np)
      return -1;
    t->rem = np;
    t->remcap = nc;
  }
  memcpy(t->rem + t->remlen, d, n);
  t->remlen += n;
  return 0;
}

static int vbuf_add(track_t *t, const unsigned char *nal, size_t n) {
  size_t need = t->vbuflen + 4 + n;

  if (need > t->vbufcap) {
    size_t nc = t->vbufcap ? t->vbufcap * 2 : 65536;
    unsigned char *np;
    while (nc < need)
      nc *= 2;
    np = realloc(t->vbuf, nc);
    if (!np)
      return -1;
    t->vbuf = np;
    t->vbufcap = nc;
  }
  t->vbuf[t->vbuflen++] = (unsigned char)(n >> 24);
  t->vbuf[t->vbuflen++] = (unsigned char)(n >> 16);
  t->vbuf[t->vbuflen++] = (unsigned char)(n >> 8);
  t->vbuf[t->vbuflen++] = (unsigned char)n;
  memcpy(t->vbuf + t->vbuflen, nal, n);
  t->vbuflen += n;
  return 0;
}

static void ps_store(unsigned char *dst, size_t *dlen, const unsigned char *s, size_t n) {
  if (n && n <= MKV_PS_MAX) {
    memcpy(dst, s, n);
    *dlen = n;
  }
}

static size_t find_startcode(const unsigned char *d, size_t len, size_t from, size_t *sclen) {
  size_t i;

  for (i = from; i + 3 <= len; i++) {
    if (d[i] || d[i + 1])
      continue;
    if (d[i + 2] == 1) {
      *sclen = 3;
      return i;
    }
    if (i + 4 <= len && d[i + 2] == 0 && d[i + 3] == 1) {
      *sclen = 4;
      return i;
    }
  }
  return len;
}

/* one video PES = one Annex-B access unit */
static void handle_video(mkv_t *m, track_t *t, int has_pts, uint64_t pts, const unsigned char *d, size_t len) {
  size_t p, scl = 0;
  int key = 0;

  /* EOS flush: cut-off picture */
  if (m->flushing)
    return;
  if (has_pts)
    t->ts_ms = (int64_t)(pts / 90);
  t->vbuflen = 0;

  p = find_startcode(d, len, 0, &scl);
  while (p < len) {
    size_t ns = p + scl, scl2 = 0;
    size_t q = find_startcode(d, len, ns, &scl2);
    size_t n = q - ns;
    unsigned type;

    if (!n) {
      p = q;
      scl = scl2;
      continue;
    }
    if (t->codec == CODEC_H264) {
      type = d[ns] & 0x1F;
      if (type == 7)
        ps_store(t->sps, &t->spslen, d + ns, n);
      else if (type == 8)
        ps_store(t->pps, &t->ppslen, d + ns, n);
      else if (type != 9 && type != 12) {
        if (type == 5)
          key = 1;
        vbuf_add(t, d + ns, n);
      }
    } else {
      type = (d[ns] >> 1) & 0x3F;
      if (type == 32)
        ps_store(t->vps, &t->vpslen, d + ns, n);
      else if (type == 33)
        ps_store(t->sps, &t->spslen, d + ns, n);
      else if (type == 34)
        ps_store(t->pps, &t->ppslen, d + ns, n);
      else if (type != 35 && type != 38) {
        if (type >= 16 && type <= 21)
          key = 1;
        vbuf_add(t, d + ns, n);
      }
    }
    p = q;
    scl = scl2;
  }

  if (!t->hdr_parsed) {
    if (t->codec == CODEC_H264 && t->spslen && t->ppslen) {
      if (h264_dims(t->sps, t->spslen, &t->width, &t->height) == 0) {
        t->cpriv_len = build_avcc(t, t->cpriv, sizeof t->cpriv);
        if (t->cpriv_len) {
          snprintf(t->codecid, sizeof t->codecid, "%s", codec_id_for(t, NULL));
          t->hdr_parsed = 1;
          all_ready(m);
        }
      }
    } else if (t->codec == CODEC_HEVC && t->vpslen && t->spslen && t->ppslen) {
      if (hevc_info(t->sps, t->spslen, t->ptl, &t->chroma, &t->width,&t->height) == 0) {
        t->cpriv_len = build_hvcc(t, t->cpriv, sizeof t->cpriv);
        if (t->cpriv_len) {
          snprintf(t->codecid, sizeof t->codecid, "%s", codec_id_for(t, NULL));
          t->hdr_parsed = 1;
          all_ready(m);
        }
      }
    }
  }
  /* undecodable before first keyframe */
  if (key)
    t->got_key = 1;
  if (t->hdr_parsed && t->got_key && t->vbuflen)
    emit(m, t, t->vbuf, t->vbuflen, key);
}

/* V_MPEG2: raw ES, start codes intact, no length-prefix/CodecPrivate.
   one PES = one picture, same as handle_video */
static void handle_mpeg2(mkv_t *m, track_t *t, int has_pts, uint64_t pts, const unsigned char *d, size_t len) {
  size_t p, scl = 0;
  int key = 0;

  if (m->flushing)
    return;
  if (has_pts)
    t->ts_ms = (int64_t)(pts / 90);

  p = find_startcode(d, len, 0, &scl);
  while (p < len) {
    size_t ns = p + scl, scl2 = 0;
    size_t q = find_startcode(d, len, ns, &scl2);
    unsigned code = (ns < len) ? d[ns] : 0xFF;

    if (code == 0xB3 && !t->hdr_parsed && q >= ns + 4) {
      unsigned w = ((unsigned)d[ns + 1] << 4) | (d[ns + 2] >> 4);
      unsigned h = ((unsigned)(d[ns + 2] & 0x0F) << 8) | d[ns + 3];
      if (w && h) {
        t->width = w;
        t->height = h;
        snprintf(t->codecid, sizeof t->codecid, "%s", codec_id_for(t, NULL));
        t->hdr_parsed = 1;
        all_ready(m);
      }
    } else if (code == 0x00 && q >= ns + 3) {
      if (((d[ns + 2] >> 3) & 0x07) == 1) /* picture_coding_type: 1 = I */
        key = 1;
    }
    p = q;
    scl = scl2;
  }

  if (key)
    t->got_key = 1;
  if (t->hdr_parsed && t->got_key)
    emit(m, t, d, len, key);
}

static void handle_audio(mkv_t *m, track_t *t, int has_pts, uint64_t pts, const unsigned char *data, size_t len) {
  size_t pos = 0;

  if (has_pts && t->remlen == 0) /* anchor on frame boundary */
    t->ts_ms = (int64_t)(pts / 90);
  if (t->remlen > MKV_REM_MAX)
    t->remlen = 0;
  if (rem_append(t, data, len))
    return;

  while (pos < t->remlen) {
    frame_t f;
    int r = next_frame(t, t->rem + pos, t->remlen - pos, &f);
    if (r > 0)
      break;
    if (r < 0) {
      pos++;
      continue;
    }
    if (!t->hdr_parsed) {
      if (f.rate)
        t->rate = f.rate;
      if (f.ch)
        t->channels = f.ch;
      snprintf(t->codecid, sizeof t->codecid, "%s", codec_id_for(t, &f));
      t->hdr_parsed = 1;
      all_ready(m);
    }
    if (f.outlen)
      emit(m, t, f.out, f.outlen, 1);
    if (t->rate && f.samples)
      t->ts_ms += (int64_t)f.samples * 1000 / (int64_t)t->rate;
    pos += f.consumed;
  }
  if (pos) {
    memmove(t->rem, t->rem + pos, t->remlen - pos);
    t->remlen -= pos;
  }
}

static void on_pes(void *ctx, unsigned pid, int has_pts, uint64_t pts, const unsigned char *data, size_t len) {
  mkv_t *m = ctx;
  track_t *t = find_track(m, pid);

  if (!t || m->err)
    return;
  if (t->cls == PID_TELETEXT)
    ttx_pes(t->ttx, has_pts, pts, data, len);
  else if (t->cls == PID_VIDEO && t->codec == CODEC_MPEG2V)
    handle_mpeg2(m, t, has_pts, pts, data, len);
  else if (t->cls == PID_VIDEO)
    handle_video(m, t, has_pts, pts, data, len);
  else
    handle_audio(m, t, has_pts, pts, data, len);
}

static int audio_supported(codec_t c) {
  return c == CODEC_AC3 || c == CODEC_EAC3 || c == CODEC_MP2A || c == CODEC_AAC || c == CODEC_AAC_LATM;
}

static void add_track(mkv_t *m, const psi_es_t *es) {
  track_t *t = &m->trk[m->ntrk];

  memset(t, 0, sizeof *t);
  t->pid = es->pid;
  t->num = m->ntrk + 1;
  t->codec = es->codec;
  t->cls = es->cls;
  memcpy(t->lang, es->lang, sizeof t->lang);
  pes_track(m->pes, t->pid);
  m->ntrk++;
}

static void setup(mkv_t *m) {
  const psi_es_t *es;
  int c, k;

  es = psi_es(m->psi, &c);
  if (m->video_ok) {
    for (k = 0; k < c && m->ntrk < MKV_MAX_TRACKS; k++) {
      if (es[k].cls != PID_VIDEO)
        continue;
      if (es[k].codec != CODEC_H264 && es[k].codec != CODEC_HEVC &&
          es[k].codec != CODEC_MPEG2V) {
        log_line("mkv=no_vc(%s)", codec_name(es[k].codec));
        continue;
      }
      add_track(m, &es[k]);
    }
  }
  for (k = 0; k < c && m->ntrk < MKV_MAX_TRACKS; k++) {
    if (es[k].cls != PID_AUDIO)
      continue;
    if (!m->cfg->audio_all && es[k].audio_index != (int)m->cfg->audio_track)
      continue;
    if (!audio_supported(es[k].codec)) {
      log_line("mkv=no_ac(%s)", codec_name(es[k].codec));
      continue;
    }
    add_track(m, &es[k]);
  }
  if (m->cfg->subs == SUB_SRT) {
    for (k = 0; k < c && m->ntrk < MKV_MAX_TRACKS; k++) {
      track_t *t;
      if (es[k].cls != PID_TELETEXT || !es[k].ttx_page)
        continue;
      add_track(m, &es[k]);
      t = &m->trk[m->ntrk - 1];
      t->ttx = ttx_new(es[k].ttx_page, es[k].ttx_lang, m->cfg->sub_lead_ms, on_cue, m);
      if (!t->ttx) {
        m->ntrk--;
        continue;
      }
      memcpy(t->lang, es[k].ttx_lang, sizeof t->lang);
      snprintf(t->codecid, sizeof t->codecid, "S_TEXT/UTF8");
      t->hdr_parsed = 1; /* no setup data */
      log_line("mkv=txtsub(%u=%s)", es[k].ttx_page, es[k].ttx_lang);
      break;
    }
  }
  if (!m->ntrk)
    log_line("mkv=no_mux");
  m->setup = 1;
}

mkv_t *mkv_new(int fd, const config_t *cfg, int video_ok, unsigned long long *bytes) {
  mkv_t *m = calloc(1, sizeof *m);

  if (!m)
    return NULL;
  m->fd = fd;
  m->cfg = cfg;
  m->video_ok = video_ok;
  m->bytes = bytes;
  m->psi = psi_new();
  m->pes = pes_new(on_pes, m);
  if (!m->psi || !m->pes) {
    mkv_close(m);
    return NULL;
  }
  return m;
}

void mkv_feed(mkv_t *m, const unsigned char *pkt) {
  if (m->err)
    return;
  psi_feed(m->psi, pkt);
  if (!m->setup && psi_ready(m->psi))
    setup(m);
  if (m->setup)
    pes_feed(m->pes, pkt);
  if (m->setup && !m->started && m->ntrk)
    all_ready(m); /* re-check: SDT may arrive later */
}

const psi_t *mkv_psi(const mkv_t *m) { return m->psi; }

int mkv_error(const mkv_t *m) { return m->err; }

void mkv_close(mkv_t *m) {
  int i;

  if (!m)
    return;
  m->flushing = 1;
  if (m->pes)
    pes_flush(m->pes);
  for (i = 0; i < m->ntrk; i++)
    if (m->trk[i].ttx)
      ttx_flush(m->trk[i].ttx);
  if (!m->started && m->ntrk)
    start(m);
  cluster_flush(m);
  for (i = 0; i < m->npend; i++)
    free(m->pend[i].data);
  for (i = 0; i < m->ntrk; i++) {
    free(m->trk[i].rem);
    free(m->trk[i].vbuf);
    ttx_free(m->trk[i].ttx);
  }
  ebuf_free(&m->cl);
  pes_free(m->pes);
  psi_free(m->psi);
  free(m);
}
