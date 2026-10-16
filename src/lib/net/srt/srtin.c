/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <unistd.h>

#include <srt/srt.h>

#include "lib/helper/log.h"
#include "lib/sys/signal.h"

#include "srtcommon.h"
#include "srtin.h"

#define SRT_RCVTIMEO_MS 200
#define SRT_CONNECT_SLICE_MS 100
#define SRT_CONNECT_TIMEOUT_DEFAULT_MS 3000
#define SRT_REVIVE_INTERVAL_S 1.0 /* min gap between bonded member re-add checks */

struct srtin {
  SRTSOCKET sock;
  SRTSOCKET listeners[SRTCOMMON_MAX_PEERS]; /* kept alive alongside sock, see open_single_listener() */
  int n_listeners;
  atomic_int *busy; /* listener side: 1 while sock live. read by libsrt thread, heap: outlives r use */
  srtgroup_mode_t group_mode;
  srtin_cfg_t cfg; /* caller reconnect re-dials from it */
  metrics_exporter_t *mx;
  const char *tool_version;
  double next_revive_at; /* mono_seconds() gate for bonded caller member re-add */
  srtcommon_resolver_t *revive_res; /* in-flight member re-add lookups */
};

static int stop_wanted(const srtin_cfg_t *cfg) {
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

static void idle_wait(void);

static int stop_cb(void *arg) { return stop_wanted(arg); }

/* transient (nullable): set when failure is peer-side and worth retrying */
static SRTSOCKET open_single_caller(const srtin_cfg_t *cfg, int *transient) {
  SRTSOCKET s;
  struct sockaddr_storage remote;
  struct sockaddr_storage local;
  int remote_len;
  int local_len;

  if (transient) *transient = 0;
  if (srtcommon_resolve_stoppable(cfg->peers[0].host, cfg->peers[0].port, &remote, &remote_len, connect_total_ms(cfg), stop_cb, (void *)cfg)) {
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
static int accept_poll(SRTSOCKET lsn, SRTSOCKET *out, atomic_int *busy) {
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
static SRTSOCKET open_single_listener(const srtin_cfg_t *cfg, SRTSOCKET *lsn_out, atomic_int *busy) {
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

static SRTSOCKET open_group_caller(const srtin_cfg_t *cfg, int *transient) {
  SRTSOCKET g;
  SRT_GROUP_TYPE type = cfg->group_mode == SRTGROUP_BACKUP ? SRT_GTYPE_BACKUP : SRT_GTYPE_BROADCAST;
  SRT_SOCKGROUPCONFIG gc[SRTCOMMON_MAX_PEERS];

  if (transient) *transient = 0;
  g = srt_create_group(type);
  if (g == SRT_INVALID_SOCK) {
    log_code("LE500164", "srt: group create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_apply_opts(g, &cfg->opts, 1, SRTO_RCVTIMEO, SRT_RCVTIMEO_MS)) {
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_build_group_config(cfg->peers, cfg->npeers, gc)) {
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  if (srt_connect_group(g, gc, cfg->npeers) == SRT_ERROR) {
    log_code("LE600139", "srt: group connect failed: %s", srt_getlasterror_str());
    srt_close(g);
    if (transient) *transient = 1;
    return SRT_INVALID_SOCK;
  }
  return g;
}

/* one listener per bonded peer. accept_bond returns first hello. GROUPCONNECT: accept
   returns group id, not socket. lifetime rule: see open_single_listener() */
static SRTSOCKET open_group_listener(const srtin_cfg_t *cfg, SRTSOCKET *listeners_out, int *n_listeners_out) {
  SRTSOCKET listeners[SRTCOMMON_MAX_PEERS];
  int groupconnect = 1;
  SRTSOCKET s = SRT_INVALID_SOCK;
  int i;

  *n_listeners_out = 0;
  for (i = 0; i < cfg->npeers; i++) {
    struct sockaddr_storage local;
    int local_len;

    listeners[i] = srt_create_socket();
    if (listeners[i] == SRT_INVALID_SOCK) {
      log_code("LE500159", "srt: socket create failed: %s", srt_getlasterror_str());
      goto fail;
    }
    if (srtcommon_apply_opts(listeners[i], &cfg->opts, 0, SRTO_RCVTIMEO, SRT_RCVTIMEO_MS) ||
        srt_setsockopt(listeners[i], 0, SRTO_GROUPCONNECT, &groupconnect, sizeof groupconnect) != 0) {
      log_code("LE500158", "srt: setsockopt failed: %s", srt_getlasterror_str());
      goto fail;
    }
    if (srtcommon_resolve(cfg->peers[i].host, cfg->peers[i].port, &local, &local_len))
      goto fail;
    if (srt_bind(listeners[i], (struct sockaddr *)&local, local_len) != 0 || srt_listen(listeners[i], 1) != 0) {
      log_code("LE500165", "srt: bind/listen failed on peer %d: %s", i, srt_getlasterror_str());
      goto fail;
    }
  }
  while (!stop_wanted(cfg)) {
    s = srt_accept_bond(listeners, cfg->npeers, SRT_RCVTIMEO_MS);
    if (s != SRT_INVALID_SOCK)
      break;
    if (srt_getlasterror(NULL) != SRT_ETIMEOUT) {
      log_code("LE600140", "srt: accept_bond failed: %s", srt_getlasterror_str());
      s = SRT_INVALID_SOCK;
      break;
    }
  }
  if (s == SRT_INVALID_SOCK) {
    for (int j = 0; j < i; j++)
      srt_close(listeners[j]);
  } else {
    memcpy(listeners_out, listeners, (size_t)i * sizeof listeners[0]);
    *n_listeners_out = i;
  }
  return s;

fail:
  for (int j = 0; j < i; j++)
    srt_close(listeners[j]);
  return SRT_INVALID_SOCK;
}

/* listeners_out/n_listeners_out: sockets outliving this call, see open_single_listener().
   caller flavors: empty, nothing to keep alive */
static SRTSOCKET open_srt_input(const srtin_cfg_t *cfg, SRTSOCKET *listeners_out, int *n_listeners_out, atomic_int *busy, int *transient) {
  *n_listeners_out = 0;
  *transient = 0;
  if (cfg->group_mode != SRTGROUP_NONE)
    return cfg->listen ? open_group_listener(cfg, listeners_out, n_listeners_out) : open_group_caller(cfg, transient);
  if (cfg->listen) {
    SRTSOCKET lsn;
    SRTSOCKET s = open_single_listener(cfg, &lsn, busy);
    if (lsn != SRT_INVALID_SOCK) {
      listeners_out[0] = lsn;
      *n_listeners_out = 1;
    }
    return s;
  }
  return open_single_caller(cfg, transient);
}

srtin_t *srtin_open(const srtin_cfg_t *cfg) {
  srtin_t *r;
  SRTSOCKET s;
  SRTSOCKET listeners[SRTCOMMON_MAX_PEERS];
  int n_listeners;
  atomic_int *busy;

  if (cfg->npeers <= 0 || cfg->npeers > SRTCOMMON_MAX_PEERS) {
    log_code("LE000322", "srt: invalid peer count %d", cfg->npeers);
    return NULL;
  }
  if (cfg->group_mode == SRTGROUP_NONE && cfg->npeers != 1) {
    log_code("LE000323", "srt: plain connection needs exactly one peer, got %d", cfg->npeers);
    return NULL;
  }

  if (srtcommon_startup() != 0) {
    log_code("LE500166", "srt: startup failed: %s", srt_getlasterror_str());
    return NULL;
  }
  srtcommon_open_logging(cfg->verbose);

  busy = calloc(1, sizeof *busy);
  if (!busy) {
    srtcommon_cleanup();
    return NULL;
  }

  for (;;) {
    int transient;
    s = open_srt_input(cfg, listeners, &n_listeners, busy, &transient);
    if (s != SRT_INVALID_SOCK || !cfg->retry_open || !transient || stop_wanted(cfg)) break;
    idle_wait();
  }
  if (s == SRT_INVALID_SOCK) {
    free(busy);
    srtcommon_cleanup();
    return NULL;
  }

  r = calloc(1, sizeof *r);
  if (!r) {
    srt_close(s);
    for (int i = 0; i < n_listeners; i++)
      srt_close(listeners[i]);
    free(busy);
    srtcommon_cleanup();
    return NULL;
  }
  r->sock = s;
  r->busy = busy;
  r->n_listeners = n_listeners;
  memcpy(r->listeners, listeners, (size_t)n_listeners * sizeof listeners[0]);
  r->group_mode = cfg->group_mode;
  r->cfg = *cfg;
  r->mx = cfg->mx;
  r->tool_version = cfg->tool_version;
  return r;
}

/* no stats without live socket: header and extras still go out */
static void push_stats(srtin_t *r) {
  SRT_TRACEBSTATS st;
  int have;

  if (!r->mx || !metrics_exporter_due(r->mx, mono_seconds())) return;
  have = r->sock != SRT_INVALID_SOCK && srt_bstats(r->sock, &st, 0) == 0;
  if (!have) {
    metrics_push_entries(r->mx, r->tool_version, NULL, 0);
    return;
  }
  metrics_entry_t e[] = {
  {METRICS_ID_SRT_RECEIVER_RECEIVED_TOTAL, NULL, (uint64_t)st.pktRecvTotal},
  {METRICS_ID_SRT_RECEIVER_LOST_TOTAL, NULL, (uint64_t)st.pktRcvLossTotal},
  {METRICS_ID_SRT_RECEIVER_DROPPED_TOTAL, NULL, (uint64_t)st.pktRcvDropTotal},
  {METRICS_ID_SRT_RECEIVER_RTT_MILLISECONDS, NULL, (uint64_t)st.msRTT},
  {METRICS_ID_SRT_RECEIVER_BUFFER_MILLISECONDS, NULL, (uint64_t)st.msRcvTsbPdDelay},
  };
  metrics_push_entries(r->mx, r->tool_version, e, sizeof e / sizeof e[0]);
}

static void idle_wait(void) {
  int wfd = signal_wake_fd();
  if (wfd >= 0) {
    struct pollfd pfd = {wfd, POLLIN, 0};
    poll(&pfd, 1, SRT_RCVTIMEO_MS);
  } else {
    usleep(SRT_RCVTIMEO_MS * 1000);
  }
}

/* 1: r->sock live again */
static int reconnect_once(srtin_t *r) {
  if (r->group_mode != SRTGROUP_NONE && r->n_listeners > 0) {
    r->sock = srt_accept_bond(r->listeners, r->n_listeners, SRT_RCVTIMEO_MS);
    if (r->sock != SRT_INVALID_SOCK) return 1;
    if (srt_getlasterror(NULL) != SRT_ETIMEOUT) log_code("LW600141", "srt: reconnect attempt failed: %s", srt_getlasterror_str());
    return 0;
  }
  if (r->group_mode != SRTGROUP_NONE) {
    r->sock = open_group_caller(&r->cfg, NULL);
    if (r->sock == SRT_INVALID_SOCK) idle_wait();
    return r->sock != SRT_INVALID_SOCK;
  }
  if (r->n_listeners > 0) {
    if (accept_poll(r->listeners[0], &r->sock, r->busy) < 0) idle_wait();
    return r->sock != SRT_INVALID_SOCK;
  }
  r->sock = open_single_caller(&r->cfg, NULL);
  if (r->sock == SRT_INVALID_SOCK) idle_wait();
  return r->sock != SRT_INVALID_SOCK;
}

static int read_once(srtin_t *r, unsigned char *buf, size_t cap, int *reconnected_out) {
  int n;

  *reconnected_out = 0;

  /* mid-reconnect: one attempt per call, returns like a timeout. caller's loop drives retry cadence, !internal */
  if (r->sock == SRT_INVALID_SOCK) {
    if (stop_wanted(&r->cfg)) return 0;
    if (!reconnect_once(r)) return 0;
    *reconnected_out = 1;
    return 0;
  }

  if (r->group_mode != SRTGROUP_NONE && r->n_listeners == 0) {
    /* libsrt never re-dials a broken member of a live group */
    double now = mono_seconds();
    if (r->revive_res || now >= r->next_revive_at) {
      if (!r->revive_res) r->next_revive_at = now + SRT_REVIVE_INTERVAL_S;
      srtcommon_group_revive(r->sock, r->cfg.peers, r->cfg.npeers, &r->revive_res);
    }
  }

  n = srt_recvmsg2(r->sock, (char *)buf, (int)cap, NULL);
  if (n == SRT_ERROR) {
    int err = srt_getlasterror(NULL);
    if (err == SRT_ETIMEOUT) return 0;
    if (err == SRT_EASYNCRCV) {
      /* group with every member link down right now, not a dead group: retry */
      idle_wait();
      return 0;
    }
    if (r->group_mode != SRTGROUP_NONE) {
      /* group error: state could be corrupted, reconnect regardless of cause.
         next call retries, one attempt each, see above */
      log_code("LW600142", "srt: read failed on group (%s), reconnecting", srt_getlasterror_str());
    } else {
      /* peer gone: listener goes back to accept, caller re-dials */
      log_code("LW600143", "srt: link lost (%s), reconnecting", srt_getlasterror_str());
    }
    srt_close(r->sock);
    r->sock = SRT_INVALID_SOCK;
    atomic_store(r->busy, 0);
    return 0;
  }
  return n;
}

int srtin_read(srtin_t *r, unsigned char *buf, size_t cap, int *reconnected_out) {
  int n = read_once(r, buf, cap, reconnected_out);
  push_stats(r);
  return n;
}

void srtin_stats(const srtin_t *r, srtin_stats_t *out) {
  SRT_TRACEBSTATS st;

  memset(out, 0, sizeof *out);
  if (r->sock == SRT_INVALID_SOCK || srt_bstats(r->sock, &st, 0) != 0) return;
  out->connected = 1;
  out->packets = (uint64_t)st.pktRecvTotal;
  out->lost = (uint64_t)st.pktRcvLossTotal;
  out->dropped = (uint64_t)st.pktRcvDropTotal;
  out->rtt_ms = st.msRTT;
  out->mbps = st.mbpsRecvRate;
}

void srtin_close(srtin_t *r) {
  if (!r) return;
  if (r->sock != SRT_INVALID_SOCK) srt_close(r->sock);
  for (int i = 0; i < r->n_listeners; i++) srt_close(r->listeners[i]);
  srtcommon_resolver_free(r->revive_res);
  srtcommon_cleanup();
  free(r->busy);
  free(r);
}
