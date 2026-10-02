/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "framequeue.h"

typedef struct node {
  struct node *next;
  source_frame_t f;
  unsigned char data[];
} node_t;

struct framequeue {
  node_t *head;
  node_t *tail;
  node_t *popped; /* backs last out->data */
  size_t count;
  uint64_t us;
};

framequeue_t *framequeue_new(void) { return calloc(1, sizeof(framequeue_t)); }

static void free_list(node_t *n) {
  while (n) {
    node_t *next = n->next;
    free(n);
    n = next;
  }
}

void framequeue_free(framequeue_t *q) {
  if (!q) return;
  free_list(q->head);
  free(q->popped);
  free(q);
}

static uint64_t frame_us(const source_frame_t *f) { return f->sample_rate ? (uint64_t)f->samples * 1000000ULL / f->sample_rate : 0; }

int framequeue_push(framequeue_t *q, const source_frame_t *f) {
  node_t *n = malloc(sizeof *n + f->len);
  if (!n) return -1;
  n->next = NULL;
  n->f = *f;
  memcpy(n->data, f->data, f->len);
  n->f.data = n->data;
  if (q->tail) q->tail->next = n;
  else q->head = n;
  q->tail = n;
  q->count++;
  q->us += frame_us(f);
  return 0;
}

int framequeue_pop(framequeue_t *q, source_frame_t *out) {
  node_t *n = q->head;
  if (!n) return 0;
  q->head = n->next;
  if (!q->head) q->tail = NULL;
  q->count--;
  q->us -= frame_us(&n->f);
  free(q->popped);
  q->popped = n;
  *out = n->f;
  return 1;
}

size_t framequeue_count(const framequeue_t *q) { return q->count; }

unsigned framequeue_ms(const framequeue_t *q) { return (unsigned)(q->us / 1000); }
