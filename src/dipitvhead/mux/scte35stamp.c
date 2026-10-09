/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/demux/crc32.h"

#include "scte35stamp.h"

#define SECTION_MAX 4096
#define TABLE_ID_SPLICE_INFO 0xFC
#define PTS_MODULUS ((uint64_t)1 << 33)
#define PTS_ADJ_OFFSET 4
#define SECTION_MIN 16

void scte35stamp_init(scte35stamp_t *s) { memset(s, 0, sizeof *s); }

static unsigned payload_start(const unsigned char *pkt188) {
  unsigned afc = (pkt188[3] >> 4) & 0x3;
  unsigned start = 4;
  if (afc == 0x3) start += 1 + pkt188[4];
  return start;
}

void scte35stamp_flush(scte35stamp_t *s, scte35_emit_fn emit, void *ctx) {
  for (int i = 0; i < s->n; i++) emit(ctx, s->pk[i]);
  s->n = 0;
  s->total = 0;
  s->got = 0;
}

static void patch_and_emit(scte35stamp_t *s, int64_t delta90k, scte35_emit_fn emit, void *ctx) {
  unsigned char sec[SECTION_MAX];
  unsigned pos = 0;
  uint64_t adj;
  uint64_t d;
  uint32_t crc;
  for (int i = 0; i < s->n; i++) {
    unsigned st = payload_start(s->pk[i]) + (i == 0 ? 1 : 0);
    unsigned take = 188 - st;
    if (take > s->total - pos) take = s->total - pos;
    memcpy(sec + pos, s->pk[i] + st, take);
    pos += take;
  }
  if (crc32_mpeg(sec, s->total) != 0) {
    scte35stamp_flush(s, emit, ctx);
    return;
  }
  adj = ((uint64_t)(sec[PTS_ADJ_OFFSET] & 1) << 32) | ((uint64_t)sec[PTS_ADJ_OFFSET + 1] << 24) | ((uint64_t)sec[PTS_ADJ_OFFSET + 2] << 16) |
        ((uint64_t)sec[PTS_ADJ_OFFSET + 3] << 8) | sec[PTS_ADJ_OFFSET + 4];
  d = (uint64_t)(((delta90k % (int64_t)PTS_MODULUS) + (int64_t)PTS_MODULUS) % (int64_t)PTS_MODULUS);
  adj = (adj + d) % PTS_MODULUS;
  sec[PTS_ADJ_OFFSET] = (unsigned char)((sec[PTS_ADJ_OFFSET] & 0xFE) | ((adj >> 32) & 1));
  sec[PTS_ADJ_OFFSET + 1] = (unsigned char)(adj >> 24);
  sec[PTS_ADJ_OFFSET + 2] = (unsigned char)(adj >> 16);
  sec[PTS_ADJ_OFFSET + 3] = (unsigned char)(adj >> 8);
  sec[PTS_ADJ_OFFSET + 4] = (unsigned char)adj;
  crc = crc32_mpeg(sec, s->total - 4);
  sec[s->total - 4] = (unsigned char)(crc >> 24);
  sec[s->total - 3] = (unsigned char)(crc >> 16);
  sec[s->total - 2] = (unsigned char)(crc >> 8);
  sec[s->total - 1] = (unsigned char)crc;
  pos = 0;
  for (int i = 0; i < s->n; i++) {
    unsigned st = payload_start(s->pk[i]) + (i == 0 ? 1 : 0);
    unsigned take = 188 - st;
    if (take > s->total - pos) take = s->total - pos;
    memcpy(s->pk[i] + st, sec + pos, take);
    pos += take;
  }
  s->patched++;
  scte35stamp_flush(s, emit, ctx);
}

static int starts_section(const unsigned char *pkt188, unsigned *total) {
  unsigned st = payload_start(pkt188);
  unsigned len;
  if (!(pkt188[1] & 0x40) || st + 4 > 188 || pkt188[st] != 0 || pkt188[st + 1] != TABLE_ID_SPLICE_INFO) return 0;
  len = (unsigned)((pkt188[st + 2] & 0x0F) << 8) | pkt188[st + 3];
  *total = 3 + len;
  return *total >= SECTION_MIN && *total <= SECTION_MAX && st + 1 + SECTION_MIN <= 188;
}

void scte35stamp_feed(scte35stamp_t *s, unsigned char *pkt188, int64_t delta90k, scte35_emit_fn emit, void *ctx) {
  unsigned total;
  unsigned st;
  unsigned take;
  if ((pkt188[3] >> 6) || !(pkt188[3] & 0x10)) {
    scte35stamp_flush(s, emit, ctx);
    emit(ctx, pkt188);
    return;
  }
  if (pkt188[1] & 0x40) {
    scte35stamp_flush(s, emit, ctx);
    if (!starts_section(pkt188, &total) || (total + 1 + 182) / 183 > SCTE35_MAX_PACKETS) {
      emit(ctx, pkt188);
      return;
    }
    s->total = total;
    s->got = 0;
  } else if (!s->n) {
    emit(ctx, pkt188);
    return;
  } else if (payload_start(pkt188) >= 188) {
    scte35stamp_flush(s, emit, ctx);
    emit(ctx, pkt188);
    return;
  }
  if (s->n >= SCTE35_MAX_PACKETS) {
    scte35stamp_flush(s, emit, ctx);
    emit(ctx, pkt188);
    return;
  }
  memcpy(s->pk[s->n], pkt188, 188);
  st = payload_start(pkt188) + (s->n == 0 ? 1 : 0);
  take = 188 - st;
  if (take > s->total - s->got) take = s->total - s->got;
  s->got += take;
  s->n++;
  if (s->got >= s->total) patch_and_emit(s, delta90k, emit, ctx);
}
