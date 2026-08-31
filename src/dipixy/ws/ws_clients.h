/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_WS_CLIENTS_H
#define DIPIXY_WS_CLIENTS_H

#include <stddef.h>

#include "../core/route.h"
#include "../ts/pidfilter.h"

typedef struct {
  const char *ip;
  int http_ver; /* 1, 2, or 3 */
  route_fmt_t fmt;
  unsigned pmt_pid;
  const pid_filter_t *filter; /* NULL or count 0: no filter */
  const char *src_proto;      /* "rtp","udp","srt","rist","stdin","sds","m3u","xspf","csv","xml","http" */
  const char *src_addr;
  int src_ordinal;            /* 0: n/a (direct rtp/udp/srt) */
  const char *src_name;       /* NULL/empty: unnamed */
  unsigned item_num;          /* 0: n/a */
  const char *item_name;      /* NULL/empty: n/a or unnamed */
} client_info_t;

/* array sized to max_clients: doubles as the max-clients cap on distinct clients */
void ws_clients_init(int max_clients);

/* ts-push: registers for connection lifetime. >=0 handle, -1 at cap */
int ws_clients_add_persistent(const client_info_t *info);
void ws_clients_remove(int handle); /* handle<0: no-op */

/* hls/dash/llhls: finds-or-creates session matching info, refreshes it.
   >=0 handle, -1 = new session needed, at cap */
int ws_clients_touch(const client_info_t *info);

void ws_clients_add_bytes(int handle, size_t n); /* handle<0: no-op */

/* pump thread, ~1/sec */
void ws_clients_tick(void);

/* clients.snapshot json, thread-local, valid until next call, no free. 0 ok, -1 OOM */
int ws_clients_build_snapshot(char **out);

#endif
