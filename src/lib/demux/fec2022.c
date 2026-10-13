/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "../helper/beutil.h"
#include "fec2022.h"

#define EXT_BASE ((int64_t)1 << 40)

typedef struct {
  int64_t gen;
  int open;
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
} fec2022_gen_t;

/* g[gen & 1]: previous generation stays open for late repair */
typedef struct {
  int64_t start;
  int start_valid;
  int64_t gen;
  int gen_ever;
  fec2022_gen_t g[2];
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
  unsigned late_run;
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
  unsigned char *held;   /* repairs ahead of hi_ext, replayed on src in */
  size_t *held_len;
  unsigned held_count;
  unsigned held_cap;
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
  dec->slot_seen = calloc(ld * 2, sizeof *dec->slot_seen);
  dec->slot_len = calloc(ld * 2, sizeof *dec->slot_len);
  dec->slot_pkt = calloc(ld * 2, FEC2022_MAX_PKT);
  dec->ring_n = ld * 4;
  dec->ring = calloc(dec->ring_n, sizeof *dec->ring);
  dec->ready = calloc(dec->ring_n, FEC2022_MAX_PKT);
  dec->ready_len = calloc(dec->ring_n, sizeof *dec->ready_len);
  dec->held_cap = 2 * l;
  dec->held = calloc(dec->held_cap, FEC2022_MAX_REPAIR);
  dec->held_len = calloc(dec->held_cap, sizeof *dec->held_len);
  if (!dec->held || !dec->held_len) {
    fec2022_dec_free(dec);
    return NULL;
  }
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
  free(dec->held);
  free(dec->held_len);
  free(dec);
}

static int64_t floordiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if (a % b < 0) q--;
  return q;
}

static size_t slot_idx(const fec2022_dec_t *dec, unsigned c, unsigned gs, unsigned i) {
  return ((size_t)c * 2 + gs) * dec->d + i;
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

static void ready_push(fec2022_dec_t *dec, const ring_slot_t *r) {
  unsigned tail;

  if (r->state != RING_READY || dec->ready_count >= dec->ring_n) return;
  tail = (dec->ready_head + dec->ready_count) % (unsigned)dec->ring_n;
  memcpy(dec->ready + (size_t)tail * FEC2022_MAX_PKT, r->pkt, r->len);
  dec->ready_len[tail] = r->len;
  dec->ready_count++;
}

static void release_pending(fec2022_dec_t *dec) {
  size_t ld = (size_t)dec->l * dec->d;

  if (!dec->have_cursor) return;
  for (;;) {
    ring_slot_t *r = &dec->ring[(uint64_t)dec->next_release_ext % dec->ring_n];
    if (r->state == RING_EMPTY && dec->hi_ext - dec->next_release_ext < (int64_t)(2 * ld))
      break;
    ready_push(dec, r);
    r->state = RING_EMPTY;
    dec->next_release_ext++;
  }
}

static void resolve_column(fec2022_dec_t *dec, unsigned c, unsigned gs) {
  fec2022_col_t *cs = &dec->col[c];
  fec2022_gen_t *g = &cs->g[gs];
  unsigned missing = dec->d;

  if (g->real_count == dec->d - 1 && g->repair_seen) {
    for (unsigned i = 0; i < dec->d; i++) {
      if (!dec->slot_seen[slot_idx(dec, c, gs, i)]) {
        missing = i;
        break;
      }
    }
  }
  for (unsigned i = 0; i < dec->d; i++) {
    size_t si = slot_idx(dec, c, gs, i);
    int64_t ext = cs->start + (g->gen * dec->d + i) * (int64_t)dec->l;
    uint16_t seq = (uint16_t)ext;

    if (dec->slot_seen[si]) {
      ring_put(dec, ext, RING_READY, dec->slot_pkt + si * FEC2022_MAX_PKT, dec->slot_len[si]);
    } else if (i == missing) {
      unsigned char p = g->rp;
      unsigned char x = g->rx;
      unsigned char cc = g->rcc;
      unsigned char m = g->rm;
      unsigned char pt_xor = g->rpt_xor;
      uint32_t ts_xor = g->rts_xor;
      uint16_t len_xor = g->rlen_xor;
      unsigned char payload[FEC2022_MAX_PKT - 12];
      unsigned char rec[FEC2022_MAX_PKT];

      memcpy(payload, g->repair_payload, sizeof payload);
      for (unsigned j = 0; j < dec->d; j++) {
        size_t sj = slot_idx(dec, c, gs, j);
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
      if (len_xor > sizeof payload) {
        ring_put(dec, ext, RING_DROPPED, NULL, 0);
        continue;
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
  g->open = 0;
  release_pending(dec);
}

static void enter_generation(fec2022_dec_t *dec, unsigned c, int64_t gen) {
  fec2022_col_t *cs = &dec->col[c];
  unsigned gs = (unsigned)(gen & 1);
  fec2022_gen_t *g = &cs->g[gs];

  if (g->open) resolve_column(dec, c, gs);
  g->gen = gen;
  g->open = 1;
  g->real_count = 0;
  g->repair_seen = 0;
  for (unsigned i = 0; i < dec->d; i++) dec->slot_seen[slot_idx(dec, c, gs, i)] = 0;
  if (!cs->gen_ever || gen > cs->gen) cs->gen = gen;
  cs->gen_ever = 1;
}

/* open slot for gen, entering it when newest. -1: late or already closed */
static int select_gen(fec2022_dec_t *dec, unsigned c, int64_t gen) {
  fec2022_col_t *cs = &dec->col[c];
  unsigned gs = (unsigned)(gen & 1);

  if (cs->gen_ever && gen < cs->gen)
    return (cs->g[gs].open && cs->g[gs].gen == gen) ? (int)gs : -1;
  if (!cs->gen_ever || gen != cs->gen)
    enter_generation(dec, c, gen);
  else if (!cs->g[gs].open)
    return -1;
  return (int)gs;
}

static void close_column(fec2022_dec_t *dec, unsigned c) {
  fec2022_col_t *cs = &dec->col[c];

  for (unsigned gs = 0; gs < 2; gs++)
    if (cs->g[gs].open) resolve_column(dec, c, gs);
  cs->gen_ever = 0;
  cs->start_valid = 0;
}

/* give up on generations whose repair window (one matrix after last packet) passed */
static void expire_columns(fec2022_dec_t *dec) {
  int64_t ld = (int64_t)dec->l * dec->d;

  for (unsigned c = 0; c < dec->l; c++) {
    fec2022_col_t *cs = &dec->col[c];
    if (!cs->start_valid) continue;
    for (unsigned gs = 0; gs < 2; gs++) {
      int64_t last;
      if (!cs->g[gs].open) continue;
      last = cs->start + (cs->g[gs].gen * dec->d + dec->d - 1) * (int64_t)dec->l;
      if (dec->hi_ext >= last + ld) resolve_column(dec, c, gs);
    }
  }
}

static void resync(fec2022_dec_t *dec) {
  if (dec->have_cursor) {
    for (unsigned c = 0; c < dec->l; c++)
      for (unsigned gs = 0; gs < 2; gs++)
        if (dec->col[c].g[gs].open) resolve_column(dec, c, gs);
    for (int64_t e = dec->next_release_ext; e <= dec->hi_ext; e++) {
      ring_slot_t *r = &dec->ring[(uint64_t)e % dec->ring_n];
      ready_push(dec, r);
      r->state = RING_EMPTY;
    }
  }
  memset(dec->col, 0, sizeof dec->col);
  dec->held_count = 0;
  dec->have_ssrc = 0;
  dec->have_cursor = 0;
  dec->have_hi = 0;
  dec->late_run = 0;
}

static int seq_jumped(const fec2022_dec_t *dec, const unsigned char *pkt) {
  int delta;

  if (!dec->have_hi) return 0;
  if (dec->have_ssrc && be32_get(pkt + 8) != dec->src_ssrc) return 1;
  delta = (int16_t)(be16_get(pkt + 2) - (uint16_t)dec->hi_ext);
  return delta > (int)dec->ring_n || delta < -(int)dec->ring_n;
}

static int source_apply(fec2022_dec_t *dec, const unsigned char *pkt, size_t len) {
  int64_t ext;
  int64_t row;
  int64_t gen;
  unsigned c;
  unsigned idx;
  fec2022_col_t *cs;
  fec2022_gen_t *g;
  size_t si;
  int gs;

  if (len < 12 || len > FEC2022_MAX_PKT) return -1;
  if (seq_jumped(dec, pkt)) resync(dec);
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
  expire_columns(dec);
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
  gs = select_gen(dec, c, gen);
  if (gs < 0) {
    if (++dec->late_run >= dec->l * dec->d) {
      resync(dec);
      return source_apply(dec, pkt, len);
    }
    release_pending(dec);
    return 0;
  }
  dec->late_run = 0;
  g = &cs->g[gs];

  si = slot_idx(dec, c, (unsigned)gs, idx);
  if (!dec->slot_seen[si]) {
    dec->slot_seen[si] = 1;
    dec->slot_len[si] = len;
    memcpy(dec->slot_pkt + si * FEC2022_MAX_PKT, pkt, len);
    g->real_count++;
  }
  if (g->real_count == dec->d || (g->repair_seen && g->real_count == dec->d - 1))
    resolve_column(dec, c, (unsigned)gs);
  else
    release_pending(dec);
  return 0;
}

/* 1: repair covers packets not seen yet, keep it. 0: applied or dropped */
static int repair_apply(fec2022_dec_t *dec, const unsigned char *pkt, size_t len) {
  const unsigned char *fh;
  int64_t snb;
  int64_t gen;
  size_t ld = (size_t)dec->l * dec->d;
  unsigned c;
  fec2022_col_t *cs;
  fec2022_gen_t *g;
  size_t payload_off;
  size_t payload_len;
  int gs;

  if (!dec->have_cursor) return 1;
  if (len < 12 + 16 || (pkt[0] >> 6) != 2) return 0;
  fh = pkt + 12; /* repair RTP header never has CSRC: low bits of byte 0 carry recovery */
  snb = ext_seq(dec, be16_get(fh), 0); /* RFC6015 SN base low */
  if (fh[13] != dec->l || fh[14] != dec->d) return 0; /* RFC6015 Offset, NA */
  if (snb > dec->hi_ext) return 1;
  if (dec->hi_ext - snb > (int64_t)dec->ring_n) return 0;

  c = (unsigned)((uint64_t)snb % dec->l);
  cs = &dec->col[c];
  if (cs->start_valid && (snb - cs->start) % (int64_t)ld != 0) close_column(dec, c);
  if (!cs->start_valid) {
    cs->start = snb;
    cs->start_valid = 1;
  }
  gen = floordiv((snb - cs->start) / (int64_t)dec->l, dec->d);
  gs = select_gen(dec, c, gen);
  if (gs < 0) return 0;
  g = &cs->g[gs];
  if (g->repair_seen) return 0;

  g->repair_seen = 1;
  g->rp = (pkt[0] >> 5) & 1;
  g->rx = (pkt[0] >> 4) & 1;
  g->rcc = pkt[0] & 0xF;
  g->rm = (pkt[1] >> 7) & 1;
  g->rpt_xor = fh[4] & 0x7F;           /* RFC6015 PT recovery */
  g->rts_xor = be32_get(fh + 8);    /* RFC6015 TS recovery */
  g->rlen_xor = be16_get(fh + 2);   /* RFC6015 Length recovery */
  payload_off = 12 + 16;
  payload_len = len - payload_off;
  if (payload_len > sizeof g->repair_payload) payload_len = sizeof g->repair_payload;
  memset(g->repair_payload, 0, sizeof g->repair_payload);
  memcpy(g->repair_payload, fh + 16, payload_len);
  if (g->real_count == dec->d || g->real_count == dec->d - 1) resolve_column(dec, c, (unsigned)gs);
  return 0;
}

static void held_retry(fec2022_dec_t *dec) {
  unsigned i = 0;

  while (i < dec->held_count) {
    unsigned char *h = dec->held + (size_t)i * FEC2022_MAX_REPAIR;
    if (repair_apply(dec, h, dec->held_len[i])) {
      i++;
      continue;
    }
    dec->held_count--;
    memmove(h, h + FEC2022_MAX_REPAIR, (size_t)(dec->held_count - i) * FEC2022_MAX_REPAIR);
    memmove(dec->held_len + i, dec->held_len + i + 1, (size_t)(dec->held_count - i) * sizeof *dec->held_len);
  }
}

int fec2022_dec_source(fec2022_dec_t *dec, const unsigned char *pkt, size_t len) {
  int rc = source_apply(dec, pkt, len);
  if (dec->held_count) held_retry(dec);
  return rc;
}

void fec2022_dec_repair(fec2022_dec_t *dec, const unsigned char *pkt, size_t len) {
  if (len > FEC2022_MAX_REPAIR || !repair_apply(dec, pkt, len)) return;
  if (dec->held_count == dec->held_cap) {
    dec->held_count--;
    memmove(dec->held, dec->held + FEC2022_MAX_REPAIR, (size_t)dec->held_count * FEC2022_MAX_REPAIR);
    memmove(dec->held_len, dec->held_len + 1, (size_t)dec->held_count * sizeof *dec->held_len);
  }
  memcpy(dec->held + (size_t)dec->held_count * FEC2022_MAX_REPAIR, pkt, len);
  dec->held_len[dec->held_count++] = len;
}

size_t fec2022_dec_next_len(const fec2022_dec_t *dec) {
  return dec->ready_count ? dec->ready_len[dec->ready_head] : 0;
}

size_t fec2022_dec_drain(fec2022_dec_t *dec, unsigned char *out, size_t cap) {
  size_t len;
  if (dec->ready_count == 0) return 0;
  len = dec->ready_len[dec->ready_head];
  if (len > cap) return 0;
  memcpy(out, dec->ready + (size_t)dec->ready_head * FEC2022_MAX_PKT, len);
  dec->ready_head = (dec->ready_head + 1) % (unsigned)dec->ring_n;
  dec->ready_count--;
  return len;
}
