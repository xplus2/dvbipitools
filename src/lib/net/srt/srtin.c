/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <srt/srt.h>

#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include "srtcommon.h"
#include "srtin.h"

#define SRT_RCVTIMEO_MS 200

struct srtin {
  SRTSOCKET sock;
  SRTSOCKET listeners[SRTCOMMON_MAX_PEERS]; /* kept alive alongside sock, see open_single_listener() */
  int n_listeners;
  srtgroup_mode_t group_mode;
  metrics_exporter_t *mx;
  const char *tool_version;
};

static SRTSOCKET open_single_caller(const srtin_cfg_t *cfg) {
  SRTSOCKET s;
  struct sockaddr_storage remote;
  struct sockaddr_storage local;
  int remote_len;
  int local_len;

  if (srtcommon_resolve(cfg->peers[0].host, cfg->peers[0].port, &remote, &remote_len))
    return SRT_INVALID_SOCK;

  s = srt_create_socket();
  if (s == SRT_INVALID_SOCK) {
    log_line("srt: socket create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_apply_opts(s, &cfg->opts, 0, SRTO_RCVTIMEO, SRT_RCVTIMEO_MS)) {
    srt_close(s);
    return SRT_INVALID_SOCK;
  }

  if (cfg->rendezvous) {
    if (!cfg->local_host) {
      log_line("srt: rendezvous requires a local bind address");
      srt_close(s);
      return SRT_INVALID_SOCK;
    }
    if (srtcommon_resolve(cfg->local_host, cfg->local_port, &local, &local_len)) {
      srt_close(s);
      return SRT_INVALID_SOCK;
    }
    if (srt_rendezvous(s, (struct sockaddr *)&local, local_len, (struct sockaddr *)&remote, remote_len) != 0) {
      log_line("srt: rendezvous failed: %s", srt_getlasterror_str());
      srt_close(s);
      return SRT_INVALID_SOCK;
    }
  } else if (srt_connect(s, (struct sockaddr *)&remote, remote_len) != 0) {
    log_line("srt: connect failed: %s", srt_getlasterror_str());
    srt_close(s);
    return SRT_INVALID_SOCK;
  }
  return s;
}

/* accepted socket shares listener's UDP multiplexer. early close: kills connection too.
   keep listener open until final teardown. lsn_out: carries it out */
static SRTSOCKET open_single_listener(const srtin_cfg_t *cfg, SRTSOCKET *lsn_out) {
  SRTSOCKET lsn;
  SRTSOCKET s = SRT_INVALID_SOCK;
  struct sockaddr_storage local;
  int local_len;

  *lsn_out = SRT_INVALID_SOCK;
  if (srtcommon_resolve(cfg->peers[0].host, cfg->peers[0].port, &local, &local_len))
    return SRT_INVALID_SOCK;
  lsn = srt_create_socket();
  if (lsn == SRT_INVALID_SOCK) {
    log_line("srt: socket create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_apply_opts(lsn, &cfg->opts, 0, SRTO_RCVTIMEO, SRT_RCVTIMEO_MS)) {
    srt_close(lsn);
    return SRT_INVALID_SOCK;
  }
  if (srt_bind(lsn, (struct sockaddr *)&local, local_len) != 0 || srt_listen(lsn, 1) != 0) {
    log_line("srt: bind/listen failed: %s", srt_getlasterror_str());
    srt_close(lsn);
    return SRT_INVALID_SOCK;
  }
  while (!signal_stop_requested()) {
    s = srt_accept(lsn, NULL, NULL);
    if (s != SRT_INVALID_SOCK)
      break;
    if (srt_getlasterror(NULL) != SRT_ETIMEOUT) {
      log_line("srt: accept failed: %s", srt_getlasterror_str());
      s = SRT_INVALID_SOCK;
      break;
    }
  }
  if (s == SRT_INVALID_SOCK)
    srt_close(lsn); /* no connection ever came in: nothing depends on this multiplexer */
  else
    *lsn_out = lsn;
  return s;
}

static SRTSOCKET open_group_caller(const srtin_cfg_t *cfg) {
  SRTSOCKET g;
  SRT_GROUP_TYPE type = cfg->group_mode == SRTGROUP_BACKUP ? SRT_GTYPE_BACKUP : SRT_GTYPE_BROADCAST;
  SRT_SOCKGROUPCONFIG gc[SRTCOMMON_MAX_PEERS];

  g = srt_create_group(type);
  if (g == SRT_INVALID_SOCK) {
    log_line("srt: group create failed: %s", srt_getlasterror_str());
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
    log_line("srt: group connect failed: %s", srt_getlasterror_str());
    srt_close(g);
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
      log_line("srt: socket create failed: %s", srt_getlasterror_str());
      goto fail;
    }
    if (srtcommon_apply_opts(listeners[i], &cfg->opts, 0, SRTO_RCVTIMEO, SRT_RCVTIMEO_MS) ||
        srt_setsockopt(listeners[i], 0, SRTO_GROUPCONNECT, &groupconnect, sizeof groupconnect) != 0) {
      log_line("srt: setsockopt failed: %s", srt_getlasterror_str());
      goto fail;
    }
    if (srtcommon_resolve(cfg->peers[i].host, cfg->peers[i].port, &local, &local_len))
      goto fail;
    if (srt_bind(listeners[i], (struct sockaddr *)&local, local_len) != 0 || srt_listen(listeners[i], 1) != 0) {
      log_line("srt: bind/listen failed on peer %d: %s", i, srt_getlasterror_str());
      goto fail;
    }
  }
  while (!signal_stop_requested()) {
    s = srt_accept_bond(listeners, cfg->npeers, SRT_RCVTIMEO_MS);
    if (s != SRT_INVALID_SOCK)
      break;
    if (srt_getlasterror(NULL) != SRT_ETIMEOUT) {
      log_line("srt: accept_bond failed: %s", srt_getlasterror_str());
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
static SRTSOCKET open_srt_input(const srtin_cfg_t *cfg, SRTSOCKET *listeners_out, int *n_listeners_out) {
  *n_listeners_out = 0;
  if (cfg->group_mode != SRTGROUP_NONE)
    return cfg->listen ? open_group_listener(cfg, listeners_out, n_listeners_out) : open_group_caller(cfg);
  if (cfg->listen) {
    SRTSOCKET lsn;
    SRTSOCKET s = open_single_listener(cfg, &lsn);
    if (lsn != SRT_INVALID_SOCK) {
      listeners_out[0] = lsn;
      *n_listeners_out = 1;
    }
    return s;
  }
  return open_single_caller(cfg);
}

srtin_t *srtin_open(const srtin_cfg_t *cfg) {
  srtin_t *r;
  SRTSOCKET s;
  SRTSOCKET listeners[SRTCOMMON_MAX_PEERS];
  int n_listeners;

  if (cfg->npeers <= 0 || cfg->npeers > SRTCOMMON_MAX_PEERS) {
    log_line("srt: invalid peer count %d", cfg->npeers);
    return NULL;
  }
  if (cfg->group_mode == SRTGROUP_NONE && cfg->npeers != 1) {
    log_line("srt: plain connection needs exactly one peer, got %d", cfg->npeers);
    return NULL;
  }

  if (srt_startup() == SRT_ERROR) {
    log_line("srt: startup failed: %s", srt_getlasterror_str());
    return NULL;
  }
  srtcommon_open_logging(cfg->verbose);

  s = open_srt_input(cfg, listeners, &n_listeners);
  if (s == SRT_INVALID_SOCK) {
    srt_cleanup();
    return NULL;
  }

  r = calloc(1, sizeof *r);
  if (!r) {
    srt_close(s);
    for (int i = 0; i < n_listeners; i++)
      srt_close(listeners[i]);
    srt_cleanup();
    return NULL;
  }
  r->sock = s;
  r->n_listeners = n_listeners;
  memcpy(r->listeners, listeners, (size_t)n_listeners * sizeof listeners[0]);
  r->group_mode = cfg->group_mode;
  r->mx = cfg->mx;
  r->tool_version = cfg->tool_version;
  return r;
}

static void push_stats(srtin_t *r) {
  SRT_TRACEBSTATS st;
  metrics_writer_t w;

  if (!r->mx || !metrics_exporter_due(r->mx, mono_seconds()) || srt_bstats(r->sock, &st, 0) != 0)
    return;
  if (metrics_exporter_begin(r->mx, &w, r->tool_version))
    return;
  metrics_writer_put(&w, METRICS_ID_SRT_RECEIVER_RECEIVED_TOTAL, NULL, (uint64_t)st.pktRecvTotal);
  metrics_writer_put(&w, METRICS_ID_SRT_RECEIVER_LOST_TOTAL, NULL, (uint64_t)st.pktRcvLossTotal);
  metrics_writer_put(&w, METRICS_ID_SRT_RECEIVER_DROPPED_TOTAL, NULL, (uint64_t)st.pktRcvDropTotal);
  metrics_writer_put(&w, METRICS_ID_SRT_RECEIVER_RTT_MILLISECONDS, NULL, (uint64_t)st.msRTT);
  metrics_writer_put(&w, METRICS_ID_SRT_RECEIVER_BUFFER_MILLISECONDS, NULL, (uint64_t)st.msRcvTsbPdDelay);
  metrics_exporter_send(r->mx, &w);
}

int srtin_read(srtin_t *r, unsigned char *buf, size_t cap, int *reconnected_out) {
  int n;

  *reconnected_out = 0;

  /* mid-reconnect: one attempt per call, returns like a timeout.
     caller's loop drives retry cadence, !internal */
  if (r->sock == SRT_INVALID_SOCK) {
    if (signal_stop_requested())
      return 0;
    r->sock = srt_accept_bond(r->listeners, r->n_listeners, SRT_RCVTIMEO_MS);
    if (r->sock == SRT_INVALID_SOCK) {
      if (srt_getlasterror(NULL) != SRT_ETIMEOUT)
        log_line("srt: reconnect attempt failed: %s", srt_getlasterror_str());
      return 0;
    }
    *reconnected_out = 1;
    return 0;
  }

  n = srt_recvmsg2(r->sock, (char *)buf, (int)cap, NULL);
  if (n == SRT_ERROR) {
    int err = srt_getlasterror(NULL);

    if (err == SRT_ETIMEOUT)
      return 0;
    if (err == SRT_EASYNCRCV) {
      /* group with every member link down right now, not a dead group: retry */
      usleep(SRT_RCVTIMEO_MS * 1000);
      return 0;
    }
    if (r->group_mode != SRTGROUP_NONE && r->n_listeners > 0) {
      /* group error: state could be corrupted, reconnect regardless of cause.
         next call retries, one attempt each, see above */
      log_line("srt: read failed on group (%s), reconnecting", srt_getlasterror_str());
      srt_close(r->sock);
      r->sock = SRT_INVALID_SOCK;
      return 0;
    }
    log_line("srt: read failed: %s", srt_getlasterror_str());
    return -1;
  }
  if (n > 0)
    push_stats(r);
  return n;
}

void srtin_close(srtin_t *r) {
  if (!r)
    return;
  if (r->sock != SRT_INVALID_SOCK)
    srt_close(r->sock);
  for (int i = 0; i < r->n_listeners; i++)
    srt_close(r->listeners[i]);
  srt_cleanup();
  free(r);
}
