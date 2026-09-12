/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include "ws_dispatch.h"

#include "internal.h"
#include "reactor_tls.h"
#include "../ws/ws_frame.h"
#include "../ws/ws_sources.h"

#include <string.h>

void ws_dispatch_frame(void *ctx, ws_dispatch_queue_fn queue, int opcode, const uint8_t *payload, size_t plen) {
  char *json;
  switch (opcode) {
    case WS_OP_PING:
      queue(ctx, WS_OP_PONG, payload, plen);
      break;
    case WS_OP_CLOSE:
      queue(ctx, WS_OP_CLOSE, payload, plen <= 125 ? plen : 0);
      break;
    case WS_OP_TEXT:
      if (memmem(payload, plen, "\"playlists.reload\"", 18)) {
        reactor_reload_channels();
        return;
      }
      if (memmem(payload, plen, "\"tls.reload\"", 12)) {
        const char *resp = reload_tls() == 0 ? "{\"type\":\"tls.reload\",\"ok\":true}" : "{\"type\":\"tls.reload\",\"ok\":false}";
        queue(ctx, WS_OP_TEXT, resp, strlen(resp));
        return;
      }
      if (memmem(payload, plen, "\"clients.get\"", 13)) {
        if (!ws_clients_build_snapshot(&json)) queue(ctx, WS_OP_TEXT, json, strlen(json));
        return;
      }
      if (!memmem(payload, plen, "\"sources.get\"", 13)) return;
      if (ws_sources_build_snapshot(reactor_cfg(), reactor_channels(), &json)) return;
      queue(ctx, WS_OP_TEXT, json, strlen(json));
      break;
    default:
      break;
  }
}
