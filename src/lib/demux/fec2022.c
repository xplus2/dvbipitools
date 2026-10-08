/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "../helper/beutil.h"
#include "fec2022.h"
#include "rtp.h"

#define EXT_BASE ((int64_t)1 << 40)

typedef struct {
  int64_t start;
  int start_valid;
  int64_t gen;
  int gen_ever;
  int gen_open;
  unsigned real_count;
  int repair_seen;
  unsigned char rp;
  unsigned char rx;
  unsigned char rcc;
  unsigned char rm;
  unsigned char rpt_xor;
  uint32_t rts_xor;
  uint16_t rlen_xor;
  unsigned char repair_payload[FEC2022_MAX_PKT - 12];
  size_t repair_payload_len;
} fec2022_col_t;

typedef enum { RING_EMPTY, RING_READY, RING_DROPPED } ring_state_t;

typedef struct {
  ring_state_t state;
  unsigned char pkt[FEC2022_MAX_PKT];
  size_t len;
} ring_slot_t;

struct fec2022_dec {
  unsigned l;
  unsigned d;
  uint32_t src_ssrc;
  int have_ssrc;
  int have_cursor;
  int have_hi;
  int64_t hi_ext;
  int64_t origin_ext;
  int64_t next_release_ext;
  size_t ring_n;
  fec2022_col_t col[FEC2022_MAX_L];
  int *slot_seen;
  size_t *slot_len;
  unsigned char *slot_pkt;
  ring_slot_t *ring;
  unsigned char *ready;
  size_t *ready_len;
  unsigned ready_head;
  unsigned ready_count;
};

fec2022_dec_t *fec2022_dec_new(unsigned l, unsigned d) {
  fec2022_dec_t *dec;
  size_t ld;

  if (l == 0 || d == 0 || l > FEC2022_MAX_L || l * d > FEC2022_MAX_LD) return NULL;
  dec = calloc(1, sizeof *dec);
  if (!dec) return NULL;
  dec->l = l;
  dec->d = d;
  ld = (size_t)l * d;
  dec->slot_seen = calloc(ld, sizeof *dec->slot_seen);
  dec->slot_len = calloc(ld, sizeof *dec->slot_len);
  dec->slot_pkt = calloc(ld, FEC2022_MAX_PKT);
  dec->ring_n = ld * 4;
  dec->ring = calloc(dec->ring_n, sizeof *dec->ring);
  dec->ready = calloc(ld, FEC2022_MAX_PKT);
  dec->ready_len = calloc(ld, sizeof *dec->ready_len);
  if (!dec->slot_seen || !dec->slot_len || !dec->slot_pkt || !dec->ring || !dec->ready || !dec->ready_len) {
    fec2022_dec_free(dec);
    return NULL;
  }
  return dec;
}

void fec2022_dec_free(fec2022_dec_t *dec) {
  if (!dec) return;
  free(dec->slot_seen);
  free(dec->slot_len);
  free(dec->slot_pkt);
  free(dec->ring);
  free(dec->ready);
  free(dec->ready_len);
  free(dec);
}

static int64_t floordiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if (a % b < 0) q--;
  return q;
}

/* unwrap 16 bit sequence against highest */
static int64_t ext_seq(fec2022_dec_t *dec, uint16_t seq, int advance) {
  int64_t ext;
  if (!dec->have_hi) {
    dec->hi_ext = EXT_BASE + seq;
    dec->have_hi = 1;
  }
  ext = dec->hi_ext + (int16_t)(uint16_t)(seq - (uint16_t)dec->hi_ext);
  if (advance && ext > dec->hi_ext) dec->hi_ext = ext;
  return ext;
}

static void ring_put(fec2022_dec_t *dec, int64_t ext, ring_state_t state, const unsigned char *pkt, size_t len) {
  ring_slot_t *r;

  if (!dec->have_cursor || ext < dec->next_release_ext) return;
  r = &dec->ring[(uint64_t)ext % dec->ring_n];
  r->state = state;
  if (state == RING_READY) {
    memcpy(r->pkt, pkt, len);
    r->len = len;
  }
}

static void release_pending(fec2022_dec_t *dec) {
  size_t ld = (size_t)dec->l * dec->d;

  if (!dec->have_cursor) return;
  for (;;) {
    ring_slot_t *r = &dec->ring[(uint64_t)dec->next_release_ext % dec->ring_n];
    if (r->state == RING_EMPTY && dec->hi_ext - dec->next_release_ext < (int64_t)(2 * ld))
      break;
    if (r->state == RING_READY && dec->ready_count < ld) {
      unsigned tail = (dec->ready_head + dec->ready_count) % ld;
      memcpy(dec->ready + tail * FEC2022_MAX_PKT, r->pkt, r->len);
      dec->ready_len[tail] = r->len;
      dec->ready_count++;
    }
    r->state = RING_EMPTY;
    dec->next_release_ext++;
  }
}

static void resolve_column(fec2022_dec_t *dec, unsigned c) {
  fec2022_col_t *cs = &dec->col[c];
  unsigned missing = dec->d;

  if (cs->real_count == dec->d - 1 && cs->repair_seen) {
    for (unsigned i = 0; i < dec->d; i++) {
      if (!dec->slot_seen[(size_t)c * dec->d + i]) {
        missing = i;
        break;
      }
    }
  }
  for (unsigned i = 0; i < dec->d; i++) {
    size_t si = (size_t)c * dec->d + i;
    int64_t ext = cs->start + (cs->gen * dec->d + i) * (int64_t)dec->l;
    uint16_t seq = (uint16_t)ext;

    if (dec->slot_seen[si]) {
      ring_put(dec, ext, RING_READY, dec->slot_pkt + si * FEC2022_MAX_PKT, dec->slot_len[si]);
    } else if (i == missing && cs->rlen_xor <= sizeof cs->repair_payload) {
      unsigned char p = cs->rp;
      unsigned char x = cs->rx;
      unsigned char cc = cs->rcc;
      unsigned char m = cs->rm;
      unsigned char pt_xor = cs->rpt_xor;
      uint32_t ts_xor = cs->rts_xor;
      uint16_t len_xor = cs->rlen_xor;
      unsigned char payload[FEC2022_MAX_PKT - 12];
      unsigned char rec[FEC2022_MAX_PKT];

      memcpy(payload, cs->repair_payload, sizeof payload);
      for (unsigned j = 0; j < dec->d; j++) {
        size_t sj = (size_t)c * dec->d + j;
        const unsigned char *sp;
        size_t sl;

        if (j == i || !dec->slot_seen[sj]) continue;
        sp = dec->slot_pkt + sj * FEC2022_MAX_PKT;
        sl = dec->slot_len[sj];
        p ^= (sp[0] >> 5) & 1;
        x ^= (sp[0] >> 4) & 1;
        cc ^= sp[0] & 0xF;
        m ^= (sp[1] >> 7) & 1;
        pt_xor ^= sp[1] & 0x7F;
        ts_xor ^= be32_get(sp + 4);
        len_xor ^= (uint16_t)(sl - 12);
        for (size_t k = 0; k < sl - 12; k++) payload[k] ^= sp[12 + k];
      }
      rec[0] = (unsigned char)(0x80 | (p << 5) | (x << 4) | cc);
      rec[1] = (unsigned char)((m << 7) | pt_xor);
      be16_put(rec + 2, seq);
      be32_put(rec + 4, ts_xor);
      be32_put(rec + 8, dec->src_ssrc);
      memcpy(rec + 12, payload, len_xor);
      ring_put(dec, ext, RING_READY, rec, (size_t)12 + len_xor);
    } else {
      ring_put(dec, ext, RING_DROPPED, NULL, 0);
    }
  }
  cs->gen_open = 0;
  release_pending(dec);
}

static void enter_generation(fec2022_dec_t *dec, unsigned c, int64_t gen) {
  fec2022_col_t *cs = &dec->col[c];

  if (cs->gen_ever && cs->gen_open) resolve_column(dec, c);
  cs->gen = gen;
  cs->gen_ever = 1;
  cs->gen_open = 1;
  cs->real_count = 0;
  cs->repair_seen = 0;
  for (unsigned i = 0; i < dec->d; i++) dec->slot_seen[(size_t)c * dec->d + i] = 0;
}

int fec2022_dec_source(fec2022_dec_t *dec, const unsigned char *pkt, size_t len) {
  int64_t ext;
  int64_t row;
  int64_t gen;
  unsigned c;
  unsigned idx;
  fec2022_col_t *cs;
  size_t si;

  if (len < 12 || len > FEC2022_MAX_PKT) return -1;
  ext = ext_seq(dec, be16_get(pkt + 2), 1);
  if (!dec->have_ssrc) {
    dec->src_ssrc = be32_get(pkt + 8);
    dec->have_ssrc = 1;
  }
  if (!dec->have_cursor) {
    dec->next_release_ext = ext;
    dec->origin_ext = ext;
    dec->have_cursor = 1;
  }
  c = (unsigned)((uint64_t)ext % dec->l);
  cs = &dec->col[c];
  if (!cs->start_valid) {
    /* 1st group of col starts at 1st pos or after 1st pkg */
    cs->start = dec->origin_ext + (int64_t)((c + dec->l - (uint64_t)dec->origin_ext % dec->l) % dec->l);
    cs->start_valid = 1;
  }
  row = (ext - cs->start) / (int64_t)dec->l;
  gen = floordiv(row, dec->d);
  idx = (unsigned)(row - gen * dec->d);
  if (cs->gen_ever && gen < cs->gen) return 0;
  if (!cs->gen_ever || gen != cs->gen) enter_generation(dec, c, gen);
  else if (!cs->gen_open) return 0;

  si = (size_t)c * dec->d + idx;
  if (!dec->slot_seen[si]) {
    dec->slot_seen[si] = 1;
    dec->slot_len[si] = len;
    memcpy(dec->slot_pkt + si * FEC2022_MAX_PKT, pkt, len);
    cs->real_count++;
  }
  if (cs->real_count == dec->d)
    resolve_column(dec, c);
  else
    release_pending(dec);
  return 0;
}

void fec2022_dec_repair(fec2022_dec_t *dec, const unsigned char *pkt, size_t len) {
  rtp_hdr_t h;
  const unsigned char *fh;
  int64_t snb;
  int64_t gen;
  size_t ld = (size_t)dec->l * dec->d;
  unsigned c;
  fec2022_col_t *cs;
  size_t payload_off;
  size_t payload_len;

  if (!rtp_parse_header(pkt, len, &h) || h.payload_off + 16 > len) return;
  fh = pkt + h.payload_off;
  snb = ext_seq(dec, be16_get(fh), 0); /* RFC6015 SN base low */
  if (fh[13] != dec->l || fh[14] != dec->d) return; /* RFC6015 Offset, NA */

  c = (unsigned)((uint64_t)snb % dec->l);
  cs = &dec->col[c];
  if (cs->start_valid && (snb - cs->start) % (int64_t)ld != 0) {
    if (cs->gen_ever && cs->gen_open) resolve_column(dec, c);
    cs->gen_ever = 0;
    cs->start_valid = 0;
  }
  if (!cs->start_valid) {
    cs->start = snb;
    cs->start_valid = 1;
  }
  gen = floordiv((snb - cs->start) / (int64_t)dec->l, dec->d);
  if (cs->gen_ever && gen < cs->gen) return;
  if (!cs->gen_ever || gen != cs->gen)
    enter_generation(dec, c, gen);
  else if (!cs->gen_open)
    return;
  if (cs->repair_seen) return;

  cs->repair_seen = 1;
  cs->rp = (pkt[0] >> 5) & 1;
  cs->rx = (pkt[0] >> 4) & 1;
  cs->rcc = pkt[0] & 0xF;
  cs->rm = (pkt[1] >> 7) & 1;
  cs->rpt_xor = fh[4] & 0x7F;           /* RFC6015 PT recovery */
  cs->rts_xor = be32_get(fh + 8);    /* RFC6015 TS recovery */
  cs->rlen_xor = be16_get(fh + 2);   /* RFC6015 Length recovery */
  payload_off = h.payload_off + 16;
  payload_len = len - payload_off;
  if (payload_len > sizeof cs->repair_payload) payload_len = sizeof cs->repair_payload;
  memset(cs->repair_payload, 0, sizeof cs->repair_payload);
  memcpy(cs->repair_payload, fh + 16, payload_len);
  cs->repair_payload_len = payload_len;
  if (cs->real_count == dec->d || cs->real_count == dec->d - 1) resolve_column(dec, c);
}

size_t fec2022_dec_drain(fec2022_dec_t *dec, unsigned char *out, size_t cap) {
  size_t ld = (size_t)dec->l * dec->d;
  size_t len;
  if (dec->ready_count == 0) return 0;
  len = dec->ready_len[dec->ready_head];
  if (len > cap) return 0;
  memcpy(out, dec->ready + dec->ready_head * FEC2022_MAX_PKT, len);
  dec->ready_head = (dec->ready_head + 1) % ld;
  dec->ready_count--;
  return len;
}
