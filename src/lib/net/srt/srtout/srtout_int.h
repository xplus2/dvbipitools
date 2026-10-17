/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_NET_SRT_SRTOUT_SRTOUT_INT_H
#define LIB_NET_SRT_SRTOUT_SRTOUT_INT_H

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

int start_connect(srtout_t *r, int blocking);
void teardown_for_reconnect(srtout_t *r, const char *why);
void note_enqueue_off(srtout_t *r);
void note_enqueue_hwm(srtout_t *r);
int enqueue_pending(srtout_t *r, const unsigned char *buf, int len);
void flush_pending(srtout_t *r);
void resize_pending_if_needed(srtout_t *r);
void flush_carry(srtout_t *r);
void service_step(srtout_t *r);

#endif
