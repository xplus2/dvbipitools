/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_PCRCLOCK_H
#define DIPITVHEAD_PCRCLOCK_H

#include <stdint.h>

#define PCR_CLOCK_HZ 27000000ULL
#define PCR_MODULUS (((uint64_t)1 << 33) * 300ULL)

typedef struct {
  uint64_t bps;
  uint64_t base27;
} pcrclock_t;

void pcrclock_init(pcrclock_t *c, uint64_t bps, uint64_t base27);
uint64_t pcrclock_at(const pcrclock_t *c, uint64_t packet_index);

uint64_t pcr_add(uint64_t a, uint64_t b);
uint64_t pcr_sub(uint64_t a, uint64_t b);

int pcr_packet_read(const unsigned char pkt188[188], uint64_t *pcr27);
int pcr_packet_write(unsigned char pkt188[188], uint64_t pcr27);
void pcr_packet_build(unsigned char pkt188[188], unsigned pid, unsigned char cc, uint64_t pcr27);

#endif
