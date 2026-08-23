/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include <srt/srt.h>

#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/signal.h"

#include "srtout.h"

#define SRTOUT_SNDTIMEO_MS 1000
#define SRTOUT_CLOSE_DRAIN_MAX_MS 3000 /* cap on drain_before_close's wait */
#define SRTOUT_CLOSE_MIN_WAIT_MS 1000  /* floor on drain_before_close's wait, see there */

struct srtout {
  SRTSOCKET sock; /* plain connection socket or group id */
  metrics_exporter_t *mx;
  const char *tool_version;
  char peer_label[64];
  srtout_cfg_t cfg; /* kept for reconnect on group loss, group mode only */
};

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

/* SOCK_DGRAM: addrinfo family/form only. UDP socket itself: libsrt's own */
static int resolve_addr(const char *host, unsigned port, struct sockaddr_storage *ss, int *len) {
  struct addrinfo hints, *res;
  char portbuf[6];
  int rc;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  snprintf(portbuf, sizeof portbuf, "%u", port);
  rc = getaddrinfo(host, portbuf, &hints, &res);
  if (rc != 0) {
    log_line("srt: address resolve failed for %s:%u: %s", host, port, gai_strerror(rc));
    return -1;
  }
  memcpy(ss, res->ai_addr, res->ai_addrlen);
  *len = (int)res->ai_addrlen;
  freeaddrinfo(res);
  return 0;
}

/* is_group skips TRANSTYPE: rejected on group sockets, live already forced. 0 ok, -1 error */
static int apply_common_opts(SRTSOCKET s, const srtout_cfg_t *cfg, int is_group) {
  int transtype = SRTT_LIVE;
  int timeo = SRTOUT_SNDTIMEO_MS;

  if (!is_group && srt_setsockopt(s, 0, SRTO_TRANSTYPE, &transtype, sizeof transtype) != 0)
    goto fail;
  if (cfg->passphrase && cfg->passphrase[0]) {
    if (srt_setsockopt(s, 0, SRTO_PASSPHRASE, cfg->passphrase, (int)strlen(cfg->passphrase)) != 0)
      goto fail;
    if (cfg->pbkeylen) {
      int kl = cfg->pbkeylen;
      if (srt_setsockopt(s, 0, SRTO_PBKEYLEN, &kl, sizeof kl) != 0)
        goto fail;
    }
  }
  if (cfg->streamid && cfg->streamid[0] &&
      srt_setsockopt(s, 0, SRTO_STREAMID, cfg->streamid, (int)strlen(cfg->streamid)) != 0)
    goto fail;
  if (cfg->packetfilter && cfg->packetfilter[0] &&
      srt_setsockopt(s, 0, SRTO_PACKETFILTER, cfg->packetfilter, (int)strlen(cfg->packetfilter)) != 0)
    goto fail;
  if (cfg->latency_ms) {
    int lat = (int)cfg->latency_ms;
    if (srt_setsockopt(s, 0, SRTO_LATENCY, &lat, sizeof lat) != 0)
      goto fail;
  }
  if (srt_setsockopt(s, 0, SRTO_SNDTIMEO, &timeo, sizeof timeo) != 0)
    goto fail;
  return 0;

fail:
  log_line("srt: setsockopt failed: %s", srt_getlasterror_str());
  return -1;
}

static SRTSOCKET open_single(const srtout_cfg_t *cfg) {
  SRTSOCKET s;
  struct sockaddr_storage remote, local;
  int remote_len, local_len;

  if (resolve_addr(cfg->peers[0].host, cfg->peers[0].port, &remote, &remote_len))
    return SRT_INVALID_SOCK;

  s = srt_create_socket();
  if (s == SRT_INVALID_SOCK) {
    log_line("srt: socket create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (apply_common_opts(s, cfg, 0)) {
    srt_close(s);
    return SRT_INVALID_SOCK;
  }

  if (cfg->rendezvous) {
    if (!cfg->local_host) {
      log_line("srt: rendezvous requires a local bind address");
      srt_close(s);
      return SRT_INVALID_SOCK;
    }
    if (resolve_addr(cfg->local_host, cfg->local_port, &local, &local_len)) {
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

static SRTSOCKET open_group(const srtout_cfg_t *cfg) {
  SRTSOCKET g;
  SRT_GROUP_TYPE type = cfg->group_mode == SRTOUT_GROUP_BACKUP ? SRT_GTYPE_BACKUP : SRT_GTYPE_BROADCAST;
  SRT_SOCKGROUPCONFIG gc[SRTOUT_MAX_PEERS];

  g = srt_create_group(type);
  if (g == SRT_INVALID_SOCK) {
    log_line("srt: group create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (apply_common_opts(g, cfg, 1)) {
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  for (int i = 0; i < cfg->npeers; i++) {
    struct sockaddr_storage remote;
    int remote_len;

    if (resolve_addr(cfg->peers[i].host, cfg->peers[i].port, &remote, &remote_len)) {
      srt_close(g);
      return SRT_INVALID_SOCK;
    }
    gc[i] = srt_prepare_endpoint(NULL, (struct sockaddr *)&remote, remote_len);
  }
  if (srt_connect_group(g, gc, cfg->npeers) == SRT_ERROR) {
    log_line("srt: group connect failed: %s", srt_getlasterror_str());
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  return g;
}

srtout_t *srtout_open(const srtout_cfg_t *cfg) {
  srtout_t *r;
  SRTSOCKET s;

  if (cfg->npeers <= 0 || cfg->npeers > SRTOUT_MAX_PEERS) {
    log_line("srt: invalid peer count %d", cfg->npeers);
    return NULL;
  }
  if (cfg->group_mode == SRTOUT_GROUP_NONE && cfg->npeers != 1) {
    log_line("srt: plain connection needs exactly one peer, got %d", cfg->npeers);
    return NULL;
  }
  if (cfg->group_mode != SRTOUT_GROUP_NONE && cfg->rendezvous) {
    log_line("srt: rendezvous cannot be combined with bonded groups");
    return NULL;
  }

  if (srt_startup() == SRT_ERROR) {
    log_line("srt: startup failed: %s", srt_getlasterror_str());
    return NULL;
  }
  open_logging(cfg->verbose);

  s = cfg->group_mode == SRTOUT_GROUP_NONE ? open_single(cfg) : open_group(cfg);
  if (s == SRT_INVALID_SOCK) {
    srt_cleanup();
    return NULL;
  }

  r = calloc(1, sizeof *r);
  if (!r) {
    srt_close(s);
    srt_cleanup();
    return NULL;
  }
  r->sock = s;
  r->mx = cfg->mx;
  r->tool_version = cfg->tool_version;
  r->cfg = *cfg;
  snprintf(r->peer_label, sizeof r->peer_label, "%s:%u", cfg->peers[0].host, cfg->peers[0].port);
  return r;
}

/* stats push covers whole connection/group, not per bonded member */
static void push_stats(srtout_t *r) {
  SRT_TRACEBSTATS st;
  metrics_writer_t w;

  if (!r->mx || !metrics_exporter_due(r->mx, mono_seconds()) || srt_bstats(r->sock, &st, 0) != 0)
    return;
  if (metrics_exporter_begin(r->mx, &w, r->tool_version))
    return;
  metrics_writer_put(&w, METRICS_ID_SRT_SENDER_SENT_TOTAL, r->peer_label, (uint64_t)st.pktSentTotal);
  metrics_writer_put(&w, METRICS_ID_SRT_SENDER_RETRANSMITTED_TOTAL, r->peer_label, (uint64_t)st.pktRetransTotal);
  metrics_writer_put(&w, METRICS_ID_SRT_SENDER_RTT_MILLISECONDS, r->peer_label, (uint64_t)st.msRTT);
  metrics_writer_put(&w, METRICS_ID_SRT_SENDER_LOST_TOTAL, r->peer_label, (uint64_t)st.pktSndLossTotal);
  metrics_writer_put(&w, METRICS_ID_SRT_SENDER_DROPPED_TOTAL, r->peer_label, (uint64_t)st.pktSndDropTotal);
  metrics_exporter_send(r->mx, &w);
}

int srtout_write(srtout_t *r, const unsigned char *buf, size_t n) {
  if (r->sock == SRT_INVALID_SOCK) {
    /* prior reconnect failed, retry attempted each call */
    r->sock = open_group(&r->cfg);
    if (r->sock == SRT_INVALID_SOCK)
      return -1;
  }
  for (size_t off = 0; off < n; off += (size_t)SRT_LIVE_DEF_PLSIZE) {
    size_t chunk = n - off < (size_t)SRT_LIVE_DEF_PLSIZE ? n - off : (size_t)SRT_LIVE_DEF_PLSIZE;

    if (srt_sendmsg2(r->sock, (const char *)(buf + off), (int)chunk, NULL) == SRT_ERROR) {
      if (r->cfg.group_mode != SRTOUT_GROUP_NONE) {
        /* any send error on a group socket: state may be corrupted, not just
           disconnected. never keep writing to it, reconnect on next call */
        log_line("srt: write failed on group (%s), reconnecting", srt_getlasterror_str());
        srt_close(r->sock);
        r->sock = open_group(&r->cfg);
        return -1;
      }
      log_line("srt: write failed: %s", srt_getlasterror_str());
      return -1;
    }
  }
  push_stats(r);
  return 0;
}

/* sndbuffer empty != delivered to peer app. measured: single small msg, ~1s real wait needed.
   floor: max(MIN_WAIT_MS, peer latency), capped at DRAIN_MAX_MS */
static void drain_before_close(SRTSOCKET sock) {
  int waited_ms = 0;
  size_t blocks, bytes;
  int peer_latency = 0;
  int optlen = sizeof peer_latency;
  int floor_ms = SRTOUT_CLOSE_MIN_WAIT_MS;

  while (srt_getsndbuffer(sock, &blocks, &bytes) == 0 && blocks > 0 && waited_ms < SRTOUT_CLOSE_DRAIN_MAX_MS) {
    struct timespec ts = {0, 20L * 1000000L};
    nanosleep(&ts, NULL);
    waited_ms += 20;
  }
  if (srt_getsockopt(sock, 0, SRTO_PEERLATENCY, &peer_latency, &optlen) == 0 && peer_latency > floor_ms)
    floor_ms = peer_latency;
  if (floor_ms > SRTOUT_CLOSE_DRAIN_MAX_MS)
    floor_ms = SRTOUT_CLOSE_DRAIN_MAX_MS;
  if (floor_ms > waited_ms) {
    struct timespec ts;
    int remain_ms = floor_ms - waited_ms;
    ts.tv_sec = remain_ms / 1000;
    ts.tv_nsec = (long)(remain_ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
  }
}

void srtout_close(srtout_t *r) {
  if (!r)
    return;
  drain_before_close(r->sock);
  srt_close(r->sock);
  srt_cleanup();
  free(r);
}
