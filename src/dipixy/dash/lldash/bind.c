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

void dash_lldash_h2_bind(int slot, void *h2c, void *h2_slot, int reactor_tid, int ws_handle) {
  if (slot < 0 || slot >= g_lldash_subs_n) return;
  g_lldash_subs[slot].h2c = h2c;
  g_lldash_subs[slot].h2_slot = h2_slot;
  g_lldash_subs[slot].reactor_tid = reactor_tid;
  g_lldash_subs[slot].ws_handle = ws_handle;
  link_tid_chain(slot, reactor_tid);
  sub_attach(slot);
}

void *dash_lldash_sub_h2c(int slot) {
  if (slot < 0 || slot >= g_lldash_subs_n) return NULL;
  return g_lldash_subs[slot].h2c;
}

void *dash_lldash_sub_h2_slot(int slot) {
  if (slot < 0 || slot >= g_lldash_subs_n) return NULL;
  return g_lldash_subs[slot].h2_slot;
}

void dash_lldash_h3_bind(int slot, void *h3c, int64_t h3_sid, int reactor_tid, int ws_handle) {
  if (slot < 0 || slot >= g_lldash_subs_n) return;
  g_lldash_subs[slot].h3c = h3c;
  g_lldash_subs[slot].h3_sid = h3_sid;
  g_lldash_subs[slot].reactor_tid = reactor_tid;
  g_lldash_subs[slot].ws_handle = ws_handle;
  link_tid_chain(slot, reactor_tid);
  sub_attach(slot);
}

void *dash_lldash_sub_h3c(int slot) {
  if (slot < 0 || slot >= g_lldash_subs_n) return NULL;
  return g_lldash_subs[slot].h3c;
}

int64_t dash_lldash_sub_h3_sid(int slot) {
  if (slot < 0 || slot >= g_lldash_subs_n) return -1;
  return g_lldash_subs[slot].h3_sid;
}

void dash_lldash_register_reactor_efd(int tid, int efd) {
  if (tid < 0 || tid >= DASHCHUNK_MAX_REACTOR_THREADS) return;
  __atomic_store_n(&g_lldash_reactor_efds[tid], efd, __ATOMIC_RELAXED);
}

void dash_lldash_close_reactor_efds(void) {
  for (int i = 0; i < DASHCHUNK_MAX_REACTOR_THREADS; i++) {
    int efd = __atomic_exchange_n(&g_lldash_reactor_efds[i], -1, __ATOMIC_RELAXED);
    if (efd >= 0) close(efd);
  }
}
