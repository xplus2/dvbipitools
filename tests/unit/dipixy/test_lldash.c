/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE /* memmem */

#include <check.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dipixy/dash/lldash.h"
#include "dipixy/reactor/conn.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/segstore_int.h"
#include "dipixy/ts/pidfilter.h"
#ifdef HAVE_HTTP2
#include "dipixy/http2/http2.h"
#include "dipixy/http2/http2_int.h"
#endif

#define SEG_NAME_0 "dseg0.m4s"
#define SEG_NAME_1 "dseg500.m4s"
#define RACE_PUSHES 20000
#define PART_BYTES 64

static int g_ctx_marker;
#define CTX ((capture_ctx_t *)&g_ctx_marker)
static const lcevc_select_t full = {LCEVC_SEL_FULL, 0, 0};
static const uint8_t part_data[PART_BYTES] = {0x00, 0x00, 0x00, 0x18, 'm', 'o', 'o', 'f'};

static void no_filter(pid_filter_t *f) { memset(f, 0, sizeof *f); }

static hls_store_t *open_store(unsigned pmt, seg_container_t container, int ll) {
  pid_filter_t f;

  no_filter(&f);
  hls_store_open(CTX, &f, pmt, &full, 0.5, 6, container);
  if (ll) hls_llhls_enable(CTX, &f, pmt, &full, container, 0.5);
  return hls_store_find(CTX, &f, pmt, &full, container);
}

static int subscribe_detached(unsigned pmt, const char *name, conn_proto_t proto) {
  pid_filter_t f;

  no_filter(&f);
  return dash_lldash_subscribe(CTX, &f, pmt, &full, name, proto);
}

static int subscribe(unsigned pmt, const char *name, conn_proto_t proto) {
  int idx = subscribe_detached(pmt, name, proto);

  dash_lldash_sub_attach(idx);
  return idx;
}

static void push_part(unsigned pmt) {
  pid_filter_t f;

  no_filter(&f);
  ck_assert_int_eq(hls_push_part(CTX, &f, pmt, &full, SEG_CONTAINER_FMP4, part_data, sizeof part_data, 0.1, 1), 0);
}

static void push_segment(unsigned pmt) {
  pid_filter_t f;

  no_filter(&f);
  ck_assert_int_eq(hls_push_segment_ll(CTX, &f, pmt, &full, SEG_CONTAINER_FMP4, 0.5), 0);
}

static uint32_t live_msn(const hls_store_t *s) {
  return atomic_load_explicit(&s->snap, memory_order_acquire)->live_msn;
}

static size_t drain(int slot) {
  uint8_t buf[256];
  size_t total = 0;
  size_t n;

  while ((n = dash_lldash_ring_read(slot, buf, sizeof buf)) > 0) total += n;
  return total;
}

static int chain_head(const hls_store_t *s) {
  return atomic_load_explicit(&s->lldash_sub_head, memory_order_acquire);
}

static void setup(void) {
  hls_store_init(8);
  dash_lldash_init(8);
}

START_TEST(subscribe_rejects_unusable_requests) {
  open_store(0, SEG_CONTAINER_FMP4, 1);
  open_store(1, SEG_CONTAINER_FMP4, 0);
  ck_assert_int_eq(subscribe(0, "bogus.txt", CONN_PROTO_H2), -1);
  ck_assert_int_eq(subscribe(0, "dseg5.m4s", CONN_PROTO_H2), -1);
  ck_assert_int_eq(subscribe(7, SEG_NAME_0, CONN_PROTO_H2), -1);
  ck_assert_int_eq(subscribe(1, SEG_NAME_0, CONN_PROTO_H2), -1);
  ck_assert_int_ge(subscribe(0, SEG_NAME_0, CONN_PROTO_H2), 0);
}
END_TEST

START_TEST(part_reaches_matching_subscriber) {
  hls_store_t *s = open_store(0, SEG_CONTAINER_FMP4, 1);
  int idx = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);

  ck_assert_int_ge(idx, 0);
  ck_assert_int_eq(chain_head(s), idx);
  push_part(0);
  ck_assert_int_eq(dash_lldash_ring_pending(idx), 1);
  ck_assert_uint_eq(drain(idx), sizeof part_data);
  ck_assert_int_eq(dash_lldash_ring_pending(idx), 0);
  ck_assert_int_eq(dash_lldash_sub_finalized(idx), 0);
}
END_TEST

START_TEST(attach_replays_parts_pushed_before_attach) {
  int idx;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  idx = subscribe_detached(0, SEG_NAME_0, CONN_PROTO_H2);
  push_part(0);
  push_part(0);
  ck_assert_int_eq(dash_lldash_ring_pending(idx), 0);
  dash_lldash_sub_attach(idx);
  ck_assert_uint_eq(drain(idx), 2 * sizeof part_data);
  push_part(0);
  ck_assert_uint_eq(drain(idx), sizeof part_data);
  dash_lldash_sub_attach(idx);
  ck_assert_int_eq(dash_lldash_ring_pending(idx), 0);
}
END_TEST

START_TEST(attach_after_segment_done_replays_and_finalizes) {
  int idx;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  idx = subscribe_detached(0, SEG_NAME_0, CONN_PROTO_H2);
  push_part(0);
  push_part(0);
  push_segment(0);
  ck_assert_int_eq(dash_lldash_sub_finalized(idx), 0);
  dash_lldash_sub_attach(idx);
  ck_assert_uint_eq(drain(idx), 2 * sizeof part_data);
  ck_assert_int_eq(dash_lldash_sub_finalized(idx), 1);
  ck_assert_int_eq(dash_lldash_ring_errored(idx), 0);
}
END_TEST

START_TEST(attach_after_store_closing_aborts) {
  hls_store_t *s = open_store(0, SEG_CONTAINER_FMP4, 1);
  int idx = subscribe_detached(0, SEG_NAME_0, CONN_PROTO_H2);

  on_store_closing(s);
  ck_assert_int_eq(dash_lldash_sub_finalized(idx), 0);
  dash_lldash_sub_attach(idx);
  ck_assert_int_eq(dash_lldash_sub_finalized(idx), 1);
  ck_assert_int_eq(dash_lldash_ring_errored(idx), 1);
}
END_TEST

START_TEST(part_skips_subscriber_of_other_store) {
  int a;
  int b;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  open_store(1, SEG_CONTAINER_FMP4, 1);
  a = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  b = subscribe(1, SEG_NAME_0, CONN_PROTO_H2);
  push_part(1);
  ck_assert_int_eq(dash_lldash_ring_pending(a), 0);
  ck_assert_int_eq(dash_lldash_ring_pending(b), 1);
  push_part(0);
  ck_assert_int_eq(dash_lldash_ring_pending(a), 1);
}
END_TEST

START_TEST(part_after_segment_done_skips_old_subscriber) {
  int idx;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  idx = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  push_part(0);
  push_segment(0);
  drain(idx);
  push_part(0);
  ck_assert_int_eq(dash_lldash_ring_pending(idx), 0);
}
END_TEST

START_TEST(track_store_subscriber_gets_track_parts_only) {
  pid_filter_t f;
  hls_store_t *vs = open_store(0, SEG_CONTAINER_FMP4_VIDEO, 1);
  int idx;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  idx = subscribe(0, "dsegv0.m4s", CONN_PROTO_H2);
  ck_assert_int_ge(idx, 0);
  ck_assert_int_eq(chain_head(vs), idx);
  push_part(0);
  ck_assert_int_eq(dash_lldash_ring_pending(idx), 0);
  no_filter(&f);
  ck_assert_int_eq(hls_push_part(CTX, &f, 0, &full, SEG_CONTAINER_FMP4_VIDEO, part_data, sizeof part_data, 0.1, 1), 0);
  ck_assert_uint_eq(drain(idx), sizeof part_data);
  ck_assert_int_eq(hls_push_segment_ll(CTX, &f, 0, &full, SEG_CONTAINER_FMP4_VIDEO, 0.5), 0);
  ck_assert_int_eq(dash_lldash_sub_finalized(idx), 1);
}
END_TEST

START_TEST(track_subscribe_needs_its_own_store) {
  open_store(0, SEG_CONTAINER_FMP4, 1);
  ck_assert_int_eq(subscribe(0, "dsega0.m4s", CONN_PROTO_H2), -1);
  ck_assert_int_eq(subscribe(0, "dinita.mp4", CONN_PROTO_H2), -1);
  open_store(0, SEG_CONTAINER_FMP4_AUDIO, 1);
  ck_assert_int_ge(subscribe(0, "dsega0.m4s", CONN_PROTO_H2), 0);
}
END_TEST

START_TEST(part_pushed_ignores_ts_container) {
  hls_store_t *fm = open_store(0, SEG_CONTAINER_FMP4, 1);
  hls_store_t *ts = open_store(0, SEG_CONTAINER_TS, 0);
  int idx = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);

  link_store_chain(ts, idx);
  on_part_pushed(ts, live_msn(fm), 0, part_data, sizeof part_data);
  ck_assert_int_eq(dash_lldash_ring_pending(idx), 0);
  on_segment_done(ts, live_msn(fm));
  ck_assert_int_eq(dash_lldash_sub_finalized(idx), 0);
  on_part_pushed(fm, live_msn(fm), 0, part_data, sizeof part_data);
  ck_assert_int_eq(dash_lldash_ring_pending(idx), 1);
}
END_TEST

START_TEST(segment_done_finalizes_matching_subscriber_only) {
  hls_store_t *s = open_store(0, SEG_CONTAINER_FMP4, 1);
  int a = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  int b;

  push_segment(0);
  ck_assert_int_eq(dash_lldash_sub_finalized(a), 1);
  ck_assert_int_eq(dash_lldash_ring_errored(a), 0);
  b = subscribe(0, SEG_NAME_1, CONN_PROTO_H2);
  ck_assert_int_ge(b, 0);
  on_segment_done(s, live_msn(s) - 1);
  ck_assert_int_eq(dash_lldash_sub_finalized(b), 0);
  on_segment_done(s, live_msn(s));
  ck_assert_int_eq(dash_lldash_sub_finalized(b), 1);
}
END_TEST

START_TEST(h1_subscriber_without_connection_still_finalizes) {
  int idx;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  idx = subscribe(0, SEG_NAME_0, CONN_PROTO_H1);
  ck_assert_int_ge(idx, 0);
  push_part(0);
  ck_assert_int_eq(dash_lldash_ring_pending(idx), 0);
  ck_assert_int_eq(dash_lldash_sub_finalized(idx), 0);
  push_segment(0);
  ck_assert_int_eq(dash_lldash_sub_finalized(idx), 1);
  ck_assert_int_eq(dash_lldash_ring_errored(idx), 0);
}
END_TEST

START_TEST(store_close_aborts_h2_subscriber) {
  pid_filter_t f;
  int idx;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  idx = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  no_filter(&f);
  hls_store_close(CTX, &f, 0, &full, SEG_CONTAINER_FMP4);
  ck_assert_int_eq(dash_lldash_ring_errored(idx), 1);
  ck_assert_int_eq(dash_lldash_sub_finalized(idx), 1);
}
END_TEST

START_TEST(store_closing_flags_h1_subscriber_finalized_only) {
  hls_store_t *s = open_store(0, SEG_CONTAINER_FMP4, 1);
  int idx = subscribe(0, SEG_NAME_0, CONN_PROTO_H1);

  on_store_closing(s);
  ck_assert_int_eq(dash_lldash_sub_finalized(idx), 1);
  ck_assert_int_eq(dash_lldash_ring_errored(idx), 0);
}
END_TEST

START_TEST(store_closing_covers_every_segment_but_not_closed_subscribers) {
  hls_store_t *s = open_store(0, SEG_CONTAINER_FMP4, 1);
  int a = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  int b;
  int gone;

  push_segment(0);
  b = subscribe(0, SEG_NAME_1, CONN_PROTO_H2);
  gone = subscribe(0, SEG_NAME_1, CONN_PROTO_H2);
  dash_lldash_sub_close(gone);
  on_store_closing(s);
  ck_assert_int_eq(dash_lldash_ring_errored(a), 1);
  ck_assert_int_eq(dash_lldash_ring_errored(b), 1);
  ck_assert_int_eq(dash_lldash_sub_finalized(b), 1);
  ck_assert_int_eq(dash_lldash_ring_errored(gone), 0);
}
END_TEST

START_TEST(store_chain_unlinks_head_middle_tail) {
  hls_store_t *s = open_store(0, SEG_CONTAINER_FMP4, 1);
  int a = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  int b = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  int c = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);

  ck_assert_int_eq(chain_head(s), c);
  dash_lldash_sub_close(b);
  ck_assert_int_eq(chain_head(s), c);
  push_part(0);
  ck_assert_int_eq(dash_lldash_ring_pending(a), 1);
  ck_assert_int_eq(dash_lldash_ring_pending(b), 0);
  ck_assert_int_eq(dash_lldash_ring_pending(c), 1);
  dash_lldash_sub_close(c);
  ck_assert_int_eq(chain_head(s), a);
  dash_lldash_sub_close(a);
  ck_assert_int_eq(chain_head(s), -1);
}
END_TEST

START_TEST(store_chain_unlink_of_non_member_is_a_noop) {
  hls_store_t *s = open_store(0, SEG_CONTAINER_FMP4, 1);
  int a = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  int b = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);

  dash_lldash_sub_close(a);
  unlink_store_chain(s, a);
  ck_assert_int_eq(chain_head(s), b);
  unlink_store_chain(s, b);
  ck_assert_int_eq(chain_head(s), -1);
  unlink_store_chain(s, b);
  ck_assert_int_eq(chain_head(s), -1);
}
END_TEST

START_TEST(sub_close_twice_frees_the_slot_once) {
  int a;
  int x;
  int y;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  a = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  dash_lldash_sub_close(a);
  dash_lldash_sub_close(a);
  x = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  y = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  ck_assert_int_ne(x, y);
}
END_TEST

START_TEST(tid_chain_out_of_range_is_a_noop) {
  int a;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  a = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  link_tid_chain(a, -1);
  link_tid_chain(a, 1 << 20);
  unlink_tid_chain(a);
  dash_lldash_flush_ready(-1);
  dash_lldash_flush_ready(1 << 20);
}
END_TEST

typedef struct {
  _Atomic int started;
  const hls_store_t *store;
  uint32_t seq;
} race_t;

static void *push_loop(void *arg) {
  race_t *r = arg;

  atomic_store_explicit(&r->started, 1, memory_order_release);
  for (int i = 0; i < RACE_PUSHES; i++) on_part_pushed(r->store, r->seq, i, part_data, sizeof part_data);
  return NULL;
}

START_TEST(store_closing_races_in_flight_chunk_push) {
  hls_store_t *s = open_store(0, SEG_CONTAINER_FMP4, 1);
  int h2 = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  int h1 = subscribe(0, SEG_NAME_0, CONN_PROTO_H1);
  race_t r = {.store = s, .seq = live_msn(s)};
  pthread_t t;

  pthread_create(&t, NULL, push_loop, &r);
  while (!atomic_load_explicit(&r.started, memory_order_acquire)) usleep(100);
  on_store_closing(s);
  pthread_join(t, NULL);
  ck_assert_int_eq(dash_lldash_sub_finalized(h2), 1);
  ck_assert_int_eq(dash_lldash_ring_errored(h2), 1);
  ck_assert_int_eq(dash_lldash_sub_finalized(h1), 1);
}
END_TEST

START_TEST(store_close_races_segment_and_part_pushes) {
  pid_filter_t f;
  hls_store_t *s = open_store(0, SEG_CONTAINER_FMP4, 1);
  int idx = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  race_t r = {.store = s, .seq = live_msn(s)};
  pthread_t t;

  no_filter(&f);
  pthread_create(&t, NULL, push_loop, &r);
  while (!atomic_load_explicit(&r.started, memory_order_acquire)) usleep(100);
  hls_store_close(CTX, &f, 0, &full, SEG_CONTAINER_FMP4);
  pthread_join(t, NULL);
  ck_assert_int_eq(dash_lldash_sub_finalized(idx), 1);
  ck_assert_int_eq(dash_lldash_ring_errored(idx), 1);
}
END_TEST

#ifdef HAVE_HTTP2
#define TID_SUBS 3
#define TID_A 2
#define TID_B 3

typedef struct {
  int peer;
  conn_t *c;
  h2_push_slot_t slot;
} h2sub_t;

static int g_epfd;

static void h2sub_open(h2sub_t *h, int idx, int tid) {
  int sv[2];

  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  conn_table_init(sv[1] + 64);
  h->peer = sv[1];
  h->c = conn_new(sv[0], NULL);
  ck_assert_ptr_nonnull(h->c);
  h->c->epfd = g_epfd;
  ck_assert_int_eq(epoll_ctl(g_epfd, EPOLL_CTL_ADD, sv[0], &(struct epoll_event){.events = EPOLLIN, .data.fd = sv[0]}), 0);
  ck_assert_int_eq(h2_conn_attach(h->c), 0);
  h->slot.sid = 1;
  h->slot.sub_idx = idx;
  dash_lldash_h2_bind(idx, h->c, &h->slot, tid, -1);
}

static void h2sub_close(h2sub_t *h) {
  if (conn_for_fd(h->c->fd)) h2_conn_close(g_epfd, h->c);
  close(h->peer);
}

static int woken(const h2sub_t *h) {
  return atomic_load_explicit(&h->c->want_write, memory_order_relaxed);
}

static void arm_all(h2sub_t *h, int n) {
  for (int i = 0; i < n; i++) atomic_store_explicit(&h[i].c->want_write, 0, memory_order_relaxed);
}

START_TEST(tid_chain_wakes_only_owned_live_subscribers) {
  h2sub_t h[TID_SUBS + 1];
  int idx[TID_SUBS + 1];
  hls_store_t *s;

  g_epfd = epoll_create1(0);
  ck_assert_int_ge(g_epfd, 0);
  s = open_store(0, SEG_CONTAINER_FMP4, 1);
  for (int i = 0; i <= TID_SUBS; i++) {
    idx[i] = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
    ck_assert_int_ge(idx[i], 0);
    h2sub_open(&h[i], idx[i], i < TID_SUBS ? TID_A : TID_B);
  }
  on_segment_done(s, live_msn(s));
  arm_all(h, TID_SUBS + 1);
  dash_lldash_flush_ready(TID_A);
  for (int i = 0; i < TID_SUBS; i++) ck_assert_int_eq(woken(&h[i]), 1);
  ck_assert_int_eq(woken(&h[TID_SUBS]), 0);

  dash_lldash_sub_close(idx[1]);
  arm_all(h, TID_SUBS + 1);
  dash_lldash_flush_ready(TID_A);
  ck_assert_int_eq(woken(&h[0]), 1);
  ck_assert_int_eq(woken(&h[1]), 0);
  ck_assert_int_eq(woken(&h[2]), 1);

  dash_lldash_sub_close(idx[2]);
  dash_lldash_sub_close(idx[0]);
  arm_all(h, TID_SUBS + 1);
  dash_lldash_flush_ready(TID_A);
  for (int i = 0; i < TID_SUBS; i++) ck_assert_int_eq(woken(&h[i]), 0);

  dash_lldash_flush_ready(TID_B);
  ck_assert_int_eq(woken(&h[TID_SUBS]), 1);
  for (int i = 0; i <= TID_SUBS; i++) h2sub_close(&h[i]);
  close(g_epfd);
}
END_TEST
#endif

START_TEST(slot_accessors_ignore_out_of_range_slots) {
  size_t len = 99;
  uint8_t buf[8];

  ck_assert_ptr_null(dash_lldash_sub_h2c(-1));
  ck_assert_ptr_null(dash_lldash_sub_h2c(1000));
  ck_assert_ptr_null(dash_lldash_sub_h2_slot(-1));
  ck_assert_ptr_null(dash_lldash_sub_h2_slot(1000));
  ck_assert_ptr_null(dash_lldash_sub_h3c(-1));
  ck_assert_ptr_null(dash_lldash_sub_h3c(1000));
  ck_assert_int_eq((int)dash_lldash_sub_h3_sid(-1), -1);
  ck_assert_int_eq((int)dash_lldash_sub_h3_sid(1000), -1);
  ck_assert_uint_eq(dash_lldash_ring_read(-1, buf, sizeof buf), 0u);
  ck_assert_int_eq(dash_lldash_ring_pending(1000), 0);
  ck_assert_int_eq(dash_lldash_ring_errored(-1), 0);
  ck_assert_ptr_null(dash_lldash_ring_peek(-1, &len));
  ck_assert_uint_eq(len, 0u);
  dash_lldash_ring_hold(-1, 4);
  dash_lldash_ring_ack(-1, 4);
  ck_assert_int_eq(dash_lldash_sub_finalized(-1), 1);
  ck_assert_int_eq(dash_lldash_sub_finalized(1000), 1);
  dash_lldash_h2_bind(-1, NULL, NULL, 0, 0);
  dash_lldash_h3_bind(1000, NULL, 0, 0, 0);
  dash_lldash_register_reactor_efd(-1, 5);
  dash_lldash_register_reactor_efd(1000, 5);
  dash_lldash_flush_ready(-1);
  dash_lldash_flush_ready(1000);
}
END_TEST

START_TEST(transport_binding_is_recorded_per_slot) {
  int h2c_marker;
  int h2_slot_marker;
  int h3c_marker;
  int h2;
  int h3;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  h2 = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  h3 = subscribe(0, SEG_NAME_0, CONN_PROTO_H3);
  ck_assert_int_ge(h2, 0);
  ck_assert_int_ge(h3, 0);
  dash_lldash_h2_bind(h2, &h2c_marker, &h2_slot_marker, 1, 7);
  dash_lldash_h3_bind(h3, &h3c_marker, 42, 2, 8);
  ck_assert_ptr_eq(dash_lldash_sub_h2c(h2), &h2c_marker);
  ck_assert_ptr_eq(dash_lldash_sub_h2_slot(h2), &h2_slot_marker);
  ck_assert_ptr_eq(dash_lldash_sub_h3c(h3), &h3c_marker);
  ck_assert_int_eq((int)dash_lldash_sub_h3_sid(h3), 42);
}
END_TEST

START_TEST(ring_peek_and_advance_consume_a_part) {
  int idx;
  size_t len = 0;
  const uint8_t *p;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  idx = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  ck_assert_int_ge(idx, 0);
  push_part(0);
  p = dash_lldash_ring_peek(idx, &len);
  ck_assert_ptr_nonnull(p);
  ck_assert_uint_eq(len, sizeof part_data);
  ck_assert_mem_eq(p, part_data, sizeof part_data);
  dash_lldash_ring_hold(idx, len);
  ck_assert_int_eq(dash_lldash_ring_pending(idx), 0);
  dash_lldash_ring_ack(idx, len);
}
END_TEST

static int out_has(const conn_t *c, const void *needle, size_t n) {
  return memmem(c->out.buf + c->out.off, c->out.len - c->out.off, needle, n) != NULL;
}

static conn_t *plain_conn(int peer_and_fd[2]) {
  static config_t cfg;
  conn_t *c;

  reactor_set_context(&cfg, NULL, NULL);
  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, peer_and_fd), 0);
  conn_table_init(peer_and_fd[1] + 64);
  c = conn_new(peer_and_fd[0], NULL);
  ck_assert_ptr_nonnull(c);
  c->epfd = -1;
  return c;
}

START_TEST(try_attach_streams_chunked_framing_to_the_connection) {
  static const char hdr_ka[] = "Connection: keep-alive\r\n";
  pid_filter_t f;
  int sv[2];
  conn_t *c;
  int slot;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  no_filter(&f);
  c = plain_conn(sv);
  conn_publish(c);

  ck_assert_int_eq(dash_lldash_try_attach(c, CTX, &f, 0, &full, "bogus.txt", 1, NULL, -1), 0);
  ck_assert_int_eq(dash_lldash_try_attach(c, CTX, &f, 0, &full, SEG_NAME_0, 1, "http://o.example", -1), 1);
  slot = c->slot;
  ck_assert_int_ge(slot, 0);
  ck_assert_int_eq(c->next_state, CONN_NEXT_DASHCHUNK);
  ck_assert_int_eq(c->keep_alive, 1);
  ck_assert(out_has(c, "Transfer-Encoding: chunked\r\n", 28));
  ck_assert(out_has(c, hdr_ka, sizeof hdr_ka - 1));
  ck_assert(out_has(c, "Access-Control-Allow-Origin", 27));

  dash_lldash_sub_attach(slot);
  push_part(0);
  ck_assert(out_has(c, "\r\n40\r\n", 6) || out_has(c, "40\r\n", 4));
  ck_assert(out_has(c, part_data, sizeof part_data));
  ck_assert_int_eq(dash_lldash_sub_finalized(slot), 0);

  push_segment(0);
  ck_assert(out_has(c, "0\r\n\r\n", 5));
  ck_assert_int_eq(dash_lldash_sub_finalized(slot), 1);

  dash_lldash_sub_close(slot);
  conn_unpublish(c);
  conn_free(c);
  close(sv[0]);
  close(sv[1]);
}
END_TEST

START_TEST(h1_parts_and_terminator_in_publish_window_are_delivered) {
  pid_filter_t f;
  int sv[2];
  conn_t *c;
  int slot;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  no_filter(&f);
  c = plain_conn(sv);
  ck_assert_int_eq(dash_lldash_try_attach(c, CTX, &f, 0, &full, SEG_NAME_0, 1, NULL, -1), 1);
  slot = c->slot;

  push_part(0);
  push_segment(0);
  ck_assert_int_eq(dash_lldash_sub_finalized(slot), 0);
  ck_assert(!out_has(c, "0\r\n\r\n", 5));

  conn_publish(c);
  dash_lldash_sub_attach(slot);
  ck_assert(out_has(c, part_data, sizeof part_data));
  ck_assert(out_has(c, "0\r\n\r\n", 5));
  ck_assert_int_eq(dash_lldash_sub_finalized(slot), 1);

  dash_lldash_sub_close(slot);
  conn_unpublish(c);
  conn_free(c);
  close(sv[0]);
  close(sv[1]);
}
END_TEST

START_TEST(try_attach_without_keep_alive_announces_close) {
  pid_filter_t f;
  int sv[2];
  conn_t *c;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  no_filter(&f);
  c = plain_conn(sv);
  ck_assert_int_eq(dash_lldash_try_attach(c, CTX, &f, 0, &full, SEG_NAME_0, 0, NULL, -1), 1);
  ck_assert_int_eq(c->keep_alive, 0);
  ck_assert(out_has(c, "Connection: close\r\n", 19));
  dash_lldash_sub_close(c->slot);
  conn_free(c);
  close(sv[0]);
  close(sv[1]);
}
END_TEST

START_TEST(close_before_headers_flushed_releases_the_slot) {
  pid_filter_t f;
  int sv[2];
  conn_t *c;
  int slot;
  int again;

  open_store(0, SEG_CONTAINER_FMP4, 1);
  no_filter(&f);
  c = plain_conn(sv);
  ck_assert_int_eq(dash_lldash_try_attach(c, CTX, &f, 0, &full, SEG_NAME_0, 1, NULL, -1), 1);
  slot = c->slot;
  reactor_close(-1, c);
  capture_flush_deferred_quiescent();
  again = dash_lldash_subscribe(CTX, &f, 0, &full, SEG_NAME_0, CONN_PROTO_H1);
  ck_assert_int_eq(again, slot);
  dash_lldash_sub_close(again);
  close(sv[1]);
}
END_TEST

START_TEST(oversized_part_flags_the_ring_as_errored) {
  enum { BIG = 5 << 17 };
  pid_filter_t f;
  uint8_t *big = calloc(1, BIG);
  int idx;

  ck_assert_ptr_nonnull(big);
  open_store(0, SEG_CONTAINER_FMP4, 1);
  idx = subscribe(0, SEG_NAME_0, CONN_PROTO_H2);
  ck_assert_int_ge(idx, 0);
  no_filter(&f);
  hls_push_part(CTX, &f, 0, &full, SEG_CONTAINER_FMP4, big, BIG, 0.1, 1);
  free(big);
  ck_assert_int_eq(dash_lldash_ring_errored(idx), 1);
  ck_assert_int_eq(dash_lldash_ring_pending(idx), 0);
}
END_TEST

static Suite *lldash_suite(void) {
  Suite *s = suite_create("dipixy_lldash");
  TCase *tc = tcase_create("core");

  tcase_set_timeout(tc, 20);
  tcase_add_checked_fixture(tc, setup, NULL);
  tcase_add_test(tc, subscribe_rejects_unusable_requests);
  tcase_add_test(tc, slot_accessors_ignore_out_of_range_slots);
  tcase_add_test(tc, transport_binding_is_recorded_per_slot);
  tcase_add_test(tc, ring_peek_and_advance_consume_a_part);
  tcase_add_test(tc, part_reaches_matching_subscriber);
  tcase_add_test(tc, attach_replays_parts_pushed_before_attach);
  tcase_add_test(tc, attach_after_segment_done_replays_and_finalizes);
  tcase_add_test(tc, attach_after_store_closing_aborts);
  tcase_add_test(tc, part_skips_subscriber_of_other_store);
  tcase_add_test(tc, part_after_segment_done_skips_old_subscriber);
  tcase_add_test(tc, part_pushed_ignores_ts_container);
  tcase_add_test(tc, track_store_subscriber_gets_track_parts_only);
  tcase_add_test(tc, track_subscribe_needs_its_own_store);
  tcase_add_test(tc, segment_done_finalizes_matching_subscriber_only);
  tcase_add_test(tc, h1_subscriber_without_connection_still_finalizes);
  tcase_add_test(tc, store_close_aborts_h2_subscriber);
  tcase_add_test(tc, store_closing_flags_h1_subscriber_finalized_only);
  tcase_add_test(tc, store_closing_covers_every_segment_but_not_closed_subscribers);
  tcase_add_test(tc, store_chain_unlinks_head_middle_tail);
  tcase_add_test(tc, store_chain_unlink_of_non_member_is_a_noop);
  tcase_add_test(tc, sub_close_twice_frees_the_slot_once);
  tcase_add_test(tc, tid_chain_out_of_range_is_a_noop);
  tcase_add_test(tc, store_closing_races_in_flight_chunk_push);
  tcase_add_test(tc, store_close_races_segment_and_part_pushes);
  tcase_add_test(tc, try_attach_streams_chunked_framing_to_the_connection);
  tcase_add_test(tc, h1_parts_and_terminator_in_publish_window_are_delivered);
  tcase_add_test(tc, try_attach_without_keep_alive_announces_close);
  tcase_add_test(tc, close_before_headers_flushed_releases_the_slot);
  tcase_add_test(tc, oversized_part_flags_the_ring_as_errored);
#ifdef HAVE_HTTP2
  tcase_add_test(tc, tid_chain_wakes_only_owned_live_subscribers);
#endif
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(lldash_suite());
  int failed;
  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
