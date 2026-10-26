/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "../helper/beutil.h"
#include "fec2022.h"

#define EXT_BASE ((int64_t)1 << 40)
#define REPAIR_PAYLOAD (FEC2022_MAX_PKT - 12)

/* one column group, keyed by SN base */
typedef struct {
  int valid;
  int done;
  int64_t snb;
  unsigned have;
  unsigned char rp;
  unsigned char rx;
  unsigned char rcc;
  unsigned char rm;
  unsigned char rpt_xor;
  uint32_t rts_xor;
  uint16_t rlen_xor;
  unsigned char payload[REPAIR_PAYLOAD];
} fec2022_rep_t;

typedef enum { SLOT_EMPTY, SLOT_READY, SLOT_DROPPED } slot_state_t;

/* ring doubles as XOR history: delivered packets stay until overwritten */
typedef struct {
  slot_state_t state;
  int64_t ext;
  size_t len;
  unsigned char pkt[FEC2022_MAX_PKT];
} slot_t;

struct fec2022_dec {
  unsigned l;
  unsigned d;
  uint32_t src_ssrc;
  int have_ssrc;
  int have_hi;
  int64_t hi_ext;
  uint16_t hi_seq;
  int64_t resume_ext;
  int64_t rel;
  unsigned late_run;
  size_t ring_n;
  slot_t *ring;
  fec2022_rep_t *rep; /* [l][2] */
  size_t early_len;
  unsigned char early[FEC2022_MAX_REPAIR];
};

fec2022_dec_t *fec2022_dec_new(unsigned l, unsigned d) {
  fec2022_dec_t *dec;

  if (l == 0 || d == 0 || l > FEC2022_MAX_L || d > FEC2022_MAX_LD / l) return NULL;
  dec = calloc(1, sizeof *dec);
  if (!dec) return NULL;
  dec->l = l;
  dec->d = d;
  dec->ring_n = (size_t)l * d * 5;
  dec->ring = calloc(dec->ring_n, sizeof *dec->ring);
  dec->rep = calloc((size_t)l * 2, sizeof *dec->rep);
  if (!dec->ring || !dec->rep) {
    fec2022_dec_free(dec);
    return NULL;
  }
  dec->resume_ext = EXT_BASE;
  dec->rel = EXT_BASE;
  dec->hi_ext = EXT_BASE - 1;
  return dec;
}

void fec2022_dec_free(fec2022_dec_t *dec) {
  if (!dec) return;
  free(dec->ring);
  free(dec->rep);
  free(dec);
}

static slot_t *slot_at(const fec2022_dec_t *dec, int64_t ext) {
  return &dec->ring[(uint64_t)ext % dec->ring_n];
}

static int slot_ready(const fec2022_dec_t *dec, int64_t ext) {
  const slot_t *s = slot_at(dec, ext);
  return s->state == SLOT_READY && s->ext == ext;
}

/* unwrap 16 bit sequence against highest */
static int64_t ext_seq(fec2022_dec_t *dec, uint16_t seq, int advance) {
  int64_t ext;
  if (!dec->have_hi) {
    dec->hi_ext = dec->resume_ext;
    dec->hi_seq = seq;
    dec->have_hi = 1;
  }
  ext = dec->hi_ext + (int16_t)(uint16_t)(seq - dec->hi_seq);
  if (advance && ext > dec->hi_ext) {
    dec->hi_ext = ext;
    dec->hi_seq = seq;
  }
  return ext;
}

static fec2022_rep_t *rep_for(const fec2022_dec_t *dec, int64_t ext) {
  fec2022_rep_t *r = dec->rep + (size_t)((uint64_t)ext % dec->l) * 2;
  int64_t span = (int64_t)(dec->d - 1) * dec->l;

  for (unsigned i = 0; i < 2; i++)
    if (r[i].valid && ext >= r[i].snb && ext <= r[i].snb + span) return &r[i];
  return NULL;
}

/* hole at ext cannot fill anymore: its repair is in and all members passed */
static int hole_final(const fec2022_dec_t *dec, int64_t ext) {
  const fec2022_rep_t *r = rep_for(dec, ext);
  return r && dec->hi_ext >= r->snb + (int64_t)(dec->d - 1) * dec->l + dec->l;
}

static void skip_lost(fec2022_dec_t *dec) {
  int64_t giveup = 2 * (int64_t)dec->l * dec->d;

  for (;;) {
    const slot_t *s = slot_at(dec, dec->rel);
    if (s->ext == dec->rel && s->state == SLOT_READY) break;
    if (s->ext == dec->rel && s->state == SLOT_DROPPED) {
      dec->rel++;
      continue;
    }
    if (dec->rel > dec->hi_ext) break;
    if (dec->hi_ext - dec->rel < giveup && !hole_final(dec, dec->rel)) break;
    dec->rel++;
  }
}

/* rebuild the single missing member in place */
static void rep_recover(const fec2022_dec_t *dec, fec2022_rep_t *r) {
  int64_t missing = 0;
  unsigned present = 0;
  unsigned char p;
  unsigned char x;
  unsigned char cc;
  unsigned char m;
  slot_t *dst;

  for (unsigned i = 0; i < dec->d; i++) {
    int64_t ext = r->snb + (int64_t)i * dec->l;
    if (slot_ready(dec, ext)) present++;
    else missing = ext;
  }
  if (present == dec->d) {
    r->done = 1;
    return;
  }
  if (present != dec->d - 1) return;
  r->done = 1;
  r->have = dec->d;
  if (missing < dec->rel || missing - dec->rel >= (int64_t)dec->ring_n) return;

  p = r->rp;
  x = r->rx;
  cc = r->rcc;
  m = r->rm;
  for (unsigned i = 0; i < dec->d; i++) {
    int64_t ext = r->snb + (int64_t)i * dec->l;
    const slot_t *s;

    if (ext == missing) continue;
    s = slot_at(dec, ext);
    p ^= (s->pkt[0] >> 5) & 1;
    x ^= (s->pkt[0] >> 4) & 1;
    cc ^= s->pkt[0] & 0xF;
    m ^= (s->pkt[1] >> 7) & 1;
    r->rpt_xor ^= s->pkt[1] & 0x7F;
    r->rts_xor ^= be32_get(s->pkt + 4);
    r->rlen_xor ^= (uint16_t)(s->len - 12);
    for (size_t k = 0; k < s->len - 12; k++) r->payload[k] ^= s->pkt[12 + k];
  }
  dst = slot_at(dec, missing);
  dst->ext = missing;
  if (r->rlen_xor > sizeof r->payload) {
    dst->state = SLOT_DROPPED;
    return;
  }
  dst->pkt[0] = (unsigned char)(0x80 | (p << 5) | (x << 4) | cc);
  dst->pkt[1] = (unsigned char)((m << 7) | r->rpt_xor);
  be16_put(dst->pkt + 2, (uint16_t)(dec->hi_seq + (missing - dec->hi_ext)));
  be32_put(dst->pkt + 4, r->rts_xor);
  be32_put(dst->pkt + 8, dec->src_ssrc);
  memcpy(dst->pkt + 12, r->payload, r->rlen_xor);
  dst->len = (size_t)12 + r->rlen_xor;
  dst->state = SLOT_READY;
}

static void rep_note_source(fec2022_dec_t *dec, int64_t ext) {
  fec2022_rep_t *r = rep_for(dec, ext);

  if (!r || r->done) return;
  r->have++;
  if (r->have == dec->d) r->done = 1;
  else if (r->have == dec->d - 1) rep_recover(dec, r);
}

static void resync(fec2022_dec_t *dec) {
  for (size_t i = 0; i < dec->ring_n; i++)
    if (dec->ring[i].ext > dec->hi_ext) dec->ring[i].state = SLOT_EMPTY;
  for (int64_t e = dec->rel; e <= dec->hi_ext; e++) {
    slot_t *s = slot_at(dec, e);
    if (s->state == SLOT_READY && s->ext == e) continue;
    s->state = SLOT_DROPPED;
    s->ext = e;
  }
  memset(dec->rep, 0, (size_t)dec->l * 2 * sizeof *dec->rep);
  dec->resume_ext = dec->hi_ext + 1;
  dec->have_ssrc = 0;
  dec->have_hi = 0;
  dec->late_run = 0;
  skip_lost(dec);
}

static int seq_jumped(const fec2022_dec_t *dec, const unsigned char *pkt) {
  int delta;

  if (!dec->have_hi) return 0;
  if (dec->have_ssrc && be32_get(pkt + 8) != dec->src_ssrc) return 1;
  delta = (int16_t)(be16_get(pkt + 2) - dec->hi_seq);
  return delta > (int)(2 * dec->l * dec->d) || delta < -(int)dec->ring_n;
}

int fec2022_dec_source(fec2022_dec_t *dec, const unsigned char *pkt, size_t len) {
  int64_t ext;
  slot_t *s;

  if (len < 12 || len > FEC2022_MAX_PKT) return -1;
  if (seq_jumped(dec, pkt)) resync(dec);
  ext = ext_seq(dec, be16_get(pkt + 2), 1);
  if (!dec->have_ssrc) {
    dec->src_ssrc = be32_get(pkt + 8);
    dec->have_ssrc = 1;
  }
  if (ext < dec->rel) {
    if (++dec->late_run >= dec->l * dec->d) {
      resync(dec);
      return fec2022_dec_source(dec, pkt, len);
    }
    return 0;
  }
  dec->late_run = 0;
  if (ext - dec->rel >= (int64_t)dec->ring_n) dec->rel = ext - (int64_t)dec->ring_n + 1;
  if (slot_ready(dec, ext)) return 0;
  s = slot_at(dec, ext);
  s->state = SLOT_READY;
  s->ext = ext;
  s->len = len;
  memcpy(s->pkt, pkt, len);
  rep_note_source(dec, ext);
  skip_lost(dec);
  if (dec->early_len) {
    size_t n = dec->early_len;
    dec->early_len = 0;
    fec2022_dec_repair(dec, dec->early, n);
  }
  return 0;
}

void fec2022_dec_repair(fec2022_dec_t *dec, const unsigned char *pkt, size_t len) {
  const unsigned char *fh;
  fec2022_rep_t *r;
  int64_t snb;
  size_t payload_len;

  if (len > FEC2022_MAX_REPAIR || len < 12 + 16 || (pkt[0] >> 6) != 2) return;
  if (!dec->have_hi) {
    memcpy(dec->early, pkt, len);
    dec->early_len = len;
    return;
  }
  fh = pkt + 12; /* repair RTP header never has CSRC: low bits of byte 0 carry recovery */
  if (fh[13] != dec->l || fh[14] != dec->d) return; /* RFC6015 Offset, NA */
  snb = ext_seq(dec, be16_get(fh), 0); /* RFC6015 SN base low */
  if (dec->hi_ext - snb > (int64_t)dec->ring_n || snb - dec->hi_ext > (int64_t)dec->l * dec->d) return;

  r = dec->rep + (size_t)((uint64_t)snb % dec->l) * 2;
  if ((r[0].valid && r[0].snb == snb) || (r[1].valid && r[1].snb == snb)) return;
  if (r[0].valid && r[1].valid) {
    if (snb < r[0].snb && snb < r[1].snb) return;
    if (r[1].snb < r[0].snb) r++;
  } else if (r[0].valid) {
    r++;
  }
  r->valid = 1;
  r->done = 0;
  r->snb = snb;
  r->rp = (pkt[0] >> 5) & 1;
  r->rx = (pkt[0] >> 4) & 1;
  r->rcc = pkt[0] & 0xF;
  r->rm = (pkt[1] >> 7) & 1;
  r->rpt_xor = fh[4] & 0x7F;      /* RFC6015 PT recovery */
  r->rts_xor = be32_get(fh + 8);  /* RFC6015 TS recovery */
  r->rlen_xor = be16_get(fh + 2); /* RFC6015 Length recovery */
  payload_len = len - (12 + 16);
  memset(r->payload, 0, sizeof r->payload);
  memcpy(r->payload, fh + 16, payload_len);
  r->have = 0;
  for (unsigned i = 0; i < dec->d; i++)
    if (slot_ready(dec, snb + (int64_t)i * dec->l)) r->have++;
  if (r->have == dec->d) r->done = 1;
  else if (r->have == dec->d - 1) rep_recover(dec, r);
  skip_lost(dec);
}

size_t fec2022_dec_next_len(const fec2022_dec_t *dec) {
  const slot_t *s = slot_at(dec, dec->rel);
  return (s->state == SLOT_READY && s->ext == dec->rel) ? s->len : 0;
}

size_t fec2022_dec_drain(fec2022_dec_t *dec, unsigned char *out, size_t cap) {
  size_t len = fec2022_dec_next_len(dec);

  if (len == 0 || len > cap) return 0;
  memcpy(out, slot_at(dec, dec->rel)->pkt, len);
  dec->rel++;
  skip_lost(dec);
  return len;
}
