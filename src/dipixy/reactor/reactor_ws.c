/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include "internal.h"
#include "reactor_tls.h"
#include "../ws/ws_broadcast.h"
#include "../ws/ws_frame.h"
#include "../ws/ws_sources.h"
#include "lib/helper/base64.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/sha1.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"  /* RFC6455 */

typedef struct {
  ws_parser_t parser;
  uint8_t *out_buf;
  size_t out_cap;
} ws_conn_state_t;

static void ws_sink(void *ctx, const uint8_t *frame, size_t flen) {
  conn_send_buffered((conn_t *)ctx, frame, flen, NULL, 0);
}

static void send_frame(int epfd, conn_t *c, int opcode, const void *payload, size_t len) {
  ws_conn_state_t *ws = c->ws;
  size_t flen = ws_frame_hdr_len(len) + len;
  if (flen > ws->out_cap) {
    uint8_t *p = realloc(ws->out_buf, flen);
    if (!p)
      return;
    ws->out_buf = p;
    ws->out_cap = flen;
  }
  ws_frame_encode(ws->out_buf, opcode, payload, len);
  conn_queue(c, ws->out_buf, flen);
  reactor_ws_flush(epfd, c);
}

#define WS_CONN_POOL_MAX 64

static _Thread_local ws_conn_state_t *t_ws_pool[WS_CONN_POOL_MAX];
static _Thread_local int t_ws_pool_n;

void reactor_ws_begin(int epfd, conn_t *c) {
  ws_conn_state_t *ws;
  if (t_ws_pool_n > 0) {
    ws = t_ws_pool[--t_ws_pool_n];
  } else {
    ws = malloc(sizeof *ws);
    if (!ws) {
      reactor_ws_close(epfd, c);
      return;
    }
  }
  ws_parser_init(&ws->parser);
  ws->out_buf = NULL;
  ws->out_cap = 0;
  c->ws = ws;
  c->in.len = c->in.off = 0;
  c->become_ws = 0;
  c->close_after_flush = 0;
  c->state = CONN_WS;
  c->epfd = epfd;
  c->reactor_tid = t_reactor_tid;
  reactor_arm(epfd, c, 0);
  conn_publish(c);
  ws_broadcast_register(ws_sink, c);
}

void reactor_ws_close(int epfd, conn_t *c) {
  if (!conn_claim_teardown(c))
    return;
  ws_broadcast_unregister(ws_sink, c);
  conn_unpublish(c);
  epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
  if (c->ws) {
    ws_conn_state_t *ws = c->ws;
    ws_parser_free(&ws->parser);
    free(ws->out_buf);
    ws->out_buf = NULL;
    ws->out_cap = 0;
    if (t_ws_pool_n < WS_CONN_POOL_MAX)
      t_ws_pool[t_ws_pool_n++] = ws;
    else
      free(ws);
    c->ws = NULL;
  }
  tls_close_fd(c->fd);
  conn_free(c);
}

void reactor_ws_flush(int epfd, conn_t *c) {
  int rc, caf;
  pthread_mutex_lock(&c->out_lock);
  rc = c->dead ? CONN_FLUSH_ERROR : conn_flush(c, epfd);
  caf = c->close_after_flush;
  pthread_mutex_unlock(&c->out_lock);
  if (rc == CONN_FLUSH_ERROR || (rc == CONN_FLUSH_DONE && caf))
    reactor_ws_close(epfd, c);
}

static void handle_text(int epfd, conn_t *c, const char *msg, size_t len) {
  char *json;
  if (memmem(msg, len, "\"playlists.reload\"", 18)) {
    reactor_reload_channels();
    return;
  }
  if (memmem(msg, len, "\"tls.reload\"", 12)) {
    const char *resp = reload_tls() == 0 ? "{\"type\":\"tls.reload\",\"ok\":true}" : "{\"type\":\"tls.reload\",\"ok\":false}";
    send_frame(epfd, c, WS_OP_TEXT, resp, strlen(resp));
    return;
  }
  if (memmem(msg, len, "\"clients.get\"", 13)) {
    if (!ws_clients_build_snapshot(&json))
      send_frame(epfd, c, WS_OP_TEXT, json, strlen(json));
    return;
  }
  if (!memmem(msg, len, "\"sources.get\"", 13))
    return;
  if (ws_sources_build_snapshot(reactor_cfg(), reactor_channels(), &json))
    return;
  send_frame(epfd, c, WS_OP_TEXT, json, strlen(json));
}

void reactor_ws_readable(int epfd, conn_t *c) {
  ws_conn_state_t *ws = c->ws;
  char buf[4096];

  for (;;) {
    ssize_t n = tls_net_recv(c->fd, buf, sizeof buf);
    if (n > 0) {
      if (ws_parser_feed(&ws->parser, (const uint8_t *)buf, (size_t)n)) {
        reactor_ws_close(epfd, c);
        return;
      }
      continue;
    }
    if (n == 0) {
      reactor_ws_close(epfd, c);
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;
    reactor_ws_close(epfd, c);
    return;
  }

  for (;;) {
    int opcode, got;
    const uint8_t *payload;
    size_t plen;
    got = ws_parser_next(&ws->parser, &opcode, &payload, &plen);
    if (got < 0) {
      reactor_ws_close(epfd, c);
      return;
    }
    if (!got)
      break;
    if (opcode == WS_OP_CLOSE) {
      send_frame(epfd, c, WS_OP_CLOSE, payload, plen <= 125 ? plen : 0);
      c->close_after_flush = 1;
    } else if (opcode == WS_OP_PING) {
      send_frame(epfd, c, WS_OP_PONG, payload, plen);
    } else if (opcode == WS_OP_TEXT) {
      handle_text(epfd, c, (const char *)payload, plen);
    }
  }
  reactor_ws_flush(epfd, c);
}

static int ci_contains(const char *hay, const char *needle) {
  size_t nlen = strlen(needle);
  for (; *hay; hay++)
    if (!strncasecmp(hay, needle, nlen))
      return 1;
  return 0;
}

int ws_try_upgrade(conn_t *c, const char *path, const struct phr_header *headers, size_t num_headers, int keep_alive) {
  char conn_val[64], upg_val[32], key[80], accept[64], hdr[256];
  uint8_t digest[20];
  char input[128];

  (void)keep_alive;
  if (strcmp(path, "/ui/ws/") && strcmp(path, "/ui/ws"))
    return 0;
  if (!find_header(headers, num_headers, "Connection", conn_val, sizeof conn_val) || !ci_contains(conn_val, "upgrade"))
    return 1;
  if (!find_header(headers, num_headers, "Upgrade", upg_val, sizeof upg_val) || strcasecmp(upg_val, "websocket"))
    return 1;
  if (!find_header(headers, num_headers, "Sec-WebSocket-Key", key, sizeof key))
    return 1;

  {
    size_t klen = bufcpy(input, sizeof input, key);
    bufcpy(input + klen, sizeof input - klen, WS_GUID);
  }
  sha1(input, strlen(input), digest);
  base64_encode(digest, 20, accept);

  {
    size_t off = bufcpy(hdr, sizeof hdr,
                         "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                         "Sec-WebSocket-Accept: ");
    off += bufcpy(hdr + off, sizeof hdr - off, accept);
    off += bufcpy(hdr + off, sizeof hdr - off, "\r\n\r\n");
    conn_queue(c, hdr, off);
  }
  c->become_ws = 1;
  return 1;
}
