/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include "internal.h"
#include "reactor.h"
#include "reactor_tls.h"

#include "../core/htdocs.h"
#include "../core/metrics.h"
#include "../core/status.h"
#include "../dash/lldash.h"
#include "../hls/hls.h"
#include "../segment/segment.h"
#include "../ts/ts_push.h"
#include "../version.h"
#include "lib/demux/tspack.h"
#include "lib/helper/log.h"
#include "lib/helper/signal.h"
#ifdef HAVE_HTTP2
#include "../http2/http2.h"
#endif
#ifdef HAVE_HTTP3
#include "../http3/http3.h"
#endif

#include <errno.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <unistd.h>

/* zc completion also raises EPOLLERR: check before treating fatal */
static int socket_has_error(int fd) {
  int err = 0;
  socklen_t sl = sizeof err;
  getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &sl);
  return err != 0;
}

static void reactor_handle_event(int epfd, reactor_listeners_t *rl, int tid, struct epoll_event *evp) {
  void *ptr = evp->data.ptr;
  uint32_t e;
  conn_t *c;

  if (ptr >= (void *)&rl->L[0] && ptr <= (void *)&rl->L[rl->nL - 1]) {
    reactor_listener *lp = ptr;
    if (lp->kind == RL_TSPUSH_EFD) {
      uint64_t v = 0;
      ssize_t r = read(lp->fd, &v, sizeof v);
      (void)r;
      ts_push_flush_ready(tid);
#ifdef HAVE_HTTP3
      ts_push_h3_flush();
#endif
      return;
    }
    if (lp->kind == RL_DASHCHUNK_EFD) {
      uint64_t v = 0;
      ssize_t r = read(lp->fd, &v, sizeof v);
      (void)r;
      dash_lldash_flush_ready(tid);
      return;
    }
#ifdef HAVE_HTTP3
    if (lp->kind == RL_H3_UDP) {
      h3_handle_readable(lp->fd);
      return;
    }
#endif
    reactor_accept(epfd, lp);
    return;
  }

  c = ptr;
  e = evp->events;
  if (e & EPOLLERR)
    conn_zc_drain(c);
  if ((e & EPOLLHUP || (e & EPOLLERR && socket_has_error(c->fd))) && !(e & EPOLLIN)) {
    if (c->state == CONN_TSPUSH)
      reactor_tspush_close(epfd, c);
    else if (c->state == CONN_WS)
      reactor_ws_close(epfd, c);
    else if (c->state == CONN_DASHCHUNK)
      reactor_dashchunk_close(epfd, c);
#ifdef HAVE_HTTP2
    else if (c->state == CONN_H2)
      h2_conn_close(epfd, c);
#endif
    else
      reactor_close(epfd, c);
    return;
  }
  switch (c->state) {
    case CONN_TSPUSH:
      if (e & EPOLLIN)
        reactor_tspush_readable(epfd, c);
      else
        reactor_conn_flush(epfd, c);
      break;
    case CONN_DASHCHUNK:
      if (e & EPOLLIN)
        reactor_dashchunk_readable(epfd, c);
      else
        reactor_dashchunk_flush(epfd, c);
      break;
    case CONN_WS:
      if (e & EPOLLIN)
        reactor_ws_readable(epfd, c);
      else
        reactor_ws_flush(epfd, c);
      break;
    case CONN_WRITING:
      reactor_finish(epfd, c);
      break;
    case CONN_TLS_HANDSHAKE:
      reactor_handshake(epfd, c);
      break;
    case CONN_DISPATCH:
      break; /* LL-HLS blocking-reload park: llhls_flush_waiters() resumes it, not epoll events */
#ifdef HAVE_HTTP2
    case CONN_H2:
      if (e & EPOLLIN)
        h2_handle_readable(epfd, c);
      else
        h2_handle_writable(epfd, c);
      break;
#endif
    default:
      reactor_read(epfd, c);
      break;
  }
}

void *worker_thread(void *arg) {
  int tid = (int)(intptr_t)arg;
  reactor_listeners_t rl;
  int epfd;

  t_reactor_tid = tid;
  epfd = epoll_create1(EPOLL_CLOEXEC);
  if (epfd < 0) return NULL;
  t_reactor_epfd = epfd;

  reactor_setup_listeners(&rl, epfd, tid);
  if (tid == 0) reactor_notify_listening();

  for (;;) {
    struct epoll_event events[256];
    int nev, timeout_ms = 200;
    if (signal_stop_requested()) break;
#ifdef HAVE_HTTP3
    {
      int h3ms = h3_next_timeout_ms();
      if (h3ms >= 0 && h3ms < timeout_ms) timeout_ms = h3ms;
    }
#endif
    nev = epoll_wait(epfd, events, 256, timeout_ms);
    if (nev < 0) {
      if (errno == EINTR) continue;
      break;
    }
    for (int i = 0; i < nev; i++) reactor_handle_event(epfd, &rl, tid, &events[i]);
    if (tid == 0) conn_sweep_idle(reactor_cfg()->idle_timeout_s);
    llhls_flush_waiters();
    hls_cold_flush_waiters();
    htdocs_template_reload_check();
#ifdef HAVE_HTTP2
    h2_llhls_flush_waiters();
    h2_hls_cold_flush_waiters();
#endif
#ifdef HAVE_HTTP3
    h3_llhls_flush_waiters();
    h3_hls_cold_flush_waiters();
    h3_ws_flush();
    h3_tick();
#endif
  }

  reactor_teardown_listeners(&rl, tid);
  close(epfd);
  return NULL;
}

static void capture_pump_feed(capture_ctx_t *ctx, void *user, const unsigned char *pkt) {
  (void)user;
  if (tspack_pid(pkt) == 0x1FFF) /* stuffing, no payload, matches dipirec's STRIP_NUL default */
    return;
  ts_push_feed_pkt(ctx, pkt);
  hls_seg_feed_all(ctx, pkt);
}

void *pump_thread(void *arg) {
  int pid = (int)(intptr_t)arg;
  int sweep_counter = 0;
  while (!signal_stop_requested()) {
    int drained = capture_pump_tick(pid, capture_pump_feed, NULL);
    if (pid == 0) { /* periodic maintenance: one pump thread only, not per-shard */
      if (++sweep_counter >= 500) {
        hls_seg_sweep_idle();
        hls_seg_pool_trim_idle();
        dipixy_status_tick();
        ws_clients_tick();
        tls_gc_sweep();
        sweep_counter = 0;
      }
      if (signal_tls_reload_requested()) log_line(TOOL_NAME ": SIGUSR1: TLS cert reload %s", reload_tls() == 0 ? "ok" : "failed");
      dipixy_metrics_push(reactor_metrics());
    }
    if (!drained) usleep(2000); /* idle: back off, busy: loop into next tick */
  }
  return NULL;
}
