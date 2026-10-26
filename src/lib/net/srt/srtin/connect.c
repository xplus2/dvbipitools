/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdint.h>
#include <time.h>
#include <sys/socket.h>
#include <srt/srt.h>

#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "lib/net/srt/srtcommon.h"
#include "lib/net/srt/srtin.h"
#include "lib/net/srt/srtin/srtin_int.h"

int stop_wanted(const srtin_cfg_t *cfg) {
  return signal_stop_requested() || (cfg->stop && atomic_load_explicit(cfg->stop, memory_order_relaxed));
}

static unsigned connect_total_ms(const srtin_cfg_t *cfg) {
  return cfg->connect_timeout_ms ? cfg->connect_timeout_ms : SRT_CONNECT_TIMEOUT_DEFAULT_MS;
}

/* non-blocking connect (rendezvous if local set), polled in slices. 0 connected, -1 fail/stopped */
static int connect_interruptible(const srtin_cfg_t *cfg, SRTSOCKET s, const struct sockaddr *local, int local_len,
                                 const struct sockaddr *remote, int remote_len) {
  unsigned total = connect_total_ms(cfg);
  int sync = 0;
  int tmo = (int)total;
  int events = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
  int eid;
  struct timespec t0;
  int rc = -1;

  if (srt_setsockopt(s, 0, SRTO_RCVSYN, &sync, sizeof sync) != 0 || srt_setsockopt(s, 0, SRTO_CONNTIMEO, &tmo, sizeof tmo) != 0) {
    log_code("LE500158", "srt: setsockopt failed: %s", srt_getlasterror_str());
    return -1;
  }
  eid = srt_epoll_create();
  if (eid < 0) {
    log_code("LE500160", "srt: epoll create failed: %s", srt_getlasterror_str());
    return -1;
  }
  if (srt_epoll_add_usock(eid, s, &events) != 0) {
    log_code("LE500161", "srt: epoll add failed: %s", srt_getlasterror_str());
    goto out;
  }
  clock_gettime(CLOCK_MONOTONIC, &t0);
  if ((local ? srt_rendezvous(s, local, local_len, remote, remote_len) : srt_connect(s, remote, remote_len)) == SRT_ERROR &&
      srt_getlasterror(NULL) != SRT_EASYNCSND) {
    log_code(local ? "LE600136" : "LE600137", "srt: %s failed: %s", local ? "rendezvous" : "connect", srt_getlasterror_str());
    goto out;
  }
  while (!stop_wanted(cfg)) {
    SRT_SOCKSTATUS st = srt_getsockstate(s);
    SRTSOCKET rd[1];
    SRTSOCKET wr[1];
    int nrd = 1;
    int nwr = 1;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (st == SRTS_CONNECTED) {
      sync = 1;
      if (srt_setsockopt(s, 0, SRTO_RCVSYN, &sync, sizeof sync) != 0) {
        log_code("LE500158", "srt: setsockopt failed: %s", srt_getlasterror_str());
        goto out;
      }
      rc = 0;
      goto out;
    }
    if (st != SRTS_CONNECTING) {
      int rej = srt_getrejectreason(s);
      log_code("LE600137", "srt: connect failed: %s", rej == SRT_REJ_UNKNOWN ? srt_getlasterror_str() : srt_rejectreason_str(rej));
      goto out;
    }
    if ((now.tv_sec - t0.tv_sec) * 1000L + (now.tv_nsec - t0.tv_nsec) / 1000000L >= (long)total) {
      log_code("LE600137", "srt: connect failed: timeout");
      goto out;
    }
    srt_epoll_wait(eid, rd, &nrd, wr, &nwr, SRT_CONNECT_SLICE_MS, NULL, NULL, NULL, NULL);
  }

out:
  srt_epoll_release(eid);
  return rc;
}

static int stop_cb(const void *arg) { return stop_wanted(arg); }

/* transient (nullable): set when failure is peer-side and worth retrying */
SRTSOCKET open_single_caller(const srtin_cfg_t *cfg, int *transient) {
  SRTSOCKET s;
  struct sockaddr_storage remote;
  struct sockaddr_storage local;
  int remote_len;
  int local_len;

  if (transient) *transient = 0;
  if (srtcommon_resolve_stoppable(cfg->peers[0].host, cfg->peers[0].port, &remote, &remote_len, connect_total_ms(cfg), stop_cb, cfg)) {
    if (transient) *transient = 1;
    return SRT_INVALID_SOCK;
  }

  s = srt_create_socket();
  if (s == SRT_INVALID_SOCK) {
    log_code("LE500159", "srt: socket create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_apply_opts(s, &cfg->opts, 0, SRTO_RCVTIMEO, SRT_RCVTIMEO_MS)) {
    srt_close(s);
    return SRT_INVALID_SOCK;
  }

  if (cfg->rendezvous) {
    if (!cfg->local_host) {
      log_code("LE000321", "srt: rendezvous requires a local bind address");
      srt_close(s);
      return SRT_INVALID_SOCK;
    }
    if (srtcommon_resolve(cfg->local_host, cfg->local_port, &local, &local_len)) {
      srt_close(s);
      return SRT_INVALID_SOCK;
    }
    if (connect_interruptible(cfg, s, (struct sockaddr *)&local, local_len, (struct sockaddr *)&remote, remote_len) != 0) {
      srt_close(s);
      if (transient) *transient = 1;
      return SRT_INVALID_SOCK;
    }
  } else if (connect_interruptible(cfg, s, NULL, 0, (struct sockaddr *)&remote, remote_len) != 0) {
    srt_close(s);
    if (transient) *transient = 1;
    return SRT_INVALID_SOCK;
  }
  return s;
}

/* single listener queues extra callers: they would handshake and send into nothing, a stale one
   could be accepted after reconnect. reject while a connection is live */
static int reject_while_busy(void *opaq, SRTSOCKET ns, int hsversion, const struct sockaddr *peeraddr, const char *streamid) {
  atomic_int *busy = opaq;

  (void)hsversion;
  (void)peeraddr;
  (void)streamid;
  if (!atomic_load(busy)) return 0;
  srt_setrejectreason(ns, SRT_REJ_RESOURCE);
  return -1;
}

/* bounded wait. 1: accepted into *out, busy set. 0: timeout. -1: error */
int accept_poll(SRTSOCKET lsn, SRTSOCKET *out, atomic_int *busy) {
  int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
  int eid = srt_epoll_create();
  SRTSOCKET ready[1];
  int nready = 1;
  int rc = 1;

  if (eid < 0) {
    log_code("LE500160", "srt: epoll create failed: %s", srt_getlasterror_str());
    return -1;
  }
  if (srt_epoll_add_usock(eid, lsn, &events) != 0) {
    log_code("LE500161", "srt: epoll add failed: %s", srt_getlasterror_str());
    srt_epoll_release(eid);
    return -1;
  }
  if (srt_epoll_wait(eid, ready, &nready, NULL, NULL, SRT_RCVTIMEO_MS, NULL, NULL, NULL, NULL) == SRT_ERROR) {
    if (srt_getlasterror(NULL) == SRT_ETIMEOUT) {
      rc = 0;
    } else {
      log_code("LE500162", "srt: accept wait failed: %s", srt_getlasterror_str());
      rc = -1;
    }
  } else {
    *out = srt_accept(lsn, NULL, NULL);
    if (*out == SRT_INVALID_SOCK) {
      log_code("LE600138", "srt: accept failed: %s", srt_getlasterror_str());
      rc = -1;
    } else if (busy) {
      atomic_store(busy, 1);
    }
  }
  srt_epoll_release(eid);
  return rc;
}

static SRTSOCKET accept_single(const srtin_cfg_t *cfg, SRTSOCKET lsn, atomic_int *busy) {
  SRTSOCKET s = SRT_INVALID_SOCK;

  while (!stop_wanted(cfg)) {
    if (accept_poll(lsn, &s, busy) != 0)
      break;
  }
  return s;
}

/* accepted socket shares listener's UDP multiplexer. early close: kills connection too.
   keep listener open until final teardown. lsn_out: carries it out */
SRTSOCKET open_single_listener(const srtin_cfg_t *cfg, SRTSOCKET *lsn_out, atomic_int *busy) {
  SRTSOCKET lsn;
  SRTSOCKET s = SRT_INVALID_SOCK;
  struct sockaddr_storage local;
  int local_len;

  *lsn_out = SRT_INVALID_SOCK;
  if (srtcommon_resolve(cfg->peers[0].host, cfg->peers[0].port, &local, &local_len))
    return SRT_INVALID_SOCK;
  lsn = srt_create_socket();
  if (lsn == SRT_INVALID_SOCK) {
    log_code("LE500159", "srt: socket create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_apply_opts(lsn, &cfg->opts, 0, SRTO_RCVTIMEO, SRT_RCVTIMEO_MS)) {
    srt_close(lsn);
    return SRT_INVALID_SOCK;
  }
  if (srt_listen_callback(lsn, reject_while_busy, busy) != 0) {
    log_code("LE500243", "srt: listen callback failed: %s", srt_getlasterror_str());
    srt_close(lsn);
    return SRT_INVALID_SOCK;
  }
  if (srt_bind(lsn, (struct sockaddr *)&local, local_len) != 0 || srt_listen(lsn, 1) != 0) {
    log_code("LE500163", "srt: bind/listen failed: %s", srt_getlasterror_str());
    srt_close(lsn);
    return SRT_INVALID_SOCK;
  }
  s = accept_single(cfg, lsn, busy);
  if (s == SRT_INVALID_SOCK)
    srt_close(lsn); /* no connection ever came in: nothing depends on this multiplexer */
  else
    *lsn_out = lsn;
  return s;
}
