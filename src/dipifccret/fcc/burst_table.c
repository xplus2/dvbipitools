/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include "lib/log.h"

#include "../version.h"
#include "burst_table.h"

burst_table_t *burst_table_new(size_t cap) {
  burst_table_t *t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->slots = calloc(cap, sizeof *t->slots);
  t->index = sockaddr_index_new(cap);
  if (!t->slots || !t->index) {
    free(t->slots);
    sockaddr_index_free(t->index);
    free(t);
    return NULL;
  }
  t->cap = cap;
  pthread_mutex_init(&t->lock, NULL);
  return t;
}

void burst_table_free(burst_table_t *t) {
  if (!t)
    return;
  for (size_t i = 0; i < t->cap; i++)
    if (t->slots[i].in_use)
      burst_release(t->slots[i].b);
  pthread_mutex_destroy(&t->lock);
  sockaddr_index_free(t->index);
  free(t->slots);
  free(t);
}

static int sockaddr_eq(const struct sockaddr_storage *a, socklen_t alen, const struct sockaddr *b, socklen_t blen) {
  if (alen != blen || a->ss_family != b->sa_family)
    return 0;
  if (b->sa_family == AF_INET) {
    const struct sockaddr_in *ba = (const struct sockaddr_in *)b;
    const struct sockaddr_in *aa = (const struct sockaddr_in *)a;
    return aa->sin_port == ba->sin_port && aa->sin_addr.s_addr == ba->sin_addr.s_addr;
  }
  if (b->sa_family == AF_INET6) {
    const struct sockaddr_in6 *ba = (const struct sockaddr_in6 *)b;
    const struct sockaddr_in6 *aa = (const struct sockaddr_in6 *)a;
    return aa->sin6_port == ba->sin6_port && memcmp(&aa->sin6_addr, &ba->sin6_addr, sizeof aa->sin6_addr) == 0;
  }
  return 0;
}

/* index hit, re-validated against authoritative slots[]. pacer thread
   clears in_use: stale hits dropped. caller holds t->lock. SIZE_MAX if no
   live session for addr */
static size_t find_valid(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen) {
  size_t idx = sockaddr_index_find(t->index, addr, addrlen);
  if (idx == SIZE_MAX)
    return SIZE_MAX;
  if (!t->slots[idx].in_use || !sockaddr_eq(&t->slots[idx].addr, t->slots[idx].addrlen, addr, addrlen)) {
    sockaddr_index_remove(t->index, addr, addrlen);
    return SIZE_MAX;
  }
  return idx;
}

/* caller holds t->lock. claims free slot for addr/fd/b, indexes it. NULL on full tab */
static burst_slot_t *claim_locked(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int fd, burst_t *b) {
  for (size_t i = 0; i < t->cap; i++) {
    if (!t->slots[i].in_use) {
      memcpy(&t->slots[i].addr, addr, addrlen);
      t->slots[i].addrlen = addrlen;
      t->slots[i].fd = fd;
      t->slots[i].b = b;
      t->slots[i].msn = 0;
      t->slots[i].nack_count = 0;
      t->slots[i].congestion_adapted = 0;
      t->slots[i].in_use = 1;
      sockaddr_index_remove(t->index, addr, addrlen); /* drop stale entry before insert */
      sockaddr_index_insert(t->index, addr, addrlen, i);
      return &t->slots[i];
    }
  }
  return NULL;
}

burst_slot_t *burst_table_claim(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int fd, burst_t *b) {
  burst_slot_t *s;
  pthread_mutex_lock(&t->lock);
  s = claim_locked(t, addr, addrlen, fd, b);
  pthread_mutex_unlock(&t->lock);
  if (!s)
    log_line(TOOL_NAME ": max-bursts (%zu) reached, rejecting new burst session", t->cap);
  return s;
}

burst_slot_t *burst_table_find(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen) {
  size_t idx;
  pthread_mutex_lock(&t->lock);
  idx = find_valid(t, addr, addrlen);
  pthread_mutex_unlock(&t->lock);
  return idx == SIZE_MAX ? NULL : &t->slots[idx];
}

int burst_table_start(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int fd, burst_t *b, uint8_t *msn_out) {
  size_t i;
  burst_t *old = NULL;
  int updated = 0;

  pthread_mutex_lock(&t->lock);
  i = find_valid(t, addr, addrlen);
  if (i != SIZE_MAX) {
    old = t->slots[i].b;
    t->slots[i].fd = fd;
    t->slots[i].b = b;
    *msn_out = ++t->slots[i].msn;
    updated = 1;
  } else {
    burst_slot_t *s = claim_locked(t, addr, addrlen, fd, b);
    if (s) {
      *msn_out = 0;
      pthread_mutex_unlock(&t->lock);
      return 0;
    }
  }
  pthread_mutex_unlock(&t->lock);

  if (updated) {
    burst_terminate(old, 0, 0);
    burst_release(old);
    return 1;
  }

  log_line(TOOL_NAME ": max-bursts (%zu) reached, rejecting new burst session", t->cap);
  return -1;
}

int burst_table_note_nack(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, unsigned threshold, burst_table_nack_result_t *out) {
  size_t idx;

  memset(out, 0, sizeof *out);
  pthread_mutex_lock(&t->lock);
  idx = find_valid(t, addr, addrlen);
  if (idx == SIZE_MAX) {
    pthread_mutex_unlock(&t->lock);
    return 0;
  }

  {
    burst_slot_t *slot = &t->slots[idx];
    unsigned adapt_at;

    slot->nack_count++;
    if (threshold == 0) {
      pthread_mutex_unlock(&t->lock);
      return 1;
    }

    adapt_at = threshold / 2;
    if (slot->nack_count >= threshold) {
      burst_terminate(slot->b, 0, 0);
      out->action = BURST_TABLE_NACK_TERMINATED;
    } else if (!slot->congestion_adapted && adapt_at > 0 && slot->nack_count >= adapt_at) {
      out->new_bps = atomic_load_explicit(&slot->b->target_bps, memory_order_relaxed) * 0.5;
      atomic_store_explicit(&slot->b->target_bps, out->new_bps, memory_order_relaxed);
      slot->congestion_adapted = 1;
      out->action = BURST_TABLE_NACK_ADAPTED;
      out->msn = ++slot->msn;
    }
  }
  pthread_mutex_unlock(&t->lock);
  return 1;
}

int burst_table_terminate(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int has_stop_seq, uint32_t stop_seqnum) {
  size_t idx;
  pthread_mutex_lock(&t->lock);
  idx = find_valid(t, addr, addrlen);
  if (idx != SIZE_MAX)
    burst_terminate(t->slots[idx].b, has_stop_seq, stop_seqnum);
  pthread_mutex_unlock(&t->lock);
  return idx != SIZE_MAX;
}
