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
  if (!t->slots) {
    free(t);
    return NULL;
  }
  t->cap = cap;
  pthread_mutex_init(&t->lock, NULL);
  return t;
}

void burst_table_free(burst_table_t *t) {
  size_t i;
  if (!t)
    return;
  for (i = 0; i < t->cap; i++)
    if (t->slots[i].in_use)
      burst_release(t->slots[i].b);
  pthread_mutex_destroy(&t->lock);
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

burst_slot_t *burst_table_claim(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int fd, burst_t *b) {
  size_t i;
  pthread_mutex_lock(&t->lock);
  for (i = 0; i < t->cap; i++) {
    if (!t->slots[i].in_use) {
      memcpy(&t->slots[i].addr, addr, addrlen);
      t->slots[i].addrlen = addrlen;
      t->slots[i].fd = fd;
      t->slots[i].b = b;
      t->slots[i].msn = 0;
      t->slots[i].nack_count = 0;
      t->slots[i].congestion_adapted = 0;
      t->slots[i].in_use = 1;
      pthread_mutex_unlock(&t->lock);
      return &t->slots[i];
    }
  }
  pthread_mutex_unlock(&t->lock);
  log_line(TOOL_NAME ": max-bursts (%zu) reached, rejecting new burst session", t->cap);
  return NULL;
}

burst_slot_t *burst_table_find(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen) {
  size_t i;
  burst_slot_t *found = NULL;
  pthread_mutex_lock(&t->lock);
  for (i = 0; i < t->cap; i++) {
    if (t->slots[i].in_use && sockaddr_eq(&t->slots[i].addr, t->slots[i].addrlen, addr, addrlen)) {
      found = &t->slots[i];
      break;
    }
  }
  pthread_mutex_unlock(&t->lock);
  return found;
}

int burst_table_start(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int fd, burst_t *b, uint8_t *msn_out) {
  size_t i;
  burst_t *old = NULL;
  int updated = 0;

  pthread_mutex_lock(&t->lock);
  for (i = 0; i < t->cap; i++) {
    if (t->slots[i].in_use && sockaddr_eq(&t->slots[i].addr, t->slots[i].addrlen, addr, addrlen)) {
      old = t->slots[i].b;
      t->slots[i].fd = fd;
      t->slots[i].b = b;
      *msn_out = ++t->slots[i].msn;
      updated = 1;
      break;
    }
  }
  if (!updated) {
    for (i = 0; i < t->cap; i++) {
      if (!t->slots[i].in_use) {
        memcpy(&t->slots[i].addr, addr, addrlen);
        t->slots[i].addrlen = addrlen;
        t->slots[i].fd = fd;
        t->slots[i].b = b;
        t->slots[i].msn = 0;
        t->slots[i].nack_count = 0;
        t->slots[i].congestion_adapted = 0;
        t->slots[i].in_use = 1;
        *msn_out = 0;
        pthread_mutex_unlock(&t->lock);
        return 0;
      }
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
  size_t i;

  memset(out, 0, sizeof *out);
  pthread_mutex_lock(&t->lock);
  for (i = 0; i < t->cap; i++) {
    burst_slot_t *slot = &t->slots[i];
    unsigned adapt_at;

    if (!slot->in_use || !sockaddr_eq(&slot->addr, slot->addrlen, addr, addrlen))
      continue;

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
    pthread_mutex_unlock(&t->lock);
    return 1;
  }
  pthread_mutex_unlock(&t->lock);
  return 0;
}

int burst_table_terminate(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int has_stop_seq, uint32_t stop_seqnum) {
  size_t i;
  pthread_mutex_lock(&t->lock);
  for (i = 0; i < t->cap; i++) {
    if (t->slots[i].in_use && sockaddr_eq(&t->slots[i].addr, t->slots[i].addrlen, addr, addrlen)) {
      burst_terminate(t->slots[i].b, has_stop_seq, stop_seqnum);
      pthread_mutex_unlock(&t->lock);
      return 1;
    }
  }
  pthread_mutex_unlock(&t->lock);
  return 0;
}
