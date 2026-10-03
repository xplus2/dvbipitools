/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "pcrclock.h"

#define PCR_BYTE_INDEX 10

void pcrclock_init(pcrclock_t *c, uint64_t bps, uint64_t base27) {
  c->bps = bps;
  c->base27 = base27 % PCR_MODULUS;
}

uint64_t pcrclock_at(const pcrclock_t *c, uint64_t packet_index) {
  uint64_t bits = (packet_index * 188 + PCR_BYTE_INDEX) * 8;
  uint64_t q = bits / c->bps;
  uint64_t r = bits % c->bps;
  uint64_t ticks = (q % ((uint64_t)1 << 33)) * (PCR_CLOCK_HZ / 300) % ((uint64_t)1 << 33) * 300;
  ticks = (ticks + (r * PCR_CLOCK_HZ + c->bps / 2) / c->bps) % PCR_MODULUS;
  return pcr_add(c->base27, ticks);
}

uint64_t pcr_add(uint64_t a, uint64_t b) { return (a + b) % PCR_MODULUS; }

uint64_t pcr_sub(uint64_t a, uint64_t b) { return (a + PCR_MODULUS - b % PCR_MODULUS) % PCR_MODULUS; }

static int has_pcr(const unsigned char pkt188[188]) {
  unsigned afc = (pkt188[3] >> 4) & 0x3;
  return (afc == 0x2 || afc == 0x3) && pkt188[4] >= 7 && (pkt188[5] & 0x10);
}

int pcr_packet_read(const unsigned char pkt188[188], uint64_t *pcr27) {
  uint64_t base;
  unsigned ext;
  if (!has_pcr(pkt188)) return 0;
  base = ((uint64_t)pkt188[6] << 25) | ((uint64_t)pkt188[7] << 17) | ((uint64_t)pkt188[8] << 9) | ((uint64_t)pkt188[9] << 1) | (pkt188[10] >> 7);
  ext = ((unsigned)(pkt188[10] & 0x01) << 8) | pkt188[11];
  *pcr27 = base * 300 + ext;
  return 1;
}

int pcr_packet_write(unsigned char pkt188[188], uint64_t pcr27) {
  uint64_t base;
  unsigned ext;
  if (!has_pcr(pkt188)) return -1;
  pcr27 %= PCR_MODULUS;
  base = pcr27 / 300;
  ext = (unsigned)(pcr27 % 300);
  pkt188[6] = (unsigned char)(base >> 25);
  pkt188[7] = (unsigned char)(base >> 17);
  pkt188[8] = (unsigned char)(base >> 9);
  pkt188[9] = (unsigned char)(base >> 1);
  pkt188[10] = (unsigned char)(((base & 1) << 7) | (pkt188[10] & 0x7E) | ((ext >> 8) & 1));
  pkt188[11] = (unsigned char)ext;
  return 0;
}

void pcr_packet_build(unsigned char pkt188[188], unsigned pid, unsigned char cc, uint64_t pcr27) {
  memset(pkt188, 0xFF, 188);
  pkt188[0] = 0x47;
  pkt188[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt188[2] = (unsigned char)pid;
  pkt188[3] = (unsigned char)(0x20 | (cc & 0x0F));
  pkt188[4] = 183;
  pkt188[5] = 0x10;
  pkt188[10] = 0x7E;
  pcr_packet_write(pkt188, pcr27);
}
