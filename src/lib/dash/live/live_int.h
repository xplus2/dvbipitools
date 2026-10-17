/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_DASH_LIVE_LIVE_INT_H
#define LIB_DASH_LIVE_LIVE_INT_H

#include <stdlib.h>
#include <string.h>
#include "lib/dash/live.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "lib/dash/mpd.h"

#define DASH_ETAG_MAX 128
#define DASH_MPD_BUF_CAP (64 * 1024)
#define DASH_SEG_BUF_CAP (2 * 1024 * 1024)
#define DASH_OUT_BUF_CAP (2 * DASH_SEG_BUF_CAP)
#define DASH_OUT_BUF_PREFETCH_HEADROOM DASH_SEG_BUF_CAP
#define DASH_DEFAULT_UPDATE_PERIOD_S 6
#define DASH_DEFAULT_SEGMENT_DURATION_S 6
#define DASH_MAX_REUSE_AGE_S 15
#define DASH_MAX_LAG_MS 30000
#define DASH_MAX_FAILS 5
#define DASH_SEG_RETRY_S 0.5
#define DASH_STATIC_RETRY_S 2.0
#define DASH_INIT_RETRY_S 1.0

typedef enum { DASH_LIVE_IDLE, DASH_LIVE_FETCHING_MPD, DASH_LIVE_FETCHING_INIT, DASH_LIVE_FETCHING_SEGMENT } dash_live_phase_t;

struct dash_live {
  http_url_t mpd_url;
  char user_agent[128];
  int insecure;
  unsigned idx;
  const char *label;
  unsigned adaptation_set_index;
  dash_insp_t si;
  dash_segment_cb cb;
  void *cb_ctx;

  dash_live_phase_t phase;
  http_fetch_t *fetch;
  http_t *reuse;
  double reuse_established_at;
  double next_mpd_poll_at;
  double next_segment_at;
  char etag[DASH_ETAG_MAX];

  int have_representation;
  dash_mpd_t mpd;
  dash_representation_t repr;
  unsigned period_idx;
  unsigned long long cur_period_start_ms;
  char as_mime[64];
  int init_fetched;
  int positioned;
  unsigned long long seg_idx;
  unsigned long long seg_time;
  unsigned long long seg_d;
  int wait_timeline;
  int finished;
  unsigned fail_count;
  double mpd_fetched_at;
  double update_period_s;
  log_throttle_t drop_throttle;
  log_throttle_t trunc_throttle;
  log_throttle_t fail_throttle;
  log_throttle_t lag_throttle;

  unsigned char mpd_buf[DASH_MPD_BUF_CAP + 1];
  unsigned char seg_buf[DASH_SEG_BUF_CAP];

  unsigned char out_buf[DASH_OUT_BUF_CAP];
  size_t out_len;
  size_t out_pos;
};
typedef enum { PREP_WAIT, PREP_READY, PREP_NEED_MPD } prep_t;

int start_mpd_fetch(dash_live_t *h);
int start_url_fetch(dash_live_t *h, const char *url, dash_live_phase_t phase);
int uses_timeline(const dash_live_t *h);
int wall_anchored(const dash_live_t *h);
int paced(const dash_live_t *h);
double segment_duration_s(const dash_live_t *h);
unsigned long long elapsed_ms(const dash_live_t *h);
int find_period(const dash_live_t *h, unsigned *out);
int pick_representation(dash_live_t *h, unsigned period);
void join_position(dash_live_t *h);
void switch_period_if_due(dash_live_t *h);
int last_period(const dash_live_t *h);
prep_t prepare_segment(dash_live_t *h, char *url, size_t n);
void advance_segment(dash_live_t *h);
int handle_mpd_done(dash_live_t *h, net_err_reason_t *reason_out);
int init_failed(dash_live_t *h, net_err_reason_t *reason_out);
int handle_init_done(dash_live_t *h, net_err_reason_t *reason_out);
void segment_failed(dash_live_t *h);
int handle_segment_done(dash_live_t *h);

#endif
