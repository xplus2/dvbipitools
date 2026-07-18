/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "ts.h"

struct ts_filter {
  psi_t *psi;
  unsigned char cc_pat, cc_pmt;
  int audio_all;
  unsigned audio_track; /* 1-based, if !audio_all */
  int strip_subs;       /* -s strip */
  int bad_track;        /* -a track missing from PMT */
};

/* audio ES not selected by -a */
static int audio_dropped(const ts_filter_t *f, unsigned pid) {
  const psi_es_t *es;
  int count, k;
  if (f->audio_all)
    return 0;
  es = psi_es(f->psi, &count);
  for (k = 0; k < count; k++)
    if (es[k].pid == pid && es[k].cls == PID_AUDIO)
      return es[k].audio_index != (int)f->audio_track;
  return 0;
}

/* ES dropped by -a or -s */
static int es_dropped(const ts_filter_t *f, unsigned pid) {
  if (audio_dropped(f, pid))
    return 1;
  if (f->strip_subs) {
    pid_class_t c = psi_classify(f->psi, pid);
    if (c == PID_TELETEXT || c == PID_SUBTITLE)
      return 1;
  }
  return 0;
}

/* copy descriptors, dropping one tag */
static size_t filter_descs(const unsigned char *s, size_t slen, unsigned drop, unsigned char *d) {
  size_t i = 0, o = 0;
  while (i + 2 <= slen) {
    size_t l = s[i + 1];
    if (i + 2 + l > slen)
      break;
    if (s[i] != drop) {
      memcpy(d + o, s + i, 2 + l);
      o += 2 + l;
    }
    i += 2 + l;
  }
  return o;
}

static void finish_section(unsigned char *dst, const unsigned char *src, size_t o) {
  size_t len = o - 3 + 4; /* bytes after length field, incl CRC */
  uint32_t crc;

  dst[1] = (src[1] & 0xF0) | (unsigned char)((len >> 8) & 0x0F);
  dst[2] = (unsigned char)(len & 0xFF);
  crc = crc32_mpeg(dst, o);
  dst[o] = (unsigned char)(crc >> 24);
  dst[o + 1] = (unsigned char)(crc >> 16);
  dst[o + 2] = (unsigned char)(crc >> 8);
  dst[o + 3] = (unsigned char)crc;
}

/* PAT minus program 0 (NIT) entry */
static size_t pat_rewrite(const unsigned char *src, size_t srclen, unsigned char *dst) {
  size_t i, o = 8, end;
  if (srclen < 12 || src[0] != 0x00)
    return 0;
  memcpy(dst, src, 8);
  end = srclen - 4;
  for (i = 8; i + 4 <= end; i += 4) {
    unsigned prog = ((unsigned)src[i] << 8) | src[i + 1];
    if (prog != 0) {
      memcpy(dst + o, src + i, 4);
      o += 4;
    }
  }
  finish_section(dst, src, o);
  return o + 4;
}

/* PMT minus AIT, unselected audio, CA descriptors; rest verbatim */
static size_t pmt_rewrite(const ts_filter_t *f, const unsigned char *src, size_t srclen, unsigned char *dst) {
  size_t i, o, end, pil, newpil;

  if (srclen < 16 || src[0] != 0x02)
    return 0;
  memcpy(dst, src, 12);
  pil = (((size_t)src[10] & 0x0F) << 8) | src[11];
  end = srclen - 4;
  if (12 + pil > end)
    return 0;

  o = 12 + filter_descs(src + 12, pil, 0x09, dst + 12);
  newpil = o - 12;
  dst[10] = (src[10] & 0xF0) | (unsigned char)((newpil >> 8) & 0x0F);
  dst[11] = (unsigned char)(newpil & 0xFF);

  i = 12 + pil;
  while (i + 5 <= end) {
    unsigned pid = (((unsigned)src[i + 1] & 0x1F) << 8) | src[i + 2];
    size_t esil = (((size_t)src[i + 3] & 0x0F) << 8) | src[i + 4];
    size_t es_o, newesil;
    if (i + 5 + esil > end)
      break;
    if (psi_classify(f->psi, pid) == PID_AIT || es_dropped(f, pid)) {
      i += 5 + esil;
      continue;
    }
    es_o = o;
    dst[es_o] = src[i];
    dst[es_o + 1] = src[i + 1];
    dst[es_o + 2] = src[i + 2];
    o = es_o + 5;
    newesil = filter_descs(src + i + 5, esil, 0x09, dst + o);
    o += newesil;
    dst[es_o + 3] =
        (src[i + 3] & 0xF0) | (unsigned char)((newesil >> 8) & 0x0F);
    dst[es_o + 4] = (unsigned char)(newesil & 0xFF);
    i += 5 + esil;
  }

  finish_section(dst, src, o);
  return o + 4;
}

/* section -> one TS packet, 0xFF padded */
static int emit_section(unsigned char *out, unsigned pid, unsigned char cc, const unsigned char *sec, size_t seclen) {
  if (seclen + 1 > 184)
    return 0;
  memset(out, 0xFF, 188);
  out[0] = 0x47;
  out[1] = 0x40 | (unsigned char)((pid >> 8) & 0x1F);
  out[2] = (unsigned char)(pid & 0xFF);
  out[3] = 0x10 | (cc & 0x0F);
  out[4] = 0x00;
  memcpy(out + 5, sec, seclen);
  return 1;
}

ts_filter_t *ts_filter_new(int audio_all, unsigned audio_track, int strip_subs) {
  ts_filter_t *f = calloc(1, sizeof *f);
  if (!f)
    return NULL;
  f->psi = psi_new();
  if (!f->psi) {
    free(f);
    return NULL;
  }
  f->audio_all = audio_all;
  f->audio_track = audio_track;
  f->strip_subs = strip_subs;
  return f;
}

void ts_filter_free(ts_filter_t *f) {
  if (!f)
    return;
  psi_free(f->psi);
  free(f);
}

int ts_filter_packet(ts_filter_t *f, const unsigned char *in, unsigned char *out) {
  unsigned pid;
  int pusi;
  pid_class_t cls;
  psi_feed(f->psi, in);
  if (in[0] != 0x47)
    return 0;
  pid = (((unsigned)in[1] & 0x1F) << 8) | in[2];
  pusi = in[1] & 0x40;
  if (!f->audio_all && psi_have_pmt(f->psi) && (int)f->audio_track > psi_audio_count(f->psi))
    f->bad_track = 1;

  if (pid == 0x0000) {
    unsigned char rw[512];
    size_t sl, rl;
    const unsigned char *sec = psi_pat_section(f->psi, &sl);
    if (!sec || !pusi)
      return 0;
    rl = pat_rewrite(sec, sl, rw);
    if (rl && emit_section(out, 0x0000, (f->cc_pat = (f->cc_pat + 1) & 0x0F), rw, rl))
      return 1;
    memcpy(out, in, 188); /* fallback: original */
    return 1;
  }

  if (psi_have_pat(f->psi) && pid == psi_pmt_pid(f->psi)) {
    unsigned char rw[1024];
    size_t sl, rl;
    const unsigned char *sec = psi_pmt_section(f->psi, &sl);
    if (!sec || !pusi)
      return 0;
    rl = pmt_rewrite(f, sec, sl, rw);
    if (rl && emit_section(out, pid, (f->cc_pmt = (f->cc_pmt + 1) & 0x0F), rw, rl))
      return 1;
    memcpy(out, in, 188);
    return 1;
  }

  cls = psi_classify(f->psi, pid);
  if (pid == 0x1FFF || cls == PID_NULL || cls == PID_NIT || cls == PID_EIT || cls == PID_CAT || cls == PID_AIT || cls == PID_ECM)
    return 0;
  if (es_dropped(f, pid)) /* -a / -s drop */
    return 0;
  /* hold unclassified ES until PMT */
  if ((!f->audio_all || f->strip_subs) && !psi_have_pmt(f->psi) &&
      cls == PID_UNKNOWN)
    return 0;

  memcpy(out, in, 188);
  return 1;
}

const psi_t *ts_filter_psi(const ts_filter_t *f) { return f->psi; }
int ts_filter_bad_track(const ts_filter_t *f) { return f->bad_track; }
