/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/demux/psi/section_asm.h"
#include "lib/demux/tspack.h"
#include "lib/mux/pmt_filter.h"
#include "ts.h"

struct ts_filter {
  psi_t *psi;
  psi_section_asm_t pat_asm; /* own reassembly, rewrite fires on completion */
  psi_section_asm_t pmt_asm;
  unsigned char cc_pat;
  unsigned char cc_pmt;
  int audio_all;
  unsigned audio_track; /* 1-based, if !audio_all */
  int strip_subs;       /* -s strip */
  int bad_track;        /* -a track missing from PMT */
  int track_seen;       /* -a track was present once, later PMT drops are transient */
  unsigned strip_mask;  /* --strip, STRIP_* bits */
  unsigned char int_pending[8192]; /* STRIP_INT: pid currently mid an INT section */
  unsigned char audio_drop[8192]; /* rebuilt each PMT parse, see rebuild_audio_drop() */
};

/* audio ES not selected by -a. rebuilt on every PMT parse, not per packet */
static void rebuild_audio_drop(ts_filter_t *f) {
  const psi_es_t *es;
  int count;
  memset(f->audio_drop, 0, sizeof f->audio_drop);
  if (f->audio_all) return;
  es = psi_es(f->psi, &count);
  for (int k = 0; k < count; k++) {
    if (es[k].cls == PID_AUDIO) f->audio_drop[es[k].pid] = es[k].audio_index != (int)f->audio_track;
  }
}

static int audio_dropped(const ts_filter_t *f, unsigned pid) { return f->audio_drop[pid]; }

/* ES dropped by -a or -s */
static int es_dropped(const ts_filter_t *f, unsigned pid) {
  if (audio_dropped(f, pid)) return 1;
  if (f->strip_subs) {
    pid_class_t c = psi_classify(f->psi, pid);
    if (c == PID_TELETEXT || c == PID_SUBTITLE) return 1;
  }
  return 0;
}

/* copy descriptors, dropping one tag */
static size_t filter_descs(const unsigned char *s, size_t slen, unsigned drop, unsigned char *d) {
  size_t i = 0, o = 0;
  while (i + 2 <= slen) {
    size_t l = s[i + 1];
    if (i + 2 + l > slen) break;
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
  if (srclen < 12 || src[0] != 0x00) return 0;
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

static size_t pmt_rewrite_es_entry(const ts_filter_t *f, const unsigned char *src, size_t i, size_t esil, unsigned char *dst, size_t o) {
  size_t es_o = o;
  size_t newesil;
  dst[es_o] = src[i];
  dst[es_o + 1] = src[i + 1];
  dst[es_o + 2] = src[i + 2];
  o = es_o + 5;
  if (f->strip_mask & STRIP_ECM) {
    newesil = filter_descs(src + i + 5, esil, 0x09, dst + o);
  } else {
    memcpy(dst + o, src + i + 5, esil);
    newesil = esil;
  }
  o += newesil;
  dst[es_o + 3] = (src[i + 3] & 0xF0) | (unsigned char)((newesil >> 8) & 0x0F);
  dst[es_o + 4] = (unsigned char)(newesil & 0xFF);
  return o;
}

/* PMT minus AIT (--strip AIT), unselected audio/subs (-a/-s), CA descriptors
   (ES-level = ECM pid, program-level = EMM pid, each own --strip bit) */
static size_t pmt_rewrite(const ts_filter_t *f, const unsigned char *src, size_t srclen, unsigned char *dst) {
  size_t i, o, end, pil, newpil;

  if (srclen < 16 || src[0] != 0x02) return 0;
  memcpy(dst, src, 12);
  pil = (((size_t)src[10] & 0x0F) << 8) | src[11];
  end = srclen - 4;
  if (12 + pil > end) return 0;
  if (f->strip_mask & STRIP_EMM) {
    o = 12 + filter_descs(src + 12, pil, 0x09, dst + 12);
  } else {
    memcpy(dst + 12, src + 12, pil);
    o = 12 + pil;
  }
  newpil = o - 12;
  dst[10] = (src[10] & 0xF0) | (unsigned char)((newpil >> 8) & 0x0F);
  dst[11] = (unsigned char)(newpil & 0xFF);

  i = 12 + pil;
  while (i + 5 <= end) {
    unsigned pid = (((unsigned)src[i + 1] & 0x1F) << 8) | src[i + 2];
    size_t esil = (((size_t)src[i + 3] & 0x0F) << 8) | src[i + 4];
    if (i + 5 + esil > end) break;
    if (((f->strip_mask & STRIP_AIT) && psi_classify(f->psi, pid) == PID_AIT) || es_dropped(f, pid)) {
      i += 5 + esil;
      continue;
    }
    o = pmt_rewrite_es_entry(f, src, i, esil, dst, o);
    i += 5 + esil;
  }
  finish_section(dst, src, o);
  return o + 4;
}

ts_filter_t *ts_filter_new(int audio_all, unsigned audio_track, int strip_subs, unsigned preferred_pmt_pid, unsigned strip_mask) {
  ts_filter_t *f = calloc(1, sizeof *f);
  if (!f) return NULL;
  f->psi = psi_new();
  if (!f->psi) {
    free(f);
    return NULL;
  }
  if (preferred_pmt_pid) psi_select_pmt_pid(f->psi, preferred_pmt_pid);
  f->audio_all = audio_all;
  f->audio_track = audio_track;
  f->strip_subs = strip_subs;
  f->strip_mask = strip_mask;
  return f;
}

void ts_filter_free(ts_filter_t *f) {
  if (!f) return;
  psi_free(f->psi);
  free(f);
}

/* table_id of a section-start packet, -1 if not a section start or malformed */
static int section_table_id(const unsigned char *pkt) {
  const unsigned char *pl;
  size_t plen, ptr;
  int pusi;
  if (!tspack_payload(pkt, &pl, &plen, &pusi) || !pusi || plen < 1) return -1;
  ptr = pl[0];
  if (1 + ptr >= plen) return -1;
  return pl[1 + ptr];
}

/* STRIP_INT: table_id 0x4C, no fixed pid. per-pid latch, catches
   continuation packets too. harmless on ES pids: PES never reads as 0x4C. */
static int int_dropped(ts_filter_t *f, unsigned pid, const unsigned char *pkt) {
  int tid = section_table_id(pkt);
  if (tid >= 0) f->int_pending[pid] = (tid == 0x4C) ? 1 : 0;
  return f->int_pending[pid];
}

/* NUL/NIT/EIT/CAT/AIT/ECM/EMM/RST/TDT/TOT: resolved via psi.c or fixed pid.
   INT excluded, needs raw packet, see int_dropped() */
static int table_dropped(const ts_filter_t *f, unsigned pid, pid_class_t cls) {
  switch (cls) {
    case PID_NULL:     return (f->strip_mask & STRIP_NUL) != 0;
    case PID_NIT:      return (f->strip_mask & STRIP_NIT) != 0;
    case PID_EIT:      return (f->strip_mask & STRIP_EIT) != 0;
    case PID_CAT:      return (f->strip_mask & STRIP_CAT) != 0;
    case PID_AIT:      return (f->strip_mask & STRIP_AIT) != 0;
    case PID_ECM:      return (f->strip_mask & STRIP_ECM) != 0;
    case PID_LCEVC:    return (f->strip_mask & STRIP_LCEVC) != 0;
    case PID_UNKNOWN:
    case PID_PAT:
    case PID_PMT:
    case PID_SDT:
    case PID_OTHER_SI:
    case PID_PCR:
    case PID_VIDEO:
    case PID_AUDIO:
    case PID_TELETEXT:
    case PID_SUBTITLE:
    case PID_DATA:
      break;
  }
  /* raw-pid checks below apply regardless of cls, switch above only covers class-based ones */
  if ((f->strip_mask & STRIP_EMM) && psi_emm_pid(f->psi) && pid == psi_emm_pid(f->psi)) return 1;
  if ((f->strip_mask & (STRIP_TDT | STRIP_TOT)) && pid == 0x0014) return 1;
  if ((f->strip_mask & STRIP_RST) && pid == 0x0013) return 1;
  return 0;
}

/* rewritten section of pid's table, valid only if psi.c parsed the very same one */
static size_t rewrite_section(const ts_filter_t *f, unsigned pid, const unsigned char *sec, size_t sl, unsigned char *rw) {
  const unsigned char *known;
  size_t kl;
  size_t rl;
  if (pid == 0x0000) known = psi_pat_section(f->psi, &kl);
  else known = psi_pmt_section(f->psi, &kl);
  if (!known || kl != sl || memcmp(known, sec, sl)) return 0;
  rl = pid == 0x0000 ? pat_rewrite(sec, sl, rw) : pmt_rewrite(f, sec, sl, rw);
  if (rl) return rl;
  memcpy(rw, sec, sl); /* unparseable: keep as is */
  return sl;
}

/* PAT/PMT packet in, completed sections out as 1..n packets. continuation packets yield 0 */
static size_t section_packets(ts_filter_t *f, psi_section_asm_t *a, const unsigned char *in, unsigned pid, unsigned char *cc, unsigned char *out) {
  unsigned char rw[PSI_SECTION_ASM_BUF_LEN]; /* output never larger than input */
  const unsigned char *pl;
  size_t plen;
  size_t total = 0;
  int pusi;
  if (!tspack_payload(in, &pl, &plen, &pusi) || !psi_section_asm_cc(a, in)) return 0;
  if (pid != 0x0000) rebuild_audio_drop(f);
  for (int got = psi_section_asm_feed(a, pl, plen, pusi); got; got = psi_section_asm_next(a, pl, plen)) {
    size_t rl = rewrite_section(f, pid, a->buf, a->expect, rw);
    if (rl) total += pmt_filter_emit_packets(out + total * 188, TS_FILTER_OUT_MAX - total * 188, pid, cc, rw, rl);
  }
  return total;
}

/* locked PMT pid. unlocked: any PAT-listed pid, PMT may be mid-section */
static int pmt_pid_held(const ts_filter_t *f, unsigned pid) {
  const psi_program_t *pr;
  unsigned locked = psi_pmt_pid(f->psi);
  int count;
  if (!psi_have_pat(f->psi)) return 0;
  if (locked) return pid == locked;
  pr = psi_pat_programs(f->psi, &count);
  for (int k = 0; k < count; k++) {
    if (pr[k].pmt_pid == pid) return 1;
  }
  return 0;
}

const unsigned char *ts_filter_packet(ts_filter_t *f, const unsigned char *in, unsigned char *out, size_t *n) {
  unsigned pid;
  pid_class_t cls;
  *n = 0;
  psi_feed(f->psi, in);
  if (in[0] != 0x47) return NULL;
  pid = tspack_pid(in);
  if (!f->audio_all && psi_have_pmt(f->psi)) {
    if ((int)f->audio_track <= psi_audio_count(f->psi)) f->track_seen = 1;
    else if (!f->track_seen) f->bad_track = 1;
  }

  if (pid == 0x0000) {
    if (!(f->strip_mask & STRIP_NIT)) return in;
    *n = section_packets(f, &f->pat_asm, in, pid, &f->cc_pat, out);
    return *n ? out : NULL;
  }

  if (pmt_pid_held(f, pid)) {
    *n = section_packets(f, &f->pmt_asm, in, pid, &f->cc_pmt, out);
    return *n ? out : NULL;
  }

  cls = psi_classify(f->psi, pid);
  if (table_dropped(f, pid, cls)) return NULL;
  if ((f->strip_mask & STRIP_INT) && int_dropped(f, pid, in)) return NULL;
  if (es_dropped(f, pid))    return NULL; /* -a / -s drop */
  /* hold unclassified ES until PMT */
  if ((!f->audio_all || f->strip_subs) && !psi_have_pmt(f->psi) && cls == PID_UNKNOWN) return NULL;
  *n = 1;
  return in;
}

const psi_t *ts_filter_psi(const ts_filter_t *f) { return f->psi; }
int ts_filter_bad_track(const ts_filter_t *f) { return f->bad_track; }
