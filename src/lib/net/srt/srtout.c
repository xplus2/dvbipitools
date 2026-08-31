/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include <srt/srt.h>

#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include "srtcommon.h"
#include "srtout.h"

#define SRTOUT_SNDTIMEO_MS 1000
#define SRTOUT_CLOSE_DRAIN_MAX_MS 3000 /* cap on drain_before_close's wait */
#define SRTOUT_CLOSE_MIN_WAIT_MS 1000  /* floor on drain_before_close's wait, see there */
#define SRTOUT_RECONNECT_BACKOFF_S 0.2 /* min gap between failed (re)connect attempts */
#define SRTOUT_PLSIZE 1316             /* srt.h's SRT_LIVE_DEF_PLSIZE, but real constant expr for array sizing */

#define SRTOUT_PENDING_FLOOR_CHUNKS 64        /* never below this, regardless of bitrate/RAM: ~84KB */
#define SRTOUT_SAFETY_MULT_DEFAULT 4          /* --send-buffer-mult unset: latency-window multiples to buffer */
#define SRTOUT_SAFETY_MULT_MAX 32
#define SRTOUT_DEFAULT_LATENCY_MS 120         /* SRT_LIVE_DEF_LATENCY, fallback for opts.latency_ms 0 */
#define SRTOUT_INITIAL_BITRATE_BPS (100000000 / 8) /* generous startup guess, resize_pending_if_needed() settles it */
#define SRTOUT_RAM_FRACTION_DIVISOR 50         /* ceiling: 1/50 = 2% of MemAvailable */
#define SRTOUT_RESIZE_INTERVAL_S 1.0
#define SRTOUT_BITRATE_EMA_ALPHA 0.3           /* new-sample weight per resize tick */

struct srtout {
  SRTSOCKET sock;           /* SRT_INVALID_SOCK while (re)connecting */
  int eid;                  /* SRT epoll set: watches sock for OUT (writable/connected) | ERR */
  int connected;            /* 1 once sock has reported OUT since its last (re)connect */
  double next_reconnect_at; /* mono_seconds() gate; 0 = try on next service() call */
  metrics_exporter_t *mx;
  const char *tool_version;
  char peer_label[64];
  srtout_cfg_t cfg; /* kept for reconnect */
  unsigned safety_mult; /* resolved from cfg, clamped 1..SRTOUT_SAFETY_MULT_MAX */

  unsigned char *pending; /* pending_cap * SRTOUT_PLSIZE bytes, malloc'd, resized in resize_pending() */
  int *pending_len;       /* pending_cap ints */
  int pending_cap;
  int pending_head;  /* index of oldest queued chunk */
  int pending_count;
  int drop_logged;   /* throttles "queue full" logging to once per overflow streak */

  size_t bytes_since_check; /* fed to bitrate EMA at each resize tick */
  double last_check_at;     /* mono_seconds(); 0 = not yet sampled */
  double bitrate_ema_bps;   /* 0 until first real sample */
};

/* RCVSYN/SNDSYN off: connect/rendezvous returns immediately, EASYNCSND means in progress */
static SRTSOCKET open_single(const srtout_cfg_t *cfg) {
  SRTSOCKET s;
  struct sockaddr_storage remote;
  struct sockaddr_storage local;
  int remote_len;
  int local_len;
  int no = 0;

  if (srtcommon_resolve(cfg->peers[0].host, cfg->peers[0].port, &remote, &remote_len))
    return SRT_INVALID_SOCK;

  s = srt_create_socket();
  if (s == SRT_INVALID_SOCK) {
    log_line("srt: socket create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_apply_opts(s, &cfg->opts, 0, SRTO_SNDTIMEO, SRTOUT_SNDTIMEO_MS) ||
      srt_setsockopt(s, 0, SRTO_RCVSYN, &no, sizeof no) != 0 || srt_setsockopt(s, 0, SRTO_SNDSYN, &no, sizeof no) != 0) {
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
    if (srt_rendezvous(s, (struct sockaddr *)&local, local_len, (struct sockaddr *)&remote, remote_len) != 0 &&
        srt_getlasterror(NULL) != SRT_EASYNCSND) {
      log_line("srt: rendezvous failed: %s", srt_getlasterror_str());
      srt_close(s);
      return SRT_INVALID_SOCK;
    }
  } else if (srt_connect(s, (struct sockaddr *)&remote, remote_len) != 0 && srt_getlasterror(NULL) != SRT_EASYNCSND) {
    log_line("srt: connect failed: %s", srt_getlasterror_str());
    srt_close(s);
    return SRT_INVALID_SOCK;
  }
  return s;
}

static SRTSOCKET open_group(const srtout_cfg_t *cfg) {
  SRTSOCKET g;
  SRT_GROUP_TYPE type = cfg->group_mode == SRTGROUP_BACKUP ? SRT_GTYPE_BACKUP : SRT_GTYPE_BROADCAST;
  SRT_SOCKGROUPCONFIG gc[SRTCOMMON_MAX_PEERS];
  int no = 0;

  g = srt_create_group(type);
  if (g == SRT_INVALID_SOCK) {
    log_line("srt: group create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_apply_opts(g, &cfg->opts, 1, SRTO_SNDTIMEO, SRTOUT_SNDTIMEO_MS) ||
      srt_setsockopt(g, 0, SRTO_RCVSYN, &no, sizeof no) != 0 || srt_setsockopt(g, 0, SRTO_SNDSYN, &no, sizeof no) != 0) {
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_build_group_config(cfg->peers, cfg->npeers, gc)) {
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  if (srt_connect_group(g, gc, cfg->npeers) == SRT_ERROR && srt_getlasterror(NULL) != SRT_EASYNCSND) {
    log_line("srt: group connect failed: %s", srt_getlasterror_str());
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  return g;
}

/* (re)starts a connect attempt: creates socket/group, kicks off non-blocking
   connect, registers it with r's epoll set. 0 ok, -1 error (nothing changed) */
static int start_connect(srtout_t *r) {
  SRTSOCKET s = r->cfg.group_mode == SRTGROUP_NONE ? open_single(&r->cfg) : open_group(&r->cfg);
  int events = SRT_EPOLL_OUT | SRT_EPOLL_ERR;

  if (s == SRT_INVALID_SOCK)
    return -1;
  if (srt_epoll_add_usock(r->eid, s, &events) != 0) {
    log_line("srt: epoll add failed: %s", srt_getlasterror_str());
    srt_close(s);
    return -1;
  }
  r->sock = s;
  r->connected = 0;
  return 0;
}

/* srt_close() deregisters socket from r->eid itself: explicit remove_usock first would race it
   + log spurious internal SRT error */
static void teardown_for_reconnect(srtout_t *r, const char *why) {
  log_line("srt: %s, reconnecting", why);
  srt_close(r->sock);
  r->sock = SRT_INVALID_SOCK;
  r->connected = 0;
  r->next_reconnect_at = mono_seconds() + SRTOUT_RECONNECT_BACKOFF_S;
}

static unsigned char *pending_slot(srtout_t *r, int idx) { return r->pending + (size_t)idx * SRTOUT_PLSIZE; }

static void enqueue_pending(srtout_t *r, const unsigned char *buf, int len) {
  int tail;

  if (r->pending_count == r->pending_cap) {
    r->pending_head = (r->pending_head + 1) % r->pending_cap;
    r->pending_count--;
    if (!r->drop_logged) {
      log_line("srt output: send queue full, dropping oldest queued chunk");
      r->drop_logged = 1;
    }
  } else {
    r->drop_logged = 0;
  }
  tail = (r->pending_head + r->pending_count) % r->pending_cap;
  memcpy(pending_slot(r, tail), buf, (size_t)len);
  r->pending_len[tail] = len;
  r->pending_count++;
}

/* sends what it can without blocking. stops at first still-full result.
   real send error tears down, schedules reconnect */
static void flush_pending(srtout_t *r) {
  while (r->pending_count > 0 && r->sock != SRT_INVALID_SOCK) {
    int head = r->pending_head;
    int sent = srt_sendmsg2(r->sock, (const char *)pending_slot(r, head), r->pending_len[head], NULL);

    if (sent == SRT_ERROR) {
      if (srt_getlasterror(NULL) == SRT_EASYNCSND)
        return;
      teardown_for_reconnect(r, "write failed on queued data");
      return;
    }
    r->pending_head = (head + 1) % r->pending_cap;
    r->pending_count--;
  }
}

/* MemAvailable: safe estimate, skips reclaimable cache. -1: /proc/meminfo unreadable */
static long available_ram_bytes(void) {
  FILE *f = fopen("/proc/meminfo", "r");
  unsigned long kb = 0;
  char line[128];

  if (!f)
    return -1;
  while (fgets(line, sizeof line, f)) {
    if (sscanf(line, "MemAvailable: %lu kB", &kb) == 1)
      break;
  }
  fclose(f);
  return kb ? (long)kb * 1024 : -1;
}

static int pending_target_chunks(const srtout_t *r) {
  double bps = r->bitrate_ema_bps > 0 ? r->bitrate_ema_bps : (double)SRTOUT_INITIAL_BITRATE_BPS;
  unsigned latency_ms = r->cfg.opts.latency_ms ? r->cfg.opts.latency_ms : SRTOUT_DEFAULT_LATENCY_MS;
  double floor_bytes = (double)SRTOUT_PENDING_FLOOR_CHUNKS * SRTOUT_PLSIZE;
  double target_bytes = bps * (latency_ms / 1000.0) * r->safety_mult;
  long ram = available_ram_bytes();
  long ceiling_bytes = ram > 0 ? ram / SRTOUT_RAM_FRACTION_DIVISOR : -1;

  if (target_bytes < floor_bytes)
    target_bytes = floor_bytes;
  if (ceiling_bytes > 0 && target_bytes > (double)ceiling_bytes)
    target_bytes = (double)ceiling_bytes;
  if (target_bytes < floor_bytes)
    target_bytes = floor_bytes; /* RAM tighter than floor: keep floor anyway */
  return (int)(target_bytes / SRTOUT_PLSIZE);
}

/* retargets capacity, never below what's queued: low instant bitrate
   (finite source draining) must not discard backlog still catching up on link.
   malloc failure: keeps current buffer, retries next tick */
static void resize_pending(srtout_t *r, int new_cap) {
  unsigned char *new_pending;
  int *new_len;

  if (new_cap < r->pending_count)
    new_cap = r->pending_count;
  if (new_cap == r->pending_cap)
    return;
  new_pending = malloc((size_t)new_cap * SRTOUT_PLSIZE);
  new_len = malloc((size_t)new_cap * sizeof *new_len);
  if (!new_pending || !new_len) {
    log_line("srt output: pending buffer resize to %d chunks failed, keeping %d", new_cap, r->pending_cap);
    free(new_pending);
    free(new_len);
    return;
  }
  for (int i = 0; i < r->pending_count; i++) {
    int src = (r->pending_head + i) % r->pending_cap;
    memcpy(new_pending + (size_t)i * SRTOUT_PLSIZE, pending_slot(r, src), (size_t)r->pending_len[src]);
    new_len[i] = r->pending_len[src];
  }
  free(r->pending);
  free(r->pending_len);
  r->pending = new_pending;
  r->pending_len = new_len;
  r->pending_cap = new_cap;
  r->pending_head = 0;
}

/* re-estimates input bitrate (EMA), retargets pending buffer to
   safety_mult latency-windows at that rate, bounded by RAM headroom */
static void resize_pending_if_needed(srtout_t *r) {
  double now = mono_seconds();

  if (r->last_check_at > 0 && now - r->last_check_at < SRTOUT_RESIZE_INTERVAL_S)
    return;
  if (r->last_check_at > 0) {
    double inst = (double)r->bytes_since_check / (now - r->last_check_at);
    r->bitrate_ema_bps = r->bitrate_ema_bps > 0 ? r->bitrate_ema_bps + SRTOUT_BITRATE_EMA_ALPHA * (inst - r->bitrate_ema_bps)
                                                : inst;
  }
  r->bytes_since_check = 0;
  r->last_check_at = now;
  resize_pending(r, pending_target_chunks(r));
}

srtout_t *srtout_open(const srtout_cfg_t *cfg) {
  srtout_t *r;

  if (cfg->npeers <= 0 || cfg->npeers > SRTCOMMON_MAX_PEERS) {
    log_line("srt: invalid peer count %d", cfg->npeers);
    return NULL;
  }
  if (cfg->group_mode == SRTGROUP_NONE && cfg->npeers != 1) {
    log_line("srt: plain connection needs exactly one peer, got %d", cfg->npeers);
    return NULL;
  }
  if (cfg->group_mode != SRTGROUP_NONE && cfg->rendezvous) {
    log_line("srt: rendezvous cannot be combined with bonded groups");
    return NULL;
  }

  if (srt_startup() == SRT_ERROR) {
    log_line("srt: startup failed: %s", srt_getlasterror_str());
    return NULL;
  }
  srtcommon_open_logging(cfg->verbose);

  r = calloc(1, sizeof *r);
  if (!r) {
    srt_cleanup();
    return NULL;
  }
  r->sock = SRT_INVALID_SOCK;
  r->eid = srt_epoll_create();
  if (r->eid == SRT_ERROR) {
    log_line("srt: epoll create failed: %s", srt_getlasterror_str());
    free(r);
    srt_cleanup();
    return NULL;
  }
  r->mx = cfg->mx;
  r->tool_version = cfg->tool_version;
  r->cfg = *cfg;
  r->safety_mult = cfg->safety_mult ? cfg->safety_mult : SRTOUT_SAFETY_MULT_DEFAULT;
  if (r->safety_mult > SRTOUT_SAFETY_MULT_MAX)
    r->safety_mult = SRTOUT_SAFETY_MULT_MAX;
  snprintf(r->peer_label, sizeof r->peer_label, "%s:%u", cfg->peers[0].host, cfg->peers[0].port);

  r->pending_cap = SRTOUT_PENDING_FLOOR_CHUNKS;
  r->pending = malloc((size_t)r->pending_cap * SRTOUT_PLSIZE);
  r->pending_len = malloc((size_t)r->pending_cap * sizeof *r->pending_len);
  if (!r->pending || !r->pending_len) {
    free(r->pending);
    free(r->pending_len);
    srt_epoll_release(r->eid);
    free(r);
    srt_cleanup();
    return NULL;
  }

  if (start_connect(r) != 0) {
    free(r->pending);
    free(r->pending_len);
    srt_epoll_release(r->eid);
    free(r);
    srt_cleanup();
    return NULL;
  }
  return r;
}

/* stats push covers whole connection/group, not per bonded member */
static void push_stats(srtout_t *r) {
  SRT_TRACEBSTATS st;
  metrics_writer_t w;

  if (!r->mx || r->sock == SRT_INVALID_SOCK || !metrics_exporter_due(r->mx, mono_seconds()) || srt_bstats(r->sock, &st, 0) != 0)
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

static void service_step(srtout_t *r) {
  resize_pending_if_needed(r);
  if (r->sock == SRT_INVALID_SOCK) {
    if (mono_seconds() >= r->next_reconnect_at && start_connect(r) != 0)
      r->next_reconnect_at = mono_seconds() + SRTOUT_RECONNECT_BACKOFF_S;
  } else {
    SRT_EPOLL_EVENT ev;
    if (srt_epoll_uwait(r->eid, &ev, 1, 0) > 0 && ev.fd == r->sock) {
      if (ev.events & SRT_EPOLL_OUT) {
        r->connected = 1;
        flush_pending(r);
      }
      if (ev.events & SRT_EPOLL_ERR) {
        /* ERR can be stale from connect race. confirm broken before teardown */
        SRT_SOCKSTATUS st = srt_getsockstate(r->sock);
        if (st == SRTS_BROKEN || st == SRTS_CLOSED || st == SRTS_NONEXIST)
          teardown_for_reconnect(r, "link failed");
      }
    }
  }
}

void srtout_service(srtout_t *r, srtout_status_t *out) {
  service_step(r);
  push_stats(r);
  out->connected = r->connected;
}

void srtout_write(srtout_t *r, const unsigned char *buf, size_t n) {
  r->bytes_since_check += n;
  for (size_t off = 0; off < n; off += (size_t)SRTOUT_PLSIZE) {
    size_t chunk = n - off < (size_t)SRTOUT_PLSIZE ? n - off : (size_t)SRTOUT_PLSIZE;

    if (r->pending_count == 0 && r->connected) {
      int sent = srt_sendmsg2(r->sock, (const char *)(buf + off), (int)chunk, NULL);
      if (sent != SRT_ERROR)
        continue;
      if (srt_getlasterror(NULL) != SRT_EASYNCSND)
        teardown_for_reconnect(r, "write failed");
    }
    enqueue_pending(r, buf + off, (int)chunk);
  }
}

/* loop may exit before a just-started connect finishes: flush before teardown,
   else pending data drops silently */
static void flush_before_close(srtout_t *r) {
  int waited_ms = 0;

  while (r->pending_count > 0 && waited_ms < SRTOUT_CLOSE_DRAIN_MAX_MS) {
    struct timespec ts = {0, 20L * 1000000L};
    service_step(r);
    if (r->pending_count == 0)
      break;
    nanosleep(&ts, NULL);
    waited_ms += 20;
  }
}

/* sndbuffer empty != delivered to peer app. measured: single small msg, ~1s real wait needed.
   floor: max(MIN_WAIT_MS, peer latency), capped at DRAIN_MAX_MS */
static void drain_before_close(SRTSOCKET sock) {
  int waited_ms = 0;
  size_t blocks;
  size_t bytes;
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
  flush_before_close(r);
  if (r->sock != SRT_INVALID_SOCK) {
    drain_before_close(r->sock);
    srt_close(r->sock);
  }
  srt_epoll_release(r->eid);
  srt_cleanup();
  free(r->pending);
  free(r->pending_len);
  free(r);
}
