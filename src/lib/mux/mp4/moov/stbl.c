/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/mux/mp4/priv.h"

static void build_run_length_box(mp4buf_t *out, const track_t *t, const char *box_tag, int is_ctts) {
  mp4buf_t b;
  size_t cnt_pos;
  uint32_t entries = 0;
  uint32_t run_val = 0;
  uint32_t run_cnt = 0;
  int any = 0;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  cnt_pos = b.len;
  mb_u32(&b, 0);
  for (int i = 0; i < t->nsamp; i++) {
    uint32_t v = is_ctts ? (uint32_t)t->samp[i].cts_offset : t->samp[i].duration;
    if (v) any = 1;
    if (run_cnt && v == run_val) {
      run_cnt++;
      continue;
    }
    if (run_cnt) {
      mb_u32(&b, run_cnt);
      mb_u32(&b, run_val);
      entries++;
    }
    run_val = v;
    run_cnt = 1;
  }
  if (run_cnt) {
    mb_u32(&b, run_cnt);
    mb_u32(&b, run_val);
    entries++;
  }
  if (is_ctts && !any) {
    mp4buf_free(&b);
    return;
  }
  mb_patch_u32(&b, cnt_pos, entries);
  mb_box(out, box_tag, &b);
}

static void build_stts(mp4buf_t *out, const track_t *t) { build_run_length_box(out, t, "stts", 0); }

static void build_ctts(mp4buf_t *out, const track_t *t) { build_run_length_box(out, t, "ctts", 1); }

static void build_stsz(mp4buf_t *out, const track_t *t) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, (uint32_t)t->nsamp);
  for (int i = 0; i < t->nsamp; i++) mb_u32(&b, t->samp[i].size);
  mb_box(out, "stsz", &b);
}

static void build_stsc(mp4buf_t *out, const track_t *t, int ck) {
  mp4buf_t b;
  int any = t->nsamp || ck;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, any ? 1 : 0);
  if (any) {
    mb_u32(&b, 1);
    mb_u32(&b, 1);
    mb_u32(&b, 1);
  }
  mb_box(out, "stsc", &b);
}

static void build_stss(mp4buf_t *out, const track_t *t) {
  mp4buf_t b;
  size_t cnt_pos;
  uint32_t entries = 0;
  int any_nonkey = 0;
  for (int i = 0; i < t->nsamp; i++) {
    if (!t->samp[i].keyframe) any_nonkey = 1;
  }
  if (!any_nonkey) return;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  cnt_pos = b.len;
  mb_u32(&b, 0);
  for (int i = 0; i < t->nsamp; i++) {
    if (t->samp[i].keyframe) {
      mb_u32(&b, (uint32_t)(i + 1));
      entries++;
    }
  }
  mb_patch_u32(&b, cnt_pos, entries);
  mb_box(out, "stss", &b);
}

static void build_co64(mp4buf_t *out, const track_t *t) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, (uint32_t)t->nsamp);
  for (int i = 0; i < t->nsamp; i++) mb_u64(&b, t->samp[i].offset);
  mb_box(out, "co64", &b);
}

static void mb_zeros(mp4buf_t *b, size_t n) {
  static const unsigned char zero[4096];
  while (n) {
    size_t k = n < sizeof zero ? n : sizeof zero;
    mb_bytes(b, zero, k);
    n -= k;
  }
}

/* empty table with room for cap entries, filled in place later */
static void build_ck_table(mp4buf_t *out, const char *tag, const ck_tab_t *c, int stsz) {
  mp4buf_t b;
  if (!c->esz) return;
  memset(&b, 0, sizeof b);
  mb_u32(&b, 0);
  if (stsz) mb_u32(&b, 0);
  mb_u32(&b, 0);
  mb_zeros(&b, (size_t)c->cap * c->esz);
  mb_box(out, tag, &b);
}

void build_stbl(mp4buf_t *out, const track_t *t, int ck) {
  mp4buf_t stbl;
  trak_meta_t tm;
  trak_meta_from_track(&tm, t);
  memset(&stbl, 0, sizeof stbl);
  trak_build_stsd(&stbl, &tm);
  if (ck) {
    build_ck_table(&stbl, "stts", &t->ck[CK_STTS], 0);
    build_ck_table(&stbl, "ctts", &t->ck[CK_CTTS], 0);
    build_ck_table(&stbl, "stss", &t->ck[CK_STSS], 0);
    build_stsc(&stbl, t, 1);
    build_ck_table(&stbl, "stsz", &t->ck[CK_STSZ], 1);
    build_ck_table(&stbl, t->ck_wide ? "co64" : "stco", &t->ck[CK_CO], 0);
  } else {
    build_stts(&stbl, t);
    if (t->cls == PID_VIDEO) build_ctts(&stbl, t);
    if (t->cls == PID_VIDEO || t->cls == PID_AUDIO) build_stss(&stbl, t);
    build_stsc(&stbl, t, 0);
    build_stsz(&stbl, t);
    build_co64(&stbl, t);
  }
  mb_box(out, "stbl", &stbl);
}
