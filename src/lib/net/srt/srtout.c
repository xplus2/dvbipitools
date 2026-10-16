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

#include "srtcommon.h"
#include "srtout.h"

#define SRTOUT_SNDTIMEO_MS 1000
#define SRTOUT_CLOSE_DRAIN_MAX_MS 3000 /* cap on drain_before_close's wait */
#define SRTOUT_CLOSE_MIN_WAIT_MS 1000  /* floor on drain_before_close's wait, see there */
#define SRTOUT_RECONNECT_BACKOFF_S 0.2 /* min gap between failed (re)connect attempts */
#define SRTOUT_REVIVE_INTERVAL_S 1.0   /* min gap between bonded member re-add checks */
#define SRTOUT_CARRY_MAX_S 0.02        /* max hold of a partial chunk before sending it short */
#define SRTOUT_PLSIZE 1316             /* srt.h's SRT_LIVE_DEF_PLSIZE, but real constant expr for array sizing */

#define SRTOUT_PENDING_FLOOR_CHUNKS 64        /* never below this, regardless of bitrate/RAM: ~84KB */
#define SRTOUT_SAFETY_MULT_DEFAULT 4          /* --send-buffer-mult unset: latency-window multiples to buffer */
#define SRTOUT_SAFETY_MULT_MAX 32
#define SRTOUT_DEFAULT_LATENCY_MS 120         /* SRT_LIVE_DEF_LATENCY, fallback for opts.latency_ms 0 */
#define SRTOUT_INITIAL_BITRATE_BPS (25000000 / 8)  /* startup guess: one DVB mux, until first sample */
#define SRTOUT_GROW_PCT 25   /* grow only when target exceeds cap by this much */
#define SRTOUT_SHRINK_PCT 50 /* shrink only when target is below this share of cap */
#define SRTOUT_RAM_FRACTION_DIVISOR 50         /* ceiling: 1/50 = 2% of MemAvailable */
#define SRTOUT_RESIZE_INTERVAL_S 1.0
#define SRTOUT_RAM_CHECK_INTERVAL_S 5.0 /* feeds a soft ceiling only, doesn't need 1Hz freshness */
#define SRTOUT_BITRATE_EMA_ALPHA 0.3           /* new-sample weight per resize tick */

struct srtout {
  SRTSOCKET sock;           /* SRT_INVALID_SOCK while (re)connecting */
  int eid;                  /* SRT epoll set: watches sock for OUT (writable/connected) | ERR */
  int connected;            /* 1 once sock has reported OUT since its last (re)connect */
  double next_reconnect_at; /* mono_seconds() gate; 0 = try on next service() call */
  double next_revive_at;    /* mono_seconds() gate for bonded member re-add */
  srtcommon_resolver_t *revive_res;    /* in-flight member re-add lookups */
  srtcommon_resolver_t *reconnect_res; /* in-flight reconnect lookups */
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
  uint64_t pending_bytes; /* sum of queued chunk lengths */
  srt_queue_metrics_t queue_metrics;
  void (*note_enqueue)(srtout_t *);
  unsigned char carry[SRTOUT_PLSIZE]; /* partial chunk held until filled or close */
  int carry_len;
  double carry_since; /* mono_seconds() of first carried byte */
  int pending_hwm;
  uint64_t queue_dropped;
  int drop_logged;   /* throttles "queue full" logging to once per overflow streak */

  size_t bytes_since_check; /* fed to bitrate EMA at each resize tick */
  double last_check_at;     /* mono_seconds(); 0 = not yet sampled */
  double bitrate_ema_bps;   /* 0 until first real sample */

  long ram_cached_bytes;    /* -1 = unreadable */
  double ram_checked_at;    /* mono_seconds(); 0 = not yet sampled */
};

typedef struct {
  struct sockaddr_storage ss[SRTCOMMON_MAX_PEERS + 1]; /* peers, then local bind if rendezvous */
  int len[SRTCOMMON_MAX_PEERS + 1];
} targets_t;

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
static int start_connect(srtout_t *r, int blocking) {
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
static void teardown_for_reconnect(srtout_t *r, const char *why) {
  log_code("LW600145", "srt: %s, reconnecting", why);
  srt_close(r->sock);
  r->sock = SRT_INVALID_SOCK;
  r->connected = 0;
  r->next_reconnect_at = mono_seconds() + SRTOUT_RECONNECT_BACKOFF_S;
}

static unsigned char *pending_slot(srtout_t *r, int idx) { return r->pending + (size_t)idx * SRTOUT_PLSIZE; }

static void note_enqueue_off(srtout_t *r) { (void)r; }

static void note_enqueue_hwm(srtout_t *r) {
  if (r->pending_count > r->pending_hwm) r->pending_hwm = r->pending_count;
}

/* 1 if oldest chunk was dropped */
static int enqueue_pending(srtout_t *r, const unsigned char *buf, int len) {
  int tail;
  int dropped = 0;

  if (r->pending_count == r->pending_cap) {
    r->pending_bytes -= (uint64_t)r->pending_len[r->pending_head];
    r->pending_head = (r->pending_head + 1) % r->pending_cap;
    r->pending_count--;
    r->queue_dropped++;
    dropped = 1;
    if (!r->drop_logged) {
      log_code("LW100131", "srt output: send queue full, dropping oldest queued chunk");
      r->drop_logged = 1;
    }
  } else {
    r->drop_logged = 0;
  }
  tail = (r->pending_head + r->pending_count) % r->pending_cap;
  memcpy(pending_slot(r, tail), buf, (size_t)len);
  r->pending_len[tail] = len;
  r->pending_count++;
  r->pending_bytes += (uint64_t)len;
  r->note_enqueue(r);
  return dropped;
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
    r->pending_bytes -= (uint64_t)r->pending_len[head];
    r->pending_head = (head + 1) % r->pending_cap;
    r->pending_count--;
  }
}

/* MemAvailable: safe estimate, skips reclaimable cache. -1: /proc/meminfo unreadable */
static long read_ram_bytes(void) {
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

/* only feeds a soft ceiling, cached SRTOUT_RAM_CHECK_INTERVAL_S */
static long available_ram_bytes(srtout_t *r, double now) {
  if (r->ram_checked_at == 0.0 || now - r->ram_checked_at >= SRTOUT_RAM_CHECK_INTERVAL_S) {
    r->ram_cached_bytes = read_ram_bytes();
    r->ram_checked_at = now;
  }
  return r->ram_cached_bytes;
}

static int pending_target_chunks(srtout_t *r, double now) {
  double bps = r->bitrate_ema_bps > 0 ? r->bitrate_ema_bps : (double)SRTOUT_INITIAL_BITRATE_BPS;
  unsigned latency_ms = r->cfg.opts.latency_ms ? r->cfg.opts.latency_ms : SRTOUT_DEFAULT_LATENCY_MS;
  double floor_bytes = (double)SRTOUT_PENDING_FLOOR_CHUNKS * SRTOUT_PLSIZE;
  double target_bytes = bps * (latency_ms / 1000.0) * r->safety_mult;
  long ram = available_ram_bytes(r, now);
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
    log_code("LE500167", "srt output: pending buffer resize to %d chunks failed, keeping %d", new_cap, r->pending_cap);
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
   safety_mult latency-windows at that rate, bounded by RAM headroom.
   hysteresis: small target drift does not realloc */
static void resize_pending_if_needed(srtout_t *r) {
  double now = mono_seconds();
  int target;

  if (r->last_check_at > 0 && now - r->last_check_at < SRTOUT_RESIZE_INTERVAL_S)
    return;
  if (r->last_check_at > 0) {
    double inst = (double)r->bytes_since_check / (now - r->last_check_at);
    r->bitrate_ema_bps = r->bitrate_ema_bps > 0 ? r->bitrate_ema_bps + SRTOUT_BITRATE_EMA_ALPHA * (inst - r->bitrate_ema_bps)
                                                : inst;
  }
  r->bytes_since_check = 0;
  r->last_check_at = now;
  target = pending_target_chunks(r, now);
  if (target > r->pending_cap + r->pending_cap * SRTOUT_GROW_PCT / 100 ||
      target < r->pending_cap * SRTOUT_SHRINK_PCT / 100)
    resize_pending(r, target);
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

static uint64_t queue_ms(const srtout_t *r) {
  if (r->bitrate_ema_bps <= 0) return 0;
  return (uint64_t)((double)r->pending_bytes * 1000.0 / r->bitrate_ema_bps);
}

/* stats push covers whole connection/group, not per bonded member */
static void push_stats(srtout_t *r) {
  SRT_TRACEBSTATS st;
  int have;
  size_t n = 0;
  if (!r->mx || !metrics_exporter_due(r->mx, mono_seconds())) return;
  memset(&st, 0, sizeof st);
  have = r->sock != SRT_INVALID_SOCK && srt_bstats(r->sock, &st, 0) == 0;
  metrics_entry_t e[] = {
{METRICS_ID_SRT_SENDER_SENT_TOTAL, r->peer_label, (uint64_t)st.pktSentTotal},
{METRICS_ID_SRT_SENDER_RETRANSMITTED_TOTAL, r->peer_label, (uint64_t)st.pktRetransTotal},
{METRICS_ID_SRT_SENDER_RTT_MILLISECONDS, r->peer_label, (uint64_t)st.msRTT},
{METRICS_ID_SRT_SENDER_LOST_TOTAL, r->peer_label, (uint64_t)st.pktSndLossTotal},
{METRICS_ID_SRT_SENDER_DROPPED_TOTAL, r->peer_label, (uint64_t)st.pktSndDropTotal},
  };
  metrics_entry_t q[] = {
{METRICS_ID_SRT_SENDER_QUEUE_CHUNKS, r->peer_label, (uint64_t)r->pending_count},
{METRICS_ID_SRT_SENDER_QUEUE_CAPACITY_CHUNKS, r->peer_label, (uint64_t)r->pending_cap},
{METRICS_ID_SRT_SENDER_QUEUE_HIGH_WATERMARK_CHUNKS, r->peer_label, (uint64_t)r->pending_hwm},
{METRICS_ID_SRT_SENDER_QUEUE_DROPPED_CHUNKS_TOTAL, r->peer_label, r->queue_dropped},
{METRICS_ID_SRT_SENDER_QUEUE_MILLISECONDS, r->peer_label, queue_ms(r)},
  };
  metrics_entry_t all[sizeof e / sizeof e[0] + sizeof q / sizeof q[0]];
  if (have) {
    n = sizeof e / sizeof e[0];
    memcpy(all, e, sizeof e);
  }
  if (r->queue_metrics != SRT_QUEUE_METRICS_OFF) {
    size_t nq = r->queue_metrics == SRT_QUEUE_METRICS_FULL ? 4 : 2;
    memcpy(all + n, q, nq * sizeof q[0]);
    n += nq;
    if (r->bitrate_ema_bps > 0) all[n++] = q[4];
  }
  metrics_push_entries(r->mx, r->tool_version, all, n);
}

/* libsrt never redials a broken member of a live group */
static void revive_members(srtout_t *r) {
  double now = mono_seconds();

  if (r->cfg.group_mode == SRTGROUP_NONE || !r->connected)
    return;
  if (!r->revive_res) {
    if (now < r->next_revive_at) return;
    r->next_revive_at = now + SRTOUT_REVIVE_INTERVAL_S;
  }
  srtcommon_group_revive(r->sock, r->cfg.peers, r->cfg.npeers, &r->revive_res);
}

static int send_chunk(srtout_t *r, const unsigned char *p, size_t len);

static void flush_carry(srtout_t *r) {
  if (r->carry_len > 0) send_chunk(r, r->carry, (size_t)r->carry_len);
  r->carry_len = 0;
}

static void service_step(srtout_t *r) {
  if (r->carry_len > 0 && mono_seconds() - r->carry_since >= SRTOUT_CARRY_MAX_S) flush_carry(r);
  resize_pending_if_needed(r);
  if (r->sock == SRT_INVALID_SOCK) {
    if ((r->reconnect_res || mono_seconds() >= r->next_reconnect_at) && start_connect(r, 0) < 0)
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
    if (r->sock != SRT_INVALID_SOCK)
      revive_members(r);
  }
}

void srtout_service(srtout_t *r, srtout_status_t *out) {
  service_step(r);
  push_stats(r);
  out->connected = r->connected;
}

/* 0 ok, -1 if send failed or a chunk was dropped */
static int send_chunk(srtout_t *r, const unsigned char *p, size_t len) {
  int rc = 0;

  if (r->pending_count == 0 && r->connected) {
    int sent = srt_sendmsg2(r->sock, (const char *)p, (int)len, NULL);
    if (sent != SRT_ERROR) return 0;
    if (srt_getlasterror(NULL) != SRT_EASYNCSND) {
      teardown_for_reconnect(r, "write failed");
      rc = -1;
    }
  }
  if (enqueue_pending(r, p, (int)len)) rc = -1;
  return rc;
}

int srtout_write(srtout_t *r, const unsigned char *buf, size_t n) {
  int rc = 0;
  size_t off = 0;

  r->bytes_since_check += n;
  if (r->carry_len > 0) {
    size_t take = (size_t)(SRTOUT_PLSIZE - r->carry_len);
    if (take > n) take = n;
    memcpy(r->carry + r->carry_len, buf, take);
    r->carry_len += (int)take;
    off = take;
    if (r->carry_len < SRTOUT_PLSIZE) return 0;
    rc = send_chunk(r, r->carry, SRTOUT_PLSIZE);
    r->carry_len = 0;
  }
  for (; n - off >= (size_t)SRTOUT_PLSIZE; off += (size_t)SRTOUT_PLSIZE)
    if (send_chunk(r, buf + off, SRTOUT_PLSIZE)) rc = -1;
  if (off < n) {
    memcpy(r->carry, buf + off, n - off);
    r->carry_since = mono_seconds();
    r->carry_len = (int)(n - off);
  }
  return rc;
}

/* loop may exit before a just-started connect finishes: flush before teardown,
   else pending data drops silently */
static void flush_before_close(srtout_t *r) {
  int waited_ms = 0;
  while (r->pending_count > 0 && waited_ms < SRTOUT_CLOSE_DRAIN_MAX_MS) {
    struct timespec ts = {0, 20L * 1000000L};
    service_step(r);
    if (r->pending_count == 0) break;
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
  if (srt_getsockopt(sock, 0, SRTO_PEERLATENCY, &peer_latency, &optlen) == 0 && peer_latency > floor_ms) floor_ms = peer_latency;
  if (floor_ms > SRTOUT_CLOSE_DRAIN_MAX_MS) floor_ms = SRTOUT_CLOSE_DRAIN_MAX_MS;
  if (floor_ms > waited_ms) {
    struct timespec ts;
    int remain_ms = floor_ms - waited_ms;
    ts.tv_sec = remain_ms / 1000;
    ts.tv_nsec = (long)(remain_ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
  }
}

void srtout_queue_stats(const srtout_t *r, srtout_queue_stats_t *out) {
  out->peer_label = r->peer_label;
  out->chunks = r->pending_count;
  out->capacity = r->pending_cap;
  out->high_watermark = r->pending_hwm;
  out->dropped = r->queue_dropped;
  out->ms = queue_ms(r);
  out->ms_known = r->bitrate_ema_bps > 0;
}

void srtout_link_stats(const srtout_t *r, srtout_link_stats_t *out) {
  SRT_TRACEBSTATS st;

  memset(out, 0, sizeof *out);
  if (r->sock == SRT_INVALID_SOCK || srt_bstats(r->sock, &st, 0) != 0) return;
  out->connected = 1;
  out->packets = (uint64_t)st.pktSentTotal;
  out->retransmitted = (uint64_t)st.pktRetransTotal;
  out->lost = (uint64_t)st.pktSndLossTotal;
  out->dropped = (uint64_t)st.pktSndDropTotal;
  out->rtt_ms = st.msRTT;
  out->mbps = st.mbpsSendRate;
}

static void close_impl(srtout_t *r, int graceful) {
  if (!r) return;
  if (graceful) {
    flush_carry(r);
    flush_before_close(r);
  }
  if (r->sock != SRT_INVALID_SOCK) {
    if (graceful) drain_before_close(r->sock);
    srt_close(r->sock);
  }
  srt_epoll_release(r->eid);
  srtcommon_resolver_free(r->revive_res);
  srtcommon_resolver_free(r->reconnect_res);
  srtcommon_cleanup();
  free(r->pending);
  free(r->pending_len);
  free(r);
}

void srtout_close(srtout_t *r) {
  close_impl(r, 1);
}

void srtout_close_now(srtout_t *r) {
  close_impl(r, 0);
}
