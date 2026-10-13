/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "route_common.h"

route_setup_status_t route_setup(const route_t *rt, unsigned *list_num, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *client_ip, int http_ver, route_item_bufs_t *item_bufs,
    client_info_t *cinfo, double seg_target, int max_segs, seg_container_t container, double part_target, route_setup_t *out) {
  switch (open_source_try(rt, list_num, &out->ctx, &out->job)) {
    case OPEN_RES_OK:
      break;
    case OPEN_RES_PENDING:
      return ROUTE_SETUP_PENDING;
    case OPEN_RES_FAIL:
      return ROUTE_SETUP_404;
  }
  route_client_info(rt, *list_num, filter, pmt_pid, client_ip, http_ver, item_bufs, cinfo);
  out->ws_handle = ws_clients_touch(cinfo);
  if (out->ws_handle < 0) {
    capture_close(out->ctx);
    return ROUTE_SETUP_503;
  }
  if (!hls_seg_touch(out->ctx, filter, pmt_pid, lcevc, seg_target, max_segs, container, part_target)) return ROUTE_SETUP_503;
  return ROUTE_SETUP_OK;
}
