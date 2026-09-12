/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DISPATCH_ROUTE_COMMON_H
#define DIPIXY_DISPATCH_ROUTE_COMMON_H

#include "../internal.h"
#include "../../segment/segment.h"
#include "../../ws/ws_clients.h"

typedef enum { ROUTE_SETUP_OK, ROUTE_SETUP_404, ROUTE_SETUP_501 } route_setup_status_t;

typedef struct {
  capture_ctx_t *ctx;
  int ws_handle;
} route_setup_t;

/* open_source+route_client_info+ws_clients_touch+hls_seg_touch. ctx already closed on !OK */
route_setup_status_t route_setup(const route_t *rt, unsigned *list_num, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *client_ip, int http_ver, route_item_bufs_t *item_bufs,
                                 client_info_t *cinfo, double seg_target, int max_segs, seg_container_t container, double part_target, route_setup_t *out);

#endif
