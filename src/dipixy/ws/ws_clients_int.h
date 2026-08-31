/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_WS_CLIENTS_INT_H
#define DIPIXY_WS_CLIENTS_INT_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

#include "lib/helper/jsonbuf.h"

#include "ws_clients.h"

typedef struct {
  int used;
  int persistent;
  char ip[64];
  int http_ver;
  time_t connect_time;
  time_t last_seen; /* pull sessions only */
  route_fmt_t fmt;
  unsigned pmt_pid;
  char filter[128];
  char src_proto[16];
  char src_addr[80];
  int src_ordinal;
  char src_name[64];
  unsigned item_num;
  char item_name[128];
  _Atomic uint64_t bytes_total;
  uint64_t bytes_prev; /* tick-thread-only */
  _Atomic unsigned gen; /* bumped on every claim/remove: see pack_handle() */
} ws_client_t;

typedef struct {
  char ip[64];
  int http_ver;
  time_t connect_time;
  route_fmt_t fmt;
  unsigned pmt_pid;
  char filter[128];
  char src_proto[16];
  char src_addr[80];
  int src_ordinal;
  char src_name[64];
  unsigned item_num;
  char item_name[128];
} ws_client_snapshot_t;

typedef struct {
  int id;
  double mbps;
} tick_rate_t;

/* ws_clients.c */
extern ws_client_t *g_clients;
extern int g_clients_cap;
extern pthread_mutex_t g_clients_mtx;
extern int *g_free_slots;
extern int g_free_slots_n;
void hash_delete(int idx);

/* ws_clients_tick.c, allocated by ws_clients_init() in ws_clients.c */
extern int *g_expired_scratch;
extern tick_rate_t *g_tick_rate_scratch;

/* ws_clients_json.c */
void snapshot_client(ws_client_snapshot_t *dst, const ws_client_t *src);
void emit_client_json(jbuf_t *j, int id, const ws_client_snapshot_t *e);
void publish_client_event(const char *type, int id);
void jbuf_i64(jbuf_t *j, long long v);
void jbuf_fixed3(jbuf_t *j, double v);

#endif
