/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "releaseq.h"

#define INITIAL_PACKETS 256

typedef struct {
  unsigned char pkt[188];
  uint64_t tag90;
  int has_tag;
} slot_t;

struct releaseq {
  slot_t *slots;
  size_t cap;
  size_t max;
  size_t head;
  size_t len;
};

releaseq_t *releaseq_new(size_t max_packets) {
  releaseq_t *q;
  if (max_packets == 0) return NULL;
  q = calloc(1, sizeof *q);
  if (!q) return NULL;
  q->max = max_packets;
  return q;
}

void releaseq_free(releaseq_t *q) {
  if (!q) return;
  free(q->slots);
  free(q);
}

size_t releaseq_len(const releaseq_t *q) { return q->len; }

static int grow(releaseq_t *q) {
  size_t ncap = q->cap ? q->cap * 2 : INITIAL_PACKETS;
  slot_t *n;
  if (ncap > q->max) ncap = q->max;
  if (ncap <= q->cap) return -1;
  n = malloc(ncap * sizeof *n);
  if (!n) return -1;
  for (size_t i = 0; i < q->len; i++) n[i] = q->slots[(q->head + i) % q->cap];
  free(q->slots);
  q->slots = n;
  q->cap = ncap;
  q->head = 0;
  return 0;
}

int releaseq_push(releaseq_t *q, const unsigned char pkt188[188], int has_tag, uint64_t tag90) {
  slot_t *s;
  if (q->len == q->cap && grow(q)) return -1;
  s = &q->slots[(q->head + q->len) % q->cap];
  memcpy(s->pkt, pkt188, 188);
  s->has_tag = has_tag;
  s->tag90 = tag90;
  q->len++;
  return 0;
}

const unsigned char *releaseq_head(const releaseq_t *q, int *has_tag, uint64_t *tag90) {
  const slot_t *s;
  if (!q->len) return NULL;
  s = &q->slots[q->head];
  *has_tag = s->has_tag;
  *tag90 = s->tag90;
  return s->pkt;
}

void releaseq_pop(releaseq_t *q) {
  if (!q->len) return;
  q->head = (q->head + 1) % q->cap;
  q->len--;
}
