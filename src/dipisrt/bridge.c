/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <srt/srt.h>

#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/net/srtout.h"
#include "lib/net/tssink.h"
#include "lib/net/tssource.h"
#include "lib/signal.h"

#include "args.h"
#include "bridge.h"
#include "version.h"

#define SRT_RCVTIMEO_MS 200
#define DIPISRT_SENDER_DRAIN_MS_DEFAULT 1000 /* srt's own default latency buffer */

static void srt_log_cb(void *opaque, int level, const char *file, int line, const char *area, const char *message) {
  (void)opaque;
  (void)level;
  (void)file;
  (void)line;
  (void)area;
  log_line("srt: %s", message);
}

static void open_logging(int verbose) {
  srt_setloglevel(verbose ? LOG_DEBUG : LOG_WARNING);
  srt_setloghandler(NULL, srt_log_cb);
}

socklen_t addr_len(int family) {
  return family == AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
}

/* host: numeric IP only, already validated by args.c's argutil_addrport_parse() */
void build_addr(int family, const char *host, unsigned port, struct sockaddr_storage *ss) {
  memset(ss, 0, sizeof *ss);
  if (family == AF_INET6) {
    struct sockaddr_in6 *a = (struct sockaddr_in6 *)ss;
    a->sin6_family = AF_INET6;
    a->sin6_port = htons((uint16_t)port);
    inet_pton(AF_INET6, host, &a->sin6_addr);
  } else {
    struct sockaddr_in *a = (struct sockaddr_in *)ss;
    a->sin_family = AF_INET;
    a->sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, host, &a->sin_addr);
  }
}

srtout_group_mode_t group_mode_of(srt_group_sel_t g) {
  switch (g) {
  case SRT_GROUP_BROADCAST:
    return SRTOUT_GROUP_BROADCAST;
  case SRT_GROUP_BACKUP:
    return SRTOUT_GROUP_BACKUP;
  default:
    return SRTOUT_GROUP_NONE;
  }
}

void nonsrt_to_tssrc_cfg(const nonsrt_t *s, const char *iface, int insecure_tls, tssrc_cfg_t *tc) {
  memset(tc, 0, sizeof *tc);
  tc->user_agent = TOOL_NAME "/" TOOL_VERSION;
  switch (s->kind) {
  case NONSRT_HTTP:
    tc->kind = TSSRC_HTTP;
    tc->http = s->http;
    tc->insecure_tls = insecure_tls;
    break;
  case NONSRT_FILE:
    if (s->file_path[0]) {
      tc->kind = TSSRC_FILE;
      tc->file_path = s->file_path;
    } else {
      tc->kind = TSSRC_STDIN;
    }
    break;
  case NONSRT_RTP:
  case NONSRT_UDP:
    tc->kind = (s->kind == NONSRT_RTP) ? TSSRC_RTP : TSSRC_UDP;
    tc->family = s->family;
    tc->group = s->group;
    tc->port = s->port;
    tc->iface = iface;
    break;
  }
}

void nonsrt_to_tssink_cfg(const nonsrt_t *s, const char *iface, tssink_cfg_t *tk) {
  memset(tk, 0, sizeof *tk);
  if (s->kind == NONSRT_FILE) {
    if (s->file_path[0]) {
      tk->kind = TSSINK_FILE;
      tk->file_path = s->file_path;
    } else {
      tk->kind = TSSINK_STDOUT;
    }
  } else {
    tk->kind = (s->kind == NONSRT_RTP) ? TSSINK_RTP : TSSINK_UDP;
    tk->family = s->family;
    tk->group = s->group;
    tk->port = s->port;
    tk->iface = iface;
  }
}

/* is_group skips TRANSTYPE: rejected on group sockets, live forced already. plain
   GROUPCONNECT listener: not a group yet, still gets it. 0 ok, -1 error */
static int apply_common_opts(SRTSOCKET s, const config_t *cfg, int is_group) {
  int transtype = SRTT_LIVE;
  int timeo = SRT_RCVTIMEO_MS;

  if (!is_group && srt_setsockopt(s, 0, SRTO_TRANSTYPE, &transtype, sizeof transtype) != 0)
    goto fail;
  if (cfg->passphrase[0]) {
    if (srt_setsockopt(s, 0, SRTO_PASSPHRASE, cfg->passphrase, (int)strlen(cfg->passphrase)) != 0)
      goto fail;
    if (cfg->pbkeylen) {
      int kl = cfg->pbkeylen;
      if (srt_setsockopt(s, 0, SRTO_PBKEYLEN, &kl, sizeof kl) != 0)
        goto fail;
    }
  }
  if (cfg->streamid[0] && srt_setsockopt(s, 0, SRTO_STREAMID, cfg->streamid, (int)strlen(cfg->streamid)) != 0)
    goto fail;
  if (cfg->packetfilter[0] && srt_setsockopt(s, 0, SRTO_PACKETFILTER, cfg->packetfilter, (int)strlen(cfg->packetfilter)) != 0)
    goto fail;
  if (cfg->latency_ms) {
    int lat = (int)cfg->latency_ms;
    if (srt_setsockopt(s, 0, SRTO_LATENCY, &lat, sizeof lat) != 0)
      goto fail;
  }
  if (srt_setsockopt(s, 0, SRTO_RCVTIMEO, &timeo, sizeof timeo) != 0)
    goto fail;
  return 0;

fail:
  log_line("srt: setsockopt failed: %s", srt_getlasterror_str());
  return -1;
}

static SRTSOCKET open_single_caller(const config_t *cfg) {
  SRTSOCKET s;
  struct sockaddr_storage remote;

  s = srt_create_socket();
  if (s == SRT_INVALID_SOCK) {
    log_line("srt: socket create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (apply_common_opts(s, cfg, 0)) {
    srt_close(s);
    return SRT_INVALID_SOCK;
  }
  build_addr(cfg->in.family[0], cfg->in.srt_host[0], cfg->in.srt_port[0], &remote);

  if (cfg->rendezvous) {
    struct sockaddr_storage local;
    build_addr(cfg->local_family, cfg->local_host, cfg->local_port, &local);
    if (srt_rendezvous(s, (struct sockaddr *)&local, (int)addr_len(cfg->local_family), (struct sockaddr *)&remote,
                        (int)addr_len(cfg->in.family[0])) != 0) {
      log_line("srt: rendezvous failed: %s", srt_getlasterror_str());
      srt_close(s);
      return SRT_INVALID_SOCK;
    }
  } else if (srt_connect(s, (struct sockaddr *)&remote, (int)addr_len(cfg->in.family[0])) != 0) {
    log_line("srt: connect failed: %s", srt_getlasterror_str());
    srt_close(s);
    return SRT_INVALID_SOCK;
  }
  return s;
}

/* accepted socket shares listener's UDP multiplexer. early close: kills connection too.
   keep listener open until final teardown. lsn_out: carries it out */
static SRTSOCKET open_single_listener(const config_t *cfg, SRTSOCKET *lsn_out) {
  SRTSOCKET lsn, s = SRT_INVALID_SOCK;
  struct sockaddr_storage local;

  *lsn_out = SRT_INVALID_SOCK;
  lsn = srt_create_socket();
  if (lsn == SRT_INVALID_SOCK) {
    log_line("srt: socket create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (apply_common_opts(lsn, cfg, 0)) {
    srt_close(lsn);
    return SRT_INVALID_SOCK;
  }
  build_addr(cfg->in.family[0], cfg->in.srt_host[0], cfg->in.srt_port[0], &local);
  if (srt_bind(lsn, (struct sockaddr *)&local, (int)addr_len(cfg->in.family[0])) != 0 || srt_listen(lsn, 1) != 0) {
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

static SRTSOCKET open_group_caller(const config_t *cfg) {
  SRTSOCKET g;
  SRT_GROUP_TYPE type = cfg->group_mode == SRT_GROUP_BACKUP ? SRT_GTYPE_BACKUP : SRT_GTYPE_BROADCAST;
  SRT_SOCKGROUPCONFIG gc[DIPISRT_MAX_PEERS];

  g = srt_create_group(type);
  if (g == SRT_INVALID_SOCK) {
    log_line("srt: group create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (apply_common_opts(g, cfg, 1)) {
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  for (int i = 0; i < cfg->in.n_srt; i++) {
    struct sockaddr_storage remote;
    build_addr(cfg->in.family[i], cfg->in.srt_host[i], cfg->in.srt_port[i], &remote);
    gc[i] = srt_prepare_endpoint(NULL, (struct sockaddr *)&remote, (int)addr_len(cfg->in.family[i]));
  }
  if (srt_connect_group(g, gc, cfg->in.n_srt) == SRT_ERROR) {
    log_line("srt: group connect failed: %s", srt_getlasterror_str());
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  return g;
}

/* one listener per bonded peer; accept_bond returns first hello. GROUPCONNECT: accept
   returns group id, not socket. lifetime rule: see open_single_listener() */
static SRTSOCKET open_group_listener(const config_t *cfg, SRTSOCKET *listeners_out, int *n_listeners_out) {
  SRTSOCKET listeners[DIPISRT_MAX_PEERS];
  int groupconnect = 1;
  SRTSOCKET s = SRT_INVALID_SOCK;
  int i;

  *n_listeners_out = 0;
  for (i = 0; i < cfg->in.n_srt; i++) {
    struct sockaddr_storage local;
    listeners[i] = srt_create_socket();
    if (listeners[i] == SRT_INVALID_SOCK) {
      log_line("srt: socket create failed: %s", srt_getlasterror_str());
      goto fail;
    }
    if (apply_common_opts(listeners[i], cfg, 0) ||
        srt_setsockopt(listeners[i], 0, SRTO_GROUPCONNECT, &groupconnect, sizeof groupconnect) != 0) {
      log_line("srt: setsockopt failed: %s", srt_getlasterror_str());
      goto fail;
    }
    build_addr(cfg->in.family[i], cfg->in.srt_host[i], cfg->in.srt_port[i], &local);
    if (srt_bind(listeners[i], (struct sockaddr *)&local, (int)addr_len(cfg->in.family[i])) != 0 ||
        srt_listen(listeners[i], 1) != 0) {
      log_line("srt: bind/listen failed on peer %d: %s", i, srt_getlasterror_str());
      goto fail;
    }
  }
  while (!signal_stop_requested()) {
    s = srt_accept_bond(listeners, cfg->in.n_srt, SRT_RCVTIMEO_MS);
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

/* listeners_out/n_listeners_out: sockets outliving this call, see above. caller flavors:
   empty, nothing to keep alive */
static SRTSOCKET open_srt_input(const config_t *cfg, SRTSOCKET *listeners_out, int *n_listeners_out) {
  *n_listeners_out = 0;
  if (cfg->group_mode != SRT_GROUP_NONE)
    return cfg->in.listen ? open_group_listener(cfg, listeners_out, n_listeners_out) : open_group_caller(cfg);
  if (cfg->in.listen) {
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

static void push_receiver_stats(SRTSOCKET s, metrics_exporter_t *mx) {
  SRT_TRACEBSTATS st;
  metrics_writer_t w;

  if (!mx || !metrics_exporter_due(mx, mono_seconds()) || srt_bstats(s, &st, 0) != 0)
    return;
  if (metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;
  metrics_writer_put(&w, METRICS_ID_SRT_RECEIVER_RECEIVED_TOTAL, NULL, (uint64_t)st.pktRecvTotal);
  metrics_writer_put(&w, METRICS_ID_SRT_RECEIVER_LOST_TOTAL, NULL, (uint64_t)st.pktRcvLossTotal);
  metrics_writer_put(&w, METRICS_ID_SRT_RECEIVER_DROPPED_TOTAL, NULL, (uint64_t)st.pktRcvDropTotal);
  metrics_writer_put(&w, METRICS_ID_SRT_RECEIVER_RTT_MILLISECONDS, NULL, (uint64_t)st.msRTT);
  metrics_writer_put(&w, METRICS_ID_SRT_RECEIVER_BUFFER_MILLISECONDS, NULL, (uint64_t)st.msRcvTsbPdDelay);
  metrics_exporter_send(mx, &w);
}

static int run_sender(const config_t *cfg, metrics_exporter_t *mx) {
  tssrc_cfg_t tc;
  tssrc_t *src;
  srtout_cfg_t rcfg;
  srtout_t *srt;
  unsigned char buf[65536];
  int rc = 0;

  nonsrt_to_tssrc_cfg(&cfg->in.nonsrt, cfg->iface, cfg->insecure_tls, &tc);
  src = tssrc_open(&tc, NULL);
  if (!src)
    return 1;

  memset(&rcfg, 0, sizeof rcfg);
  for (int i = 0; i < cfg->out.n_srt; i++) {
    rcfg.peers[i].host = cfg->out.srt_host[i];
    rcfg.peers[i].port = cfg->out.srt_port[i];
  }
  rcfg.npeers = cfg->out.n_srt;
  rcfg.group_mode = group_mode_of(cfg->group_mode);
  rcfg.rendezvous = cfg->rendezvous;
  rcfg.local_host = cfg->local_host[0] ? cfg->local_host : NULL;
  rcfg.local_port = cfg->local_port;
  rcfg.passphrase = cfg->passphrase[0] ? cfg->passphrase : NULL;
  rcfg.pbkeylen = cfg->pbkeylen;
  rcfg.streamid = cfg->streamid[0] ? cfg->streamid : NULL;
  rcfg.packetfilter = cfg->packetfilter[0] ? cfg->packetfilter : NULL;
  rcfg.latency_ms = cfg->latency_ms;
  rcfg.verbose = cfg->verbose;
  rcfg.mx = mx;
  rcfg.tool_version = TOOL_VERSION;

  srt = srtout_open(&rcfg);
  if (!srt) {
    tssrc_close(src);
    return 1;
  }

  {
    int had_error = 0;

    while (!signal_stop_requested()) {
      net_err_reason_t reason = NET_ERR_OTHER;
      ssize_t n = tssrc_read(src, buf, sizeof buf, &reason);

      if (n < 0) {
        rc = 1;
        break;
      }
      if (n == 0)
        continue;
      if (srtout_write(srt, buf, (size_t)n) < 0) {
        if (!had_error) {
          log_line("srt output: write failed, will keep retrying");
          had_error = 1;
        }
      } else if (had_error) {
        log_line("srt output: recovered");
        had_error = 0;
      }
    }
  }
  if (rc) {
    /* eof/err, not live stop: drain srt retransmits before teardown */
    unsigned drain_ms = cfg->latency_ms ? cfg->latency_ms : DIPISRT_SENDER_DRAIN_MS_DEFAULT;
    struct timespec ts = {(time_t)(drain_ms / 1000), (long)(drain_ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
  }
  srtout_close(srt);
  tssrc_close(src);
  return rc;
}

#define RECV_DEDUP_HISTORY 2 /* reconnect can redeliver a couple already-written chunks */

static int run_receiver(const config_t *cfg, metrics_exporter_t *mx) {
  tssink_cfg_t tk;
  tssink_t *sink;
  SRTSOCKET s;
  SRTSOCKET listeners[DIPISRT_MAX_PEERS];
  int n_listeners;
  unsigned char buf[65536];
  unsigned char dedup_hist[RECV_DEDUP_HISTORY][65536];
  int dedup_hist_len[RECV_DEDUP_HISTORY] = {0};
  int dedup_hist_next = 0;
  int dedup_active = 0; /* set on reconnect, cleared at first non-duplicate chunk */
  int rc = 0;

  nonsrt_to_tssink_cfg(&cfg->out.nonsrt, cfg->iface, &tk);
  sink = tssink_open(&tk);
  if (!sink)
    return 1;

  if (srt_startup() == SRT_ERROR) {
    log_line("srt: startup failed: %s", srt_getlasterror_str());
    tssink_close(sink);
    return 1;
  }
  open_logging(cfg->verbose);

  s = open_srt_input(cfg, listeners, &n_listeners);
  if (s == SRT_INVALID_SOCK) {
    srt_cleanup();
    tssink_close(sink);
    return signal_stop_requested() ? 0 : 1;
  }

  while (!signal_stop_requested()) {
    int n = srt_recvmsg2(s, (char *)buf, sizeof buf, NULL);

    if (n == SRT_ERROR) {
      int err = srt_getlasterror(NULL);
      if (err == SRT_ETIMEOUT)
        continue;
      if (err == SRT_EASYNCRCV) {
        /* group with every member link down right now, not a dead group: retry */
        usleep(SRT_RCVTIMEO_MS * 1000);
        continue;
      }
      if (cfg->group_mode != SRT_GROUP_NONE && n_listeners > 0) {
        /* group error: state could be corrupted, reconnect regardless of cause */
        log_line("srt: read failed on group (%s), reconnecting", srt_getlasterror_str());
        srt_close(s);
        s = SRT_INVALID_SOCK;
        while (!signal_stop_requested() && s == SRT_INVALID_SOCK) {
          s = srt_accept_bond(listeners, n_listeners, SRT_RCVTIMEO_MS);
          if (s == SRT_INVALID_SOCK && srt_getlasterror(NULL) != SRT_ETIMEOUT)
            log_line("srt: reconnect attempt failed: %s", srt_getlasterror_str());
        }
        if (s == SRT_INVALID_SOCK)
          break;
        dedup_active = 1;
        continue;
      }
      log_line("srt: read failed: %s", srt_getlasterror_str());
      rc = 1;
      break;
    }
    if (n == 0)
      continue;
    if (dedup_active) {
      int is_dup = 0;
      for (int h = 0; h < RECV_DEDUP_HISTORY; h++) {
        if (dedup_hist_len[h] == n && memcmp(buf, dedup_hist[h], (size_t)n) == 0) {
          is_dup = 1;
          break;
        }
      }
      if (is_dup)
        continue;
      dedup_active = 0;
    }
    if (tssink_write(sink, buf, (size_t)n) < 0) {
      rc = 1;
      break;
    }
    push_receiver_stats(s, mx);
    memcpy(dedup_hist[dedup_hist_next], buf, (size_t)n);
    dedup_hist_len[dedup_hist_next] = n;
    dedup_hist_next = (dedup_hist_next + 1) % RECV_DEDUP_HISTORY;
  }

  srt_close(s);
  for (int i = 0; i < n_listeners; i++)
    srt_close(listeners[i]);
  srt_cleanup();
  tssink_close(sink);
  return rc;
}

int bridge_run(const config_t *cfg, metrics_exporter_t *mx) {
  return config_is_sender(cfg) ? run_sender(cfg, mx) : run_receiver(cfg, mx);
}
