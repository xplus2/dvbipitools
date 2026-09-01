/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ws_clients_int.h"

#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"

ws_client_t *g_clients;
int g_clients_cap;
pthread_mutex_t g_clients_mtx = PTHREAD_MUTEX_INITIALIZER;

/* g_clients index for touch()'s match. persistent entries excluded.
   speaking of tombstones, Béla Lugosi was Dracula's best acting credit. */
#define WS_HASH_EMPTY (-1)
#define WS_HASH_TOMB (-2)

static int *g_hash;
static uint32_t g_hash_cap, g_hash_mask;

int *g_free_slots;
int g_free_slots_n;

static uint32_t fnv1a_mix(uint32_t h, const void *data, size_t len) {
  const unsigned char *p = data;
  for (size_t i = 0; i < len; i++)
    h = (h ^ p[i]) * 16777619u;
  return h;
}

static uint32_t client_hash(const char *ip, route_fmt_t fmt, unsigned pmt_pid, const char *filt, const char *src_proto, const char *src_addr,
                            int src_ordinal, const char *src_name, unsigned item_num, const char *item_name) {
  uint32_t h = 2166136261u;
  h = fnv1a_mix(h, ip, strlen(ip));
  h = fnv1a_mix(h, &fmt, sizeof fmt);
  h = fnv1a_mix(h, &pmt_pid, sizeof pmt_pid);
  h = fnv1a_mix(h, filt, strlen(filt));
  h = fnv1a_mix(h, src_proto, strlen(src_proto));
  h = fnv1a_mix(h, src_addr, strlen(src_addr));
  h = fnv1a_mix(h, &src_ordinal, sizeof src_ordinal);
  h = fnv1a_mix(h, src_name, strlen(src_name));
  h = fnv1a_mix(h, &item_num, sizeof item_num);
  h = fnv1a_mix(h, item_name, strlen(item_name));
  return h;
}

/* caller holds g_clients_mtx */
static void hash_insert(uint32_t h, int idx) {
  uint32_t i = h & g_hash_mask;
  for (uint32_t n = 0; n < g_hash_cap; n++, i = (i + 1) & g_hash_mask) {
    if (g_hash[i] == WS_HASH_EMPTY || g_hash[i] == WS_HASH_TOMB) {
      g_hash[i] = idx;
      return;
    }
  }
}

/* caller holds g_clients_mtx. reads e's fields before they're cleared */
void hash_delete(int idx) {
  const ws_client_t *e = &g_clients[idx];
  uint32_t h = client_hash(e->ip, e->fmt, e->pmt_pid, e->filter, e->src_proto, e->src_addr, e->src_ordinal, e->src_name, e->item_num, e->item_name);
  uint32_t i = h & g_hash_mask;
  for (uint32_t n = 0; n < g_hash_cap; n++, i = (i + 1) & g_hash_mask) {
    if (g_hash[i] == WS_HASH_EMPTY)
      return;
    if (g_hash[i] == idx) {
      g_hash[i] = WS_HASH_TOMB;
      return;
    }
  }
}

/* handle = slot index + gen. stale handle must not hit a slot idle-eviction gave to new clients */
#define WS_HANDLE_IDX_BITS 20
#define WS_HANDLE_GEN_BITS 11
#define WS_HANDLE_IDX_MASK ((1 << WS_HANDLE_IDX_BITS) - 1)
#define WS_HANDLE_GEN_MASK ((1u << WS_HANDLE_GEN_BITS) - 1)

static int pack_handle(int idx, unsigned gen) {
  return idx | (int)((gen & WS_HANDLE_GEN_MASK) << WS_HANDLE_IDX_BITS);
}

static int handle_idx(int handle) { return handle & WS_HANDLE_IDX_MASK; }

static unsigned handle_gen(int handle) { return ((unsigned)handle >> WS_HANDLE_IDX_BITS) & WS_HANDLE_GEN_MASK; }

/* caller holds g_clients_mtx */
static unsigned claim_slot(int idx) {
  unsigned new_gen = atomic_load_explicit(&g_clients[idx].gen, memory_order_relaxed) + 1;
  memset(&g_clients[idx], 0, sizeof g_clients[idx]);
  atomic_store_explicit(&g_clients[idx].gen, new_gen, memory_order_release);
  return new_gen;
}

void ws_clients_init(int max_clients) {
  uint32_t i;
  g_clients = calloc((size_t)max_clients, sizeof *g_clients);
  g_clients_cap = g_clients ? max_clients : 0;
  if (!g_clients_cap) {
    g_hash = NULL;
    g_hash_cap = g_hash_mask = 0;
    return;
  }
  g_hash_cap = (uint32_t)next_pow2((size_t)g_clients_cap * 2u); /* ~50% load factor */
  g_hash_mask = g_hash_cap - 1;
  g_hash = malloc(g_hash_cap * sizeof *g_hash);
  if (!g_hash) {
    g_hash_cap = g_hash_mask = 0;
    return;
  }
  for (i = 0; i < g_hash_cap; i++)
    g_hash[i] = WS_HASH_EMPTY;
  g_expired_scratch = malloc(sizeof(int) * (size_t)g_clients_cap);
  g_tick_rate_scratch = malloc(sizeof(tick_rate_t) * (size_t)g_clients_cap);
  g_free_slots = malloc(sizeof(int) * (size_t)g_clients_cap);
  if (g_free_slots) {
    for (i = 0; i < (uint32_t)g_clients_cap; i++)
      g_free_slots[i] = (int)i;
    g_free_slots_n = g_clients_cap;
  }
}

static void fill_entry(ws_client_t *e, const client_info_t *info, const char *filt) {
  bufcpy(e->ip, sizeof e->ip, info->ip ? info->ip : "");
  e->http_ver = info->http_ver;
  e->fmt = info->fmt;
  e->pmt_pid = info->pmt_pid;
  bufcpy(e->filter, sizeof e->filter, filt);
  bufcpy(e->src_proto, sizeof e->src_proto, info->src_proto ? info->src_proto : "");
  bufcpy(e->src_addr, sizeof e->src_addr, info->src_addr ? info->src_addr : "");
  e->src_ordinal = info->src_ordinal;
  bufcpy(e->src_name, sizeof e->src_name, info->src_name ? info->src_name : "");
  e->item_num = info->item_num;
  bufcpy(e->item_name, sizeof e->item_name, info->item_name ? info->item_name : "");
}

int ws_clients_add_persistent(const client_info_t *info) {
  char filt[128];
  int i;
  unsigned gen;
  if (info->filter)
    pid_filter_format(info->filter, filt, sizeof filt);
  else
    filt[0] = '\0';
  pthread_mutex_lock(&g_clients_mtx);
  if (g_free_slots_n == 0) {
    pthread_mutex_unlock(&g_clients_mtx);
    return -1;
  }
  i = g_free_slots[--g_free_slots_n];
  gen = claim_slot(i);
  fill_entry(&g_clients[i], info, filt);
  g_clients[i].persistent = 1;
  g_clients[i].connect_time = time(NULL);
  g_clients[i].used = 1;
  pthread_mutex_unlock(&g_clients_mtx);
  publish_client_event("clients.add", i);
  return pack_handle(i, gen);
}

void ws_clients_remove(int handle) {
  int idx = handle_idx(handle);
  int removed;
  if (handle < 0 || idx >= g_clients_cap)
    return;
  pthread_mutex_lock(&g_clients_mtx);
  removed = atomic_load_explicit(&g_clients[idx].gen, memory_order_relaxed) == handle_gen(handle);
  if (removed) {
    if (!g_clients[idx].persistent)
      hash_delete(idx);
    g_clients[idx].used = 0;
    atomic_fetch_add_explicit(&g_clients[idx].gen, 1, memory_order_release);
    if (g_free_slots)
      g_free_slots[g_free_slots_n++] = idx;
  }
  pthread_mutex_unlock(&g_clients_mtx);
  if (removed)
    publish_client_event("clients.remove", idx);
}

int ws_clients_touch(const client_info_t *info) {
  char filt[128];
  const char *ip, *src_proto, *src_addr, *src_name, *item_name;
  int free_slot;
  time_t now = time(NULL);
  uint32_t h, i;
  if (info->filter)
    pid_filter_format(info->filter, filt, sizeof filt);
  else
    filt[0] = '\0';
  ip = info->ip ? info->ip : "";
  src_proto = info->src_proto ? info->src_proto : "";
  src_addr = info->src_addr ? info->src_addr : "";
  src_name = info->src_name ? info->src_name : "";
  item_name = info->item_name ? info->item_name : "";
  h = client_hash(ip, info->fmt, info->pmt_pid, filt, src_proto, src_addr, info->src_ordinal, src_name,
                   info->item_num, item_name);

  pthread_mutex_lock(&g_clients_mtx);
  i = h & g_hash_mask;
  for (uint32_t n = 0; n < g_hash_cap; n++, i = (i + 1) & g_hash_mask) {
    ws_client_t *e;
    int idx = g_hash[i];
    if (idx == WS_HASH_EMPTY)
      break;
    if (idx == WS_HASH_TOMB)
      continue;
    e = &g_clients[idx];
    if (!e->used || e->persistent)
      continue;
    if (strcmp(e->ip, ip) || e->fmt != info->fmt || e->pmt_pid != info->pmt_pid)
      continue;
    if (strcmp(e->filter, filt))
      continue;
    if (strcmp(e->src_proto, src_proto) || e->src_ordinal != info->src_ordinal)
      continue;
    if (strcmp(e->src_addr, src_addr))
      continue;
    if (strcmp(e->src_name, src_name) || e->item_num != info->item_num)
      continue;
    if (strcmp(e->item_name, item_name))
      continue;
    e->last_seen = now;
    {
      unsigned gen = atomic_load_explicit(&e->gen, memory_order_relaxed);
      pthread_mutex_unlock(&g_clients_mtx);
      return pack_handle(idx, gen);
    }
  }

  if (g_free_slots_n == 0) {
    pthread_mutex_unlock(&g_clients_mtx);
    return -1;
  }
  free_slot = g_free_slots[--g_free_slots_n];
  {
    unsigned gen = claim_slot(free_slot);
    fill_entry(&g_clients[free_slot], info, filt);
    g_clients[free_slot].connect_time = g_clients[free_slot].last_seen = now;
    g_clients[free_slot].used = 1;
    hash_insert(h, free_slot);
    pthread_mutex_unlock(&g_clients_mtx);
    publish_client_event("clients.add", free_slot);
    return pack_handle(free_slot, gen);
  }
}

void ws_clients_add_bytes(int handle, size_t n) {
  int idx = handle_idx(handle);
  if (handle < 0 || idx >= g_clients_cap) return;
  if (atomic_load_explicit(&g_clients[idx].gen, memory_order_acquire) != handle_gen(handle)) return;
  atomic_fetch_add_explicit(&g_clients[idx].bytes_total, n, memory_order_relaxed);
}
