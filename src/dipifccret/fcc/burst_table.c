/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/log.h"

#include "../version.h"
#include "burst_table.h"

static int stripe_init(burst_stripe_t *s, size_t base, size_t count) {
  memset(s, 0, sizeof *s);
  s->free_list = calloc(count, sizeof *s->free_list);
  s->index = sockaddr_index_new(count);
  if (!s->free_list || !s->index) {
    free(s->free_list);
    sockaddr_index_free(s->index);
    return 0;
  }
  s->base = base;
  s->count = count;
  for (size_t i = 0; i < count; i++)
    s->free_list[i] = base + count - 1 - i;
  s->free_count = count;
  pthread_mutex_init(&s->lock, NULL);
  return 1;
}

static void stripe_destroy(burst_stripe_t *s) {
  pthread_mutex_destroy(&s->lock);
  sockaddr_index_free(s->index);
  free(s->free_list);
}

burst_table_t *burst_table_new(size_t cap) {
  burst_table_t *t;
  size_t n;
  size_t base;
  size_t extra;
  size_t off;

  if (cap == 0)
    return NULL;
  t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->slots = calloc(cap, sizeof *t->slots);
  if (!t->slots) {
    free(t);
    return NULL;
  }
  t->cap = cap;

  n = cap < BURST_TABLE_STRIPE_COUNT_MAX ? cap : BURST_TABLE_STRIPE_COUNT_MAX;
  base = cap / n; /* remainder spread over first `extra` stripes: total always == cap exactly */
  extra = cap % n;
  off = 0;
  for (size_t i = 0; i < n; i++) {
    size_t count = base + (i < extra ? 1 : 0);
    if (!stripe_init(&t->stripes[i], off, count)) {
      for (size_t j = 0; j < i; j++)
        stripe_destroy(&t->stripes[j]);
      free(t->slots);
      free(t);
      return NULL;
    }
    off += count;
  }
  t->stripe_count = n;
  return t;
}

void burst_table_free(burst_table_t *t) {
  if (!t)
    return;
  for (size_t i = 0; i < t->cap; i++)
    if (atomic_load_explicit(&t->slots[i].in_use, memory_order_relaxed))
      burst_release(atomic_load_explicit(&t->slots[i].b, memory_order_relaxed));
  for (size_t i = 0; i < t->stripe_count; i++)
    stripe_destroy(&t->stripes[i]);
  free(t->slots);
  free(t);
}

static burst_stripe_t *pick_stripe(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen) {
  return &t->stripes[sockaddr_stripe_of(addr, addrlen, t->stripe_count)];
}

/* idx always falls within some stripe's range: NULL unreachable */
static burst_stripe_t *stripe_of_idx(burst_table_t *t, size_t idx) {
  for (size_t i = 0; i < t->stripe_count; i++)
    if (idx >= t->stripes[i].base && idx < t->stripes[i].base + t->stripes[i].count)
      return &t->stripes[i];
  return NULL;
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

/* plain sockaddr_storage from slot's atomic words */
static void slot_read_addr(const burst_slot_t *s, struct sockaddr_storage *out) {
  uint64_t words[BURST_ADDR_WORDS] = {0};
  for (size_t w = 0; w < BURST_ADDR_WORDS; w++)
    words[w] = atomic_load_explicit(&s->addr_words[w], memory_order_relaxed);
  memcpy(out, words, sizeof *out);
}

/* index hit, re-validated against authoritative slots[]. pacer thread
   clears in_use: stale hits dropped. caller holds st->lock. SIZE_MAX if no
   live session for addr */
static size_t find_valid(const burst_table_t *t, burst_stripe_t *st, const struct sockaddr *addr, socklen_t addrlen) {
  size_t idx = sockaddr_index_find(st->index, addr, addrlen);
  struct sockaddr_storage sa;

  if (idx == SIZE_MAX)
    return SIZE_MAX;
  if (!atomic_load_explicit(&t->slots[idx].in_use, memory_order_relaxed)) {
    sockaddr_index_remove(st->index, addr, addrlen);
    return SIZE_MAX;
  }
  slot_read_addr(&t->slots[idx], &sa);
  if (!sockaddr_eq(&sa, atomic_load_explicit(&t->slots[idx].addrlen, memory_order_relaxed), addr, addrlen)) {
    sockaddr_index_remove(st->index, addr, addrlen);
    return SIZE_MAX;
  }
  return idx;
}

/* caller holds st->lock. pops st's own free list for addr/fd/b, indexes it.
   NULL if free list empty: reject, don't scan other stripes.
   gen-bracketed: pacer may read lock-free up to in_use flip below */
static burst_slot_t *claim_locked(burst_table_t *t, burst_stripe_t *st, const struct sockaddr *addr, socklen_t addrlen, int fd, burst_t *b) {
  size_t idx;
  burst_slot_t *s;
  uint64_t words[BURST_ADDR_WORDS];
  unsigned g;

  if (st->free_count == 0)
    return NULL;
  idx = st->free_list[--st->free_count];
  s = &t->slots[idx];

  {
    size_t want = idx + 1;
    size_t cur = atomic_load_explicit(&t->high_water_mark, memory_order_relaxed);
    while (want > cur && !atomic_compare_exchange_weak_explicit(&t->high_water_mark, &cur, want, memory_order_relaxed, memory_order_relaxed))
      ; /* concurrent claim in another stripe raced ahead: retry with its value */
  }

  g = seqlock_begin_write(&s->gen);
  memset(words, 0, sizeof words);
  memcpy(words, addr, addrlen);
  for (size_t w = 0; w < BURST_ADDR_WORDS; w++)
    atomic_store_explicit(&s->addr_words[w], words[w], memory_order_relaxed);
  atomic_store_explicit(&s->addrlen, addrlen, memory_order_relaxed);
  atomic_store_explicit(&s->fd, fd, memory_order_relaxed);
  atomic_store_explicit(&s->b, b, memory_order_relaxed);
  s->msn = 0;
  s->nack_count = 0;
  s->congestion_adapted = 0;
  atomic_store_explicit(&s->in_use, 1, memory_order_relaxed);
  seqlock_commit_write(&s->gen, g);

  sockaddr_index_remove(st->index, addr, addrlen); /* drop stale entry before insert */
  sockaddr_index_insert(st->index, addr, addrlen, idx);
  return s;
}

burst_slot_t *burst_table_claim(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int fd, burst_t *b) {
  burst_stripe_t *st = pick_stripe(t, addr, addrlen);
  burst_slot_t *s;
  pthread_mutex_lock(&st->lock);
  s = claim_locked(t, st, addr, addrlen, fd, b);
  pthread_mutex_unlock(&st->lock);
  if (!s)
    log_line(TOOL_NAME ": max-bursts (%zu) reached, rejecting new burst session", t->cap);
  return s;
}

burst_slot_t *burst_table_find(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen) {
  burst_stripe_t *st = pick_stripe(t, addr, addrlen);
  size_t idx;
  pthread_mutex_lock(&st->lock);
  idx = find_valid(t, st, addr, addrlen);
  pthread_mutex_unlock(&st->lock);
  return idx == SIZE_MAX ? NULL : &t->slots[idx];
}

int burst_table_start(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int fd, burst_t *b, uint8_t *msn_out) {
  burst_stripe_t *st = pick_stripe(t, addr, addrlen);
  size_t i;
  burst_t *old = NULL;
  int updated = 0;

  pthread_mutex_lock(&st->lock);
  i = find_valid(t, st, addr, addrlen);
  if (i != SIZE_MAX) {
    burst_slot_t *s = &t->slots[i];
    unsigned g = seqlock_begin_write(&s->gen);
    old = atomic_load_explicit(&s->b, memory_order_relaxed);
    atomic_store_explicit(&s->fd, fd, memory_order_relaxed);
    atomic_store_explicit(&s->b, b, memory_order_relaxed);
    *msn_out = ++s->msn;
    seqlock_commit_write(&s->gen, g);
    updated = 1;
  } else {
    const burst_slot_t *s = claim_locked(t, st, addr, addrlen, fd, b);
    if (s) {
      *msn_out = 0;
      pthread_mutex_unlock(&st->lock);
      return 0;
    }
  }
  pthread_mutex_unlock(&st->lock);

  if (updated) {
    burst_terminate(old, 0, 0);
    burst_release(old);
    return 1;
  }

  log_line(TOOL_NAME ": max-bursts (%zu) reached, rejecting new burst session", t->cap);
  return -1;
}

int burst_table_note_nack(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, unsigned threshold, burst_table_nack_result_t *out) {
  burst_stripe_t *st = pick_stripe(t, addr, addrlen);
  size_t idx;

  memset(out, 0, sizeof *out);
  pthread_mutex_lock(&st->lock);
  idx = find_valid(t, st, addr, addrlen);
  if (idx == SIZE_MAX) {
    pthread_mutex_unlock(&st->lock);
    return 0;
  }

  {
    burst_slot_t *slot = &t->slots[idx];
    burst_t *b = atomic_load_explicit(&slot->b, memory_order_relaxed);
    unsigned adapt_at;

    slot->nack_count++;
    atomic_fetch_add_explicit(&t->nacks_total, 1, memory_order_relaxed);
    if (threshold == 0) {
      pthread_mutex_unlock(&st->lock);
      return 1;
    }

    adapt_at = threshold / 2;
    if (slot->nack_count >= threshold) {
      burst_terminate(b, 0, 0);
      out->action = BURST_TABLE_NACK_TERMINATED;
    } else if (!slot->congestion_adapted && adapt_at > 0 && slot->nack_count >= adapt_at) {
      out->new_bps = atomic_load_explicit(&b->target_bps, memory_order_relaxed) * 0.5;
      atomic_store_explicit(&b->target_bps, out->new_bps, memory_order_relaxed);
      slot->congestion_adapted = 1;
      atomic_fetch_add_explicit(&t->congestion_adaptations_total, 1, memory_order_relaxed);
      out->action = BURST_TABLE_NACK_ADAPTED;
      out->msn = ++slot->msn;
    }
  }
  pthread_mutex_unlock(&st->lock);
  return 1;
}

int burst_table_terminate(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int has_stop_seq, uint32_t stop_seqnum) {
  burst_stripe_t *st = pick_stripe(t, addr, addrlen);
  size_t idx;
  pthread_mutex_lock(&st->lock);
  idx = find_valid(t, st, addr, addrlen);
  if (idx != SIZE_MAX)
    burst_terminate(atomic_load_explicit(&t->slots[idx].b, memory_order_relaxed), has_stop_seq, stop_seqnum);
  pthread_mutex_unlock(&st->lock);
  return idx != SIZE_MAX;
}

int burst_table_release(burst_table_t *t, size_t idx, const burst_t *expect_b, int should_clear, int *did_clear) {
  burst_stripe_t *st = stripe_of_idx(t, idx);
  burst_slot_t *slot = &t->slots[idx];
  int owns_slot = 0;

  *did_clear = 0;
  pthread_mutex_lock(&st->lock);
  if (atomic_load_explicit(&slot->in_use, memory_order_relaxed) && atomic_load_explicit(&slot->b, memory_order_relaxed) == expect_b) {
    owns_slot = 1;
    if (should_clear) {
      struct sockaddr_storage sa;
      socklen_t sl = atomic_load_explicit(&slot->addrlen, memory_order_relaxed);
      unsigned g = seqlock_begin_write(&slot->gen);

      atomic_store_explicit(&slot->b, NULL, memory_order_relaxed);
      atomic_store_explicit(&slot->in_use, 0, memory_order_relaxed);
      seqlock_commit_write(&slot->gen, g);

      slot_read_addr(slot, &sa);
      sockaddr_index_remove(st->index, (const struct sockaddr *)&sa, sl);
      st->free_list[st->free_count++] = idx;
      *did_clear = 1;
    }
  }
  pthread_mutex_unlock(&st->lock);
  return owns_slot;
}

void burst_table_note_bytes_sent(burst_table_t *t, uint64_t bytes) {
  atomic_fetch_add_explicit(&t->bytes_retransmitted_total, bytes, memory_order_relaxed);
}

void burst_table_get_metrics(const burst_table_t *t, burst_table_metrics_t *out) {
  out->bursts_active = 0;
  for (size_t i = 0; i < t->cap; i++)
    if (atomic_load_explicit(&t->slots[i].in_use, memory_order_relaxed))
      out->bursts_active++;
  out->bytes_retransmitted_total = atomic_load_explicit(&t->bytes_retransmitted_total, memory_order_relaxed);
  out->nacks_total = atomic_load_explicit(&t->nacks_total, memory_order_relaxed);
  out->congestion_adaptations_total = atomic_load_explicit(&t->congestion_adaptations_total, memory_order_relaxed);
}
