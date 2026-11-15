/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdatomic.h>
#include <unistd.h>

#include "dipixy/dash/lldash.h"
#include "dipixy/dash/lldash/lldash_int.h"

void dash_lldash_h2_bind(int slot, void *h2c, void *h2_slot, int reactor_tid, int ws_handle) {
  if (slot < 0 || slot >= g_lldash_subs_n) return;
  g_lldash_subs[slot].h2c = h2c;
  g_lldash_subs[slot].h2_slot = h2_slot;
  g_lldash_subs[slot].h.ws_handle = ws_handle;
  subtab_link(&g_lldash_tab, slot, reactor_tid);
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
  g_lldash_subs[slot].h.ws_handle = ws_handle;
  subtab_link(&g_lldash_tab, slot, reactor_tid);
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
