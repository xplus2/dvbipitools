/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <srt/srt.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "lib/net/srt/srtcommon.h"
#include "lib/net/srt/srtout.h"
#include "lib/net/srt/srtout/srtout_int.h"

static int target_count(const srtout_cfg_t *cfg) { return cfg->npeers + (cfg->rendezvous ? 1 : 0); }

static void target_peers(const srtout_cfg_t *cfg, srtcommon_peer_t *out) {
  for (int i = 0; i < cfg->npeers; i++) out[i] = cfg->peers[i];
  if (cfg->rendezvous) {
    out[cfg->npeers].host = cfg->local_host;
    out[cfg->npeers].port = cfg->local_port;
  }
}

static int resolve_blocking(const srtout_cfg_t *cfg, targets_t *t) {
  srtcommon_peer_t pe[SRTCOMMON_MAX_PEERS + 1];
  int n = target_count(cfg);

  target_peers(cfg, pe);
  for (int i = 0; i < n; i++)
    if (srtcommon_resolve(pe[i].host, pe[i].port, &t->ss[i], &t->len[i])) return -1;
  return 0;
}

/* 0 ready, 1 pending, -1 failed */
static int resolve_async(srtout_t *r, targets_t *t) {
  srtcommon_peer_t pe[SRTCOMMON_MAX_PEERS + 1];
  int n = target_count(&r->cfg);
  int rc = 0;

  if (!r->reconnect_res) {
    target_peers(&r->cfg, pe);
    r->reconnect_res = srtcommon_resolver_start(pe, n);
    if (!r->reconnect_res) return -1;
  }
  if (!srtcommon_resolver_poll(r->reconnect_res)) return 1;
  for (int i = 0; i < n && !rc; i++)
    if (srtcommon_resolver_get(r->reconnect_res, i, &t->ss[i], &t->len[i])) rc = -1;
  srtcommon_resolver_free(r->reconnect_res);
  r->reconnect_res = NULL;
  return rc;
}

/* RCVSYN/SNDSYN off: connect/rendezvous returns immediately, EASYNCSND means in progress */
static SRTSOCKET open_single(const srtout_cfg_t *cfg, const targets_t *t) {
  SRTSOCKET s;
  const struct sockaddr *remote = (const struct sockaddr *)&t->ss[0];
  int no = 0;

  s = srt_create_socket();
  if (s == SRT_INVALID_SOCK) {
    log_code("LE500159", "srt: socket create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_apply_opts(s, &cfg->opts, 0, SRTO_SNDTIMEO, SRTOUT_SNDTIMEO_MS) ||
      srt_setsockopt(s, 0, SRTO_RCVSYN, &no, sizeof no) != 0 || srt_setsockopt(s, 0, SRTO_SNDSYN, &no, sizeof no) != 0) {
    srt_close(s);
    return SRT_INVALID_SOCK;
  }
  if (cfg->rendezvous) {
    if (srt_rendezvous(s, (const struct sockaddr *)&t->ss[1], t->len[1], remote, t->len[0]) != 0 &&
        srt_getlasterror(NULL) != SRT_EASYNCSND) {
      log_code("LE600136", "srt: rendezvous failed: %s", srt_getlasterror_str());
      srt_close(s);
      return SRT_INVALID_SOCK;
    }
  } else if (srt_connect(s, remote, t->len[0]) != 0 && srt_getlasterror(NULL) != SRT_EASYNCSND) {
    log_code("LE600137", "srt: connect failed: %s", srt_getlasterror_str());
    srt_close(s);
    return SRT_INVALID_SOCK;
  }
  return s;
}

static SRTSOCKET open_group(const srtout_cfg_t *cfg, const targets_t *t) {
  SRTSOCKET g;
  SRT_GROUP_TYPE type = cfg->group_mode == SRTGROUP_BACKUP ? SRT_GTYPE_BACKUP : SRT_GTYPE_BROADCAST;
  SRT_SOCKGROUPCONFIG gc[SRTCOMMON_MAX_PEERS];
  int no = 0;

  g = srt_create_group(type);
  if (g == SRT_INVALID_SOCK) {
    log_code("LE500164", "srt: group create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_apply_opts(g, &cfg->opts, 1, SRTO_SNDTIMEO, SRTOUT_SNDTIMEO_MS) ||
      srt_setsockopt(g, 0, SRTO_RCVSYN, &no, sizeof no) != 0 || srt_setsockopt(g, 0, SRTO_SNDSYN, &no, sizeof no) != 0) {
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  for (int i = 0; i < cfg->npeers; i++)
    gc[i] = srt_prepare_endpoint(NULL, (const struct sockaddr *)&t->ss[i], t->len[i]);
  if (srt_connect_group(g, gc, cfg->npeers) == SRT_ERROR && srt_getlasterror(NULL) != SRT_EASYNCSND) {
    log_code("LE600139", "srt: group connect failed: %s", srt_getlasterror_str());
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  return g;
}

/* (re)starts a connect attempt: creates socket/group, kicks off non-blocking
   connect, registers it with r's epoll set. blocking: lookups inline (startup only).
   else lookups run async, 1 = still pending, call again. 0 ok, -1 error (nothing changed) */
int start_connect(srtout_t *r, int blocking) {
  targets_t t;
  SRTSOCKET s;
  int events = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
  int rc;

  if (r->cfg.rendezvous && !r->cfg.local_host) {
    log_code("LE000321", "srt: rendezvous requires a local bind address");
    return -1;
  }
  rc = blocking ? resolve_blocking(&r->cfg, &t) : resolve_async(r, &t);
  if (rc)
    return rc;
  s = r->cfg.group_mode == SRTGROUP_NONE ? open_single(&r->cfg, &t) : open_group(&r->cfg, &t);
  if (s == SRT_INVALID_SOCK)
    return -1;
  if (srt_epoll_add_usock(r->eid, s, &events) != 0) {
    log_code("LE500161", "srt: epoll add failed: %s", srt_getlasterror_str());
    srt_close(s);
    return -1;
  }
  r->sock = s;
  r->connected = 0;
  return 0;
}

/* srt_close() deregisters socket from r->eid itself: explicit remove_usock first would race it
   + log spurious internal SRT error */
void teardown_for_reconnect(srtout_t *r, const char *why) {
  log_code("LW600145", "srt: %s, reconnecting", why);
  srt_close(r->sock);
  r->sock = SRT_INVALID_SOCK;
  r->connected = 0;
  r->next_reconnect_at = mono_seconds() + SRTOUT_RECONNECT_BACKOFF_S;
}

srtout_t *srtout_open(const srtout_cfg_t *cfg) {
  srtout_t *r;
  sbuf_t pl;

  if (cfg->npeers <= 0 || cfg->npeers > SRTCOMMON_MAX_PEERS) {
    log_code("LE000322", "srt: invalid peer count %d", cfg->npeers);
    return NULL;
  }
  if (cfg->group_mode == SRTGROUP_NONE && cfg->npeers != 1) {
    log_code("LE000323", "srt: plain connection needs exactly one peer, got %d", cfg->npeers);
    return NULL;
  }
  if (cfg->group_mode != SRTGROUP_NONE && cfg->rendezvous) {
    log_code("LE000324", "srt: rendezvous cannot be combined with bonded groups");
    return NULL;
  }

  if (srtcommon_startup() != 0) {
    log_code("LE500166", "srt: startup failed: %s", srt_getlasterror_str());
    return NULL;
  }
  srtcommon_open_logging(cfg->verbose);

  r = calloc(1, sizeof *r);
  if (!r) {
    srtcommon_cleanup();
    return NULL;
  }
  r->sock = SRT_INVALID_SOCK;
  r->eid = srt_epoll_create();
  if (r->eid == SRT_ERROR) {
    log_code("LE500160", "srt: epoll create failed: %s", srt_getlasterror_str());
    free(r);
    srtcommon_cleanup();
    return NULL;
  }
  r->mx = cfg->mx;
  r->tool_version = cfg->tool_version;
  r->cfg = *cfg;
  r->safety_mult = cfg->safety_mult ? cfg->safety_mult : SRTOUT_SAFETY_MULT_DEFAULT;
  r->queue_metrics = cfg->queue_metrics;
  r->note_enqueue = cfg->queue_metrics == SRT_QUEUE_METRICS_FULL ? &note_enqueue_hwm : &note_enqueue_off;
  if (r->safety_mult > SRTOUT_SAFETY_MULT_MAX) r->safety_mult = SRTOUT_SAFETY_MULT_MAX;
  sbuf_init(&pl, r->peer_label, sizeof r->peer_label);
  sbuf_add(&pl, cfg->peers[0].host);
  sbuf_add(&pl, ":");
  sbuf_add_uint(&pl, cfg->peers[0].port);

  r->pending_cap = SRTOUT_PENDING_FLOOR_CHUNKS;
  r->pending = malloc((size_t)r->pending_cap * SRTOUT_PLSIZE);
  r->pending_len = malloc((size_t)r->pending_cap * sizeof *r->pending_len);
  if (!r->pending || !r->pending_len) {
    free(r->pending);
    free(r->pending_len);
    srt_epoll_release(r->eid);
    free(r);
    srtcommon_cleanup();
    return NULL;
  }

  if (start_connect(r, 1) != 0) {
    free(r->pending);
    free(r->pending_len);
    srt_epoll_release(r->eid);
    free(r);
    srtcommon_cleanup();
    return NULL;
  }
  return r;
}
