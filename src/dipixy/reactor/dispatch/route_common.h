/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DISPATCH_ROUTE_COMMON_H
#define DIPIXY_DISPATCH_ROUTE_COMMON_H

#include "../internal.h"
#include "../../segment/segment.h"
#include "../../ws/ws_clients.h"

typedef enum { HTTP_METHOD_GET, HTTP_METHOD_HEAD, HTTP_METHOD_POST, HTTP_METHOD_SUBSCRIBE, HTTP_METHOD_UNSUBSCRIBE, HTTP_METHOD_CONNECT, HTTP_METHOD_OTHER } http_method_t;
http_method_t parse_http_method(const char *method, size_t method_len);

typedef enum { ROUTE_RESULT_FINISH, ROUTE_RESULT_PARKED, ROUTE_RESULT_OPENING } route_result_t;

typedef struct {
  void (*status)(void *t, int code);
  int (*park_open)(void *t, capture_open_job_t *job);
  int (*cold_park)(void *t, const hls_cold_park_req_t *pr);
  int (*ll_park)(void *t, const llhls_park_req_t *pr);
  size_t (*hls)(void *t, const stream_key_t *k, const hls_store_t *st, const route_file_t *f, int ll);
  size_t (*dash)(void *t, const stream_key_t *k, const hls_store_t *st, int want_ll, const char *utc_url);
  size_t (*dash_seg)(void *t, const stream_key_t *k, const route_file_t *f);
  int (*lldash)(void *t, const stream_key_t *k, const route_file_t *f, int ws_handle);
  void (*mp4)(void *t, const stream_key_t *k, int ws_handle);
} route_io_ops_t;

typedef struct {
  const route_io_ops_t *ops;
  void *t;
  const route_t *rt;
  const pid_filter_t *filter;
  unsigned pmt_pid;
  const lcevc_select_t *lcevc;
  const char *client_ip;
  int http_ver;
  int is_head;
  int keep_alive;
  const char *origin;
  const char *park_inm;
  const char *query;
  const char *host;
  int tls;
  route_item_bufs_t *item_bufs;
  client_info_t *cinfo;
} route_io_t;

route_result_t route_dispatch_store(const route_io_t *io);

#endif
