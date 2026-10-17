/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/dash/lldash.h"
#include "dipixy/dash/dash_int.h"
#include "dipixy/altsvc.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/version.h"
#include "dipixy/ws/ws_clients.h"
#include "lib/helper/byte_ring.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "dipixy/dash/lldash/lldash_int.h"

size_t dash_lldash_ring_read(int slot, uint8_t *buf, size_t maxlen) {
  if (slot < 0 || slot >= g_lldash_subs_n) return 0;
  return byte_ring_read(&g_lldash_subs[slot].ring, buf, maxlen);
}

int dash_lldash_ring_pending(int slot) {
  dashchunk_sub_t *s;
  if (slot < 0 || slot >= g_lldash_subs_n) return 0;
  s = &g_lldash_subs[slot];
  return byte_ring_unsent(&s->ring) != 0;
}

int dash_lldash_ring_errored(int slot) {
  if (slot < 0 || slot >= g_lldash_subs_n) return 0;
  return atomic_load_explicit(&g_lldash_subs[slot].ring_errored, memory_order_acquire);
}

const uint8_t *dash_lldash_ring_peek(int slot, size_t *len) {
  if (slot < 0 || slot >= g_lldash_subs_n) {
    *len = 0;
    return NULL;
  }
  return byte_ring_peek(&g_lldash_subs[slot].ring, len);
}

void dash_lldash_ring_hold(int slot, size_t n) {
  if (slot < 0 || slot >= g_lldash_subs_n) return;
  byte_ring_hold(&g_lldash_subs[slot].ring, n);
}

void dash_lldash_ring_ack(int slot, size_t n) {
  if (slot < 0 || slot >= g_lldash_subs_n) return;
  byte_ring_ack(&g_lldash_subs[slot].ring, n);
}
