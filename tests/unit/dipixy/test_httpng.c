/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/dash/lldash.h"
#include "dipixy/httpng/httpng.h"
#include "dipixy/segment/mp4push.h"
#include "dipixy/segment/segment.h"
#include "dipixy/ts/capture/capture.h"
#include "dipixy/ts/lcevcselect.h"
#include "dipixy/ts/ts_push.h"
#include "dipixy/ws/ws_clients.h"

#define PROTO_H2 2
#define PROTO_H3 3
#define SEG_TARGET 2.0
#define PART_TARGET 0.5
#define CLIENT_SLOTS 4
#define FAKE_CONN ((void *)0x1000)
#define FAKE_REQ ((void *)0x2000)

typedef struct {
  int admission;
  int tspush_ret;
  int dashchunk_ret;
  int mp4push_ret;
  int cold_ret;
  int llhls_ret;
  int n_status;
  char status[8];
  int n_hls;
  int hls_handled;
  int hls_status;
  int n_tspush;
  int tspush_sub;
  int n_dashchunk;
  int dashchunk_sub;
  int n_mp4push;
  int mp4push_sub;
  int n_cold;
  hls_cold_park_req_t cold;
  char cold_file[64];
  int n_llhls;
  llhls_park_req_t llhls;
  char llhls_file[64];
  int n_admission;
} fake_t;

static fake_t g_fake;
static config_t g_cfg;

static void fake_status(void *conn, void *req, const char *status) {
  (void)conn;
  (void)req;
  g_fake.n_status++;
  snprintf(g_fake.status, sizeof g_fake.status, "%s", status);
}

static void fake_401(void *conn, void *req) {
  (void)conn;
  (void)req;
}

static void fake_hls(void *conn, void *req, int handled, const hls_resp_t *resp, const char *origin) {
  (void)conn;
  (void)req;
  (void)origin;
  g_fake.n_hls++;
  g_fake.hls_handled = handled;
  g_fake.hls_status = handled ? resp->status : 0;
  if (handled) hls_resp_body_release(resp->body, resp->zc);
}

static void fake_ws(void *conn, void *req) {
  (void)conn;
  (void)req;
}

static int fake_admission(void *conn) {
  (void)conn;
  g_fake.n_admission++;
  return g_fake.admission;
}

static int fake_tspush(void *conn, void *req, int sub) {
  (void)conn;
  (void)req;
  g_fake.n_tspush++;
  g_fake.tspush_sub = sub;
  return g_fake.tspush_ret;
}

static int fake_dashchunk(void *conn, void *req, int sub, int ws_handle) {
  (void)conn;
  (void)req;
  (void)ws_handle;
  g_fake.n_dashchunk++;
  g_fake.dashchunk_sub = sub;
  return g_fake.dashchunk_ret;
}

static int fake_mp4push(void *conn, void *req, int sub, int ws_handle) {
  (void)conn;
  (void)req;
  (void)ws_handle;
  g_fake.n_mp4push++;
  g_fake.mp4push_sub = sub;
  return g_fake.mp4push_ret;
}

static int fake_cold(void *conn, void *req, const hls_cold_park_req_t *pr) {
  (void)conn;
  (void)req;
  g_fake.n_cold++;
  g_fake.cold = *pr;
  snprintf(g_fake.cold_file, sizeof g_fake.cold_file, "%s", pr->filename);
  return g_fake.cold_ret;
}

static int fake_llhls(void *conn, void *req, const llhls_park_req_t *pr) {
  (void)conn;
  (void)req;
  g_fake.n_llhls++;
  g_fake.llhls = *pr;
  snprintf(g_fake.llhls_file, sizeof g_fake.llhls_file, "%s", pr->filename);
  return g_fake.llhls_ret;
}

static const httpng_ops_t g_ops = {
  .proto = PROTO_H2,
  .respond_status = fake_status,
  .respond_401 = fake_401,
  .respond_hls = fake_hls,
  .ws_dispatch = fake_ws,
  .admission_ok = fake_admission,
  .tspush_dispatch = fake_tspush,
  .dashchunk_dispatch = fake_dashchunk,
  .mp4push_dispatch = fake_mp4push,
  .hls_cold_try_park = fake_cold,
  .llhls_try_park = fake_llhls,
};

typedef struct {
  const char *path;
  const char *query;
  const char *client_ip;
  int is_head;
  int proto;
} call_t;

static void run(void (*fn)(httpng_req_t *), const call_t *call) {
  route_t rt;
  pid_filter_t filter;
  lcevc_select_t lcevc;
  route_item_bufs_t bufs;
  client_info_t cinfo;
  httpng_ops_t ops = g_ops;
  httpng_req_t rq;
  char path[128];

  snprintf(path, sizeof path, "%s", call->path);
  ck_assert_int_eq(route_parse(path, &rt), 0);
  memset(&filter, 0, sizeof filter);
  memset(&lcevc, 0, sizeof lcevc);
  lcevc.mode = LCEVC_SEL_FULL;
  if (call->proto) ops.proto = call->proto;
  memset(&rq, 0, sizeof rq);
  rq.ops = &ops;
  rq.conn = FAKE_CONN;
  rq.req = FAKE_REQ;
  rq.rt = &rt;
  rq.filter = &filter;
  rq.lcevc = &lcevc;
  rq.item_bufs = &bufs;
  rq.cinfo = &cinfo;
  rq.client_ip = call->client_ip ? call->client_ip : "192.0.2.1";
  rq.fd = -1;
  rq.is_head = call->is_head;
  rq.query = call->query;
  fn(&rq);
}

static void setup(void) {
  memset(&g_fake, 0, sizeof g_fake);
  g_fake.admission = 1;
  g_fake.tspush_ret = 1;
  g_fake.dashchunk_ret = 1;
  g_fake.mp4push_ret = 1;
  memset(&g_cfg, 0, sizeof g_cfg);
  g_cfg.segment_size = SEG_TARGET;
  g_cfg.segment_count = 6;
  g_cfg.hls_part_size = PART_TARGET;
  g_cfg.dash_part_size = PART_TARGET;
  reactor_set_context(&g_cfg, NULL, NULL);
  ws_clients_init(CLIENT_SLOTS);
  hls_store_init(8);
  hls_seg_init(8);
  ts_push_init(0, 8);
  dash_lldash_init(8);
  mp4push_init(8);
}

static void open_stdin_source(void) {
  int fd = open("/dev/null", O_RDONLY);

  ck_assert_int_ge(fd, 0);
  ck_assert_int_ge(dup2(fd, STDIN_FILENO), 0);
  close(fd);
  ck_assert_int_eq(capture_stdin_init(), 0);
}

static void occupy_client_table(void) {
  char ip[16];
  client_info_t other = {.http_ver = PROTO_H2, .ip = ip};

  for (int i = 0; i < CLIENT_SLOTS; i++) {
    snprintf(ip, sizeof ip, "198.51.100.%d", i + 1);
    ck_assert_int_ge(ws_clients_add_persistent(&other), 0);
  }
}

static void push_ready_store(seg_container_t container, double part_target) {
  capture_ctx_t *ctx = capture_stdin_get();
  pid_filter_t filter;
  lcevc_select_t lcevc = {LCEVC_SEL_FULL, 0, 0};
  const uint8_t chunk[64] = {0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p'};

  ck_assert_ptr_nonnull(ctx);
  memset(&filter, 0, sizeof filter);
  if (part_target > 0.0) hls_llhls_enable(ctx, &filter, 0, &lcevc, container, part_target);
  if (container == SEG_CONTAINER_FMP4) {
    ck_assert_int_eq(hls_push_part(ctx, &filter, 0, &lcevc, container, chunk, sizeof chunk, 0.5, 1), 0);
    ck_assert_int_eq(hls_push_segment_ll(ctx, &filter, 0, &lcevc, container, 2.0), 0);
  } else {
    ck_assert_int_eq(hls_push_segment(ctx, &filter, 0, &lcevc, container, chunk, sizeof chunk, 2.0), 0);
  }
  capture_close(ctx);
}

typedef struct {
  void (*fn)(httpng_req_t *);
  const char *path;
} route_case_t;

static const route_case_t setup_cases[] = {
  {dispatch_hls_route, "/stdin/hls"},
  {dispatch_hls_route, "/stdin/hls-fmp4"},
  {dispatch_llhls_route, "/stdin/llhls"},
  {dispatch_dash_route, "/stdin/dash"},
  {dispatch_dash_route, "/stdin/lldash"},
  {dispatch_mp4_route, "/stdin/mp4"},
};
#define SETUP_CASES (int)(sizeof setup_cases / sizeof setup_cases[0])

START_TEST(route_without_source_is_404) {
  const route_case_t *rc = &setup_cases[_i];

  run(rc->fn, &(call_t){.path = rc->path});
  ck_assert_int_eq(g_fake.n_status, 1);
  ck_assert_str_eq(g_fake.status, "404");
  ck_assert_int_eq(g_fake.n_hls, 0);
  ck_assert_int_eq(g_fake.n_cold, 0);
}
END_TEST

START_TEST(route_with_full_client_table_is_501) {
  const route_case_t *rc = &setup_cases[_i];

  open_stdin_source();
  occupy_client_table();
  run(rc->fn, &(call_t){.path = rc->path});
  ck_assert_int_eq(g_fake.n_status, 1);
  ck_assert_str_eq(g_fake.status, "501");
  ck_assert_int_eq(g_fake.n_hls, 0);
  ck_assert_int_eq(g_fake.n_cold, 0);
}
END_TEST

START_TEST(ts_route_refused_by_admission_is_503) {
  open_stdin_source();
  g_fake.admission = 0;
  run(dispatch_ts_route, &(call_t){.path = "/stdin/ts"});
  ck_assert_int_eq(g_fake.n_admission, 1);
  ck_assert_str_eq(g_fake.status, "503");
  ck_assert_int_eq(g_fake.n_tspush, 0);
}
END_TEST

START_TEST(ts_route_without_source_is_404) {
  run(dispatch_ts_route, &(call_t){.path = "/stdin/ts"});
  ck_assert_str_eq(g_fake.status, "404");
  ck_assert_int_eq(g_fake.n_tspush, 0);
}
END_TEST

START_TEST(ts_route_hands_the_subscription_to_the_protocol) {
  static const char *const paths[] = {"/stdin/ts", "/stdin/spts"};

  open_stdin_source();
  for (int i = 0; i < 2; i++) {
    g_fake.n_status = 0;
    run(dispatch_ts_route, &(call_t){.path = paths[i]});
    ck_assert_int_eq(g_fake.n_tspush, i + 1);
    ck_assert_int_ge(g_fake.tspush_sub, 0);
    ck_assert_int_eq(g_fake.n_status, 0);
  }
}
END_TEST

START_TEST(ts_route_dispatch_refusal_is_501_and_frees_the_slot) {
  open_stdin_source();
  g_fake.tspush_ret = 0;
  run(dispatch_ts_route, &(call_t){.path = "/stdin/ts"});
  ck_assert_int_eq(g_fake.n_tspush, 1);
  ck_assert_str_eq(g_fake.status, "501");
  capture_flush_deferred_quiescent();
  run(dispatch_ts_route, &(call_t){.path = "/stdin/ts"});
  ck_assert_int_eq(g_fake.n_tspush, 2);
  ck_assert_int_eq(g_fake.tspush_sub, 0);
}
END_TEST

START_TEST(ts_route_with_full_client_table_is_501_without_dispatch) {
  open_stdin_source();
  occupy_client_table();
  run(dispatch_ts_route, &(call_t){.path = "/stdin/ts"});
  ck_assert_int_eq(g_fake.n_tspush, 0);
  ck_assert_str_eq(g_fake.status, "501");
}
END_TEST

typedef struct {
  const char *path;
  hls_cold_kind_t kind;
  seg_container_t container;
  const char *file;
  int want_ll;
  int timeout_ms;
} park_case_t;

static const park_case_t park_cases[] = {
  {"/stdin/hls", HLS_COLD_HLS, SEG_CONTAINER_TS, "index.m3u8", 0, 4000},
  {"/stdin/hls-fmp4", HLS_COLD_HLS, SEG_CONTAINER_FMP4, "index.m3u8", 0, 4000},
  {"/stdin/llhls", HLS_COLD_LLHLS, SEG_CONTAINER_TS, "index_ll.m3u8", 0, 4000},
  {"/stdin/dash", HLS_COLD_DASH, SEG_CONTAINER_FMP4, "manifest.mpd", 0, 4000},
  {"/stdin/lldash", HLS_COLD_DASH, SEG_CONTAINER_FMP4, "manifest.mpd", 1, 4000},
  {"/stdin/mp4", HLS_COLD_MP4, SEG_CONTAINER_FMP4, "", 0, 4000},
};

static void (*route_fn_for(const char *path))(httpng_req_t *) {
  if (strstr(path, "mp4") && !strstr(path, "fmp4")) return dispatch_mp4_route;
  if (strstr(path, "dash")) return dispatch_dash_route;
  if (strstr(path, "llhls")) return dispatch_llhls_route;
  return dispatch_hls_route;
}

START_TEST(cold_index_is_parked_with_its_request) {
  const park_case_t *pc = &park_cases[_i];

  open_stdin_source();
  g_fake.cold_ret = 1;
  run(route_fn_for(pc->path), &(call_t){.path = pc->path, .is_head = _i & 1});
  ck_assert_int_eq(g_fake.n_cold, 1);
  ck_assert_int_eq(g_fake.cold.kind, pc->kind);
  ck_assert_int_eq(g_fake.cold.container, pc->container);
  ck_assert_str_eq(g_fake.cold_file, pc->file);
  ck_assert_int_eq(g_fake.cold.want_ll, pc->want_ll);
  ck_assert_int_eq(g_fake.cold.is_head, _i & 1);
  ck_assert_int_eq(g_fake.cold.timeout_ms, pc->timeout_ms);
  ck_assert_int_eq(g_fake.n_hls, 0);
  ck_assert_int_eq(g_fake.n_status, 0);
}
END_TEST

START_TEST(cold_index_with_full_waiter_table_falls_through_to_render) {
  open_stdin_source();
  g_fake.cold_ret = 0;
  run(dispatch_hls_route, &(call_t){.path = "/stdin/hls"});
  ck_assert_int_eq(g_fake.n_cold, 1);
  ck_assert_int_eq(g_fake.n_hls, 1);
  run(dispatch_dash_route, &(call_t){.path = "/stdin/dash"});
  ck_assert_int_eq(g_fake.n_cold, 2);
  ck_assert_int_eq(g_fake.n_hls, 2);
  run(dispatch_llhls_route, &(call_t){.path = "/stdin/llhls"});
  ck_assert_int_eq(g_fake.n_cold, 3);
  ck_assert_int_eq(g_fake.n_hls, 3);
}
END_TEST

START_TEST(hls_ready_index_renders_without_parking) {
  open_stdin_source();
  run(dispatch_hls_route, &(call_t){.path = "/stdin/hls"});
  push_ready_store(SEG_CONTAINER_TS, 0.0);
  g_fake.n_hls = 0;
  g_fake.n_cold = 0;
  run(dispatch_hls_route, &(call_t){.path = "/stdin/hls"});
  ck_assert_int_eq(g_fake.n_cold, 0);
  ck_assert_int_eq(g_fake.n_hls, 1);
  ck_assert_int_eq(g_fake.hls_handled, 1);
  ck_assert_int_eq(g_fake.hls_status, 200);
}
END_TEST

START_TEST(hls_segment_file_renders_without_parking) {
  open_stdin_source();
  g_fake.cold_ret = 1;
  run(dispatch_hls_route, &(call_t){.path = "/stdin/seg0.ts"});
  ck_assert_int_eq(g_fake.n_cold, 0);
  ck_assert_int_eq(g_fake.n_hls, 1);
  ck_assert_int_eq(g_fake.hls_handled, 1);
  ck_assert_int_eq(g_fake.hls_status, 404);
}
END_TEST

START_TEST(llhls_blocking_reload_parks_until_the_part_exists) {
  open_stdin_source();
  run(dispatch_llhls_route, &(call_t){.path = "/stdin/llhls"});
  push_ready_store(SEG_CONTAINER_TS, PART_TARGET);
  g_fake.n_cold = 0;
  g_fake.n_hls = 0;
  g_fake.llhls_ret = 1;
  run(dispatch_llhls_route, &(call_t){.path = "/stdin/llhls", .query = "_HLS_msn=900&_HLS_part=2"});
  ck_assert_int_eq(g_fake.n_cold, 0);
  ck_assert_int_eq(g_fake.n_llhls, 1);
  ck_assert_uint_eq(g_fake.llhls.want_seg, 900);
  ck_assert_int_eq(g_fake.llhls.want_part, 2);
  ck_assert_int_eq(g_fake.llhls.timeout_ms, 1000);
  ck_assert_str_eq(g_fake.llhls_file, "index_ll.m3u8");
  ck_assert_int_eq(g_fake.n_hls, 0);
}
END_TEST

START_TEST(llhls_park_refusal_or_plain_request_renders) {
  open_stdin_source();
  run(dispatch_llhls_route, &(call_t){.path = "/stdin/llhls"});
  push_ready_store(SEG_CONTAINER_TS, PART_TARGET);
  g_fake.n_hls = 0;
  g_fake.llhls_ret = 0;
  run(dispatch_llhls_route, &(call_t){.path = "/stdin/llhls", .query = "_HLS_msn=900&_HLS_part=2"});
  ck_assert_int_eq(g_fake.n_llhls, 1);
  ck_assert_int_eq(g_fake.n_hls, 1);
  run(dispatch_llhls_route, &(call_t){.path = "/stdin/llhls"});
  ck_assert_int_eq(g_fake.n_llhls, 1);
  ck_assert_int_eq(g_fake.n_hls, 2);
  run(dispatch_llhls_route, &(call_t){.path = "/stdin/seg0.1.ts", .query = "_HLS_msn=900&_HLS_part=2"});
  ck_assert_int_eq(g_fake.n_llhls, 1);
  ck_assert_int_eq(g_fake.n_hls, 3);
}
END_TEST

START_TEST(dash_manifest_ready_renders_without_parking) {
  open_stdin_source();
  run(dispatch_dash_route, &(call_t){.path = "/stdin/dash"});
  push_ready_store(SEG_CONTAINER_FMP4, 0.0);
  g_fake.n_cold = 0;
  g_fake.n_hls = 0;
  run(dispatch_dash_route, &(call_t){.path = "/stdin/dash"});
  ck_assert_int_eq(g_fake.n_cold, 0);
  ck_assert_int_eq(g_fake.n_hls, 1);
  ck_assert_int_eq(g_fake.hls_handled, 1);
}
END_TEST

typedef struct {
  const char *path;
  int is_head;
  int no_lldash;
  int proto;
  int dispatch_ret;
  int expect_dispatch;
  int expect_render;
} dashseg_case_t;

static const dashseg_case_t dashseg_cases[] = {
  {"/stdin/dseg2000.m4s", 0, 0, PROTO_H2, 1, 1, 0},
  {"/stdin/dseg2000.m4s", 0, 0, PROTO_H3, 1, 1, 0},
  {"/stdin/dseg2000.m4s", 0, 0, PROTO_H2, 0, 1, 1},
  {"/stdin/dseg2000.m4s", 1, 0, PROTO_H2, 1, 0, 1},
  {"/stdin/dseg0.m4s", 0, 1, PROTO_H2, 1, 0, 1},
  {"/stdin/dseg0.m4s", 0, 0, PROTO_H2, 1, 0, 1},
};

START_TEST(dash_segment_request_prefers_progressive_delivery) {
  const dashseg_case_t *dc = &dashseg_cases[_i];

  open_stdin_source();
  run(dispatch_dash_route, &(call_t){.path = "/stdin/lldash"});
  push_ready_store(SEG_CONTAINER_FMP4, PART_TARGET);
  g_cfg.no_lldash = dc->no_lldash;
  g_fake.dashchunk_ret = dc->dispatch_ret;
  g_fake.n_cold = 0;
  g_fake.n_hls = 0;
  run(dispatch_dash_route, &(call_t){.path = dc->path, .is_head = dc->is_head, .proto = dc->proto});
  ck_assert_int_eq(g_fake.n_cold, 0);
  ck_assert_int_eq(g_fake.n_dashchunk, dc->expect_dispatch);
  ck_assert_int_eq(g_fake.n_hls, dc->expect_render);
  if (dc->expect_dispatch) ck_assert_int_ge(g_fake.dashchunk_sub, 0);
}
END_TEST

START_TEST(dash_refused_progressive_delivery_releases_the_subscription) {
  int refused;

  open_stdin_source();
  run(dispatch_dash_route, &(call_t){.path = "/stdin/lldash"});
  push_ready_store(SEG_CONTAINER_FMP4, PART_TARGET);
  g_fake.dashchunk_ret = 0;
  run(dispatch_dash_route, &(call_t){.path = "/stdin/dseg2000.m4s"});
  ck_assert_int_eq(g_fake.n_dashchunk, 1);
  refused = g_fake.dashchunk_sub;
  g_fake.dashchunk_ret = 1;
  run(dispatch_dash_route, &(call_t){.path = "/stdin/dseg2000.m4s"});
  ck_assert_int_eq(g_fake.n_dashchunk, 2);
  ck_assert_int_eq(g_fake.dashchunk_sub, refused);
}
END_TEST

START_TEST(mp4_route_hands_the_subscription_to_the_protocol) {
  open_stdin_source();
  g_fake.cold_ret = 1;
  run(dispatch_mp4_route, &(call_t){.path = "/stdin/mp4"});
  push_ready_store(SEG_CONTAINER_FMP4, 0.0);
  g_fake.n_cold = 0;
  g_fake.n_status = 0;
  run(dispatch_mp4_route, &(call_t){.path = "/stdin/mp4"});
  ck_assert_int_eq(g_fake.n_cold, 0);
  ck_assert_int_eq(g_fake.n_mp4push, 1);
  ck_assert_int_ge(g_fake.mp4push_sub, 0);
  ck_assert_int_eq(g_fake.n_status, 0);
}
END_TEST

START_TEST(mp4_route_dispatch_refusal_is_501_and_frees_the_slot) {
  int refused;

  open_stdin_source();
  g_fake.cold_ret = 1;
  run(dispatch_mp4_route, &(call_t){.path = "/stdin/mp4"});
  push_ready_store(SEG_CONTAINER_FMP4, 0.0);
  g_fake.mp4push_ret = 0;
  run(dispatch_mp4_route, &(call_t){.path = "/stdin/mp4"});
  ck_assert_int_eq(g_fake.n_mp4push, 1);
  ck_assert_str_eq(g_fake.status, "501");
  refused = g_fake.mp4push_sub;
  capture_flush_deferred_quiescent();
  g_fake.mp4push_ret = 1;
  run(dispatch_mp4_route, &(call_t){.path = "/stdin/mp4"});
  ck_assert_int_eq(g_fake.n_mp4push, 2);
  ck_assert_int_eq(g_fake.mp4push_sub, refused);
}
END_TEST

static Suite *httpng_suite(void) {
  Suite *s = suite_create("dipixy_httpng");
  TCase *tc = tcase_create("routes");

  tcase_set_timeout(tc, 20);
  tcase_add_checked_fixture(tc, setup, NULL);
  tcase_add_loop_test(tc, route_without_source_is_404, 0, SETUP_CASES);
  tcase_add_loop_test(tc, route_with_full_client_table_is_501, 0, SETUP_CASES);
  tcase_add_test(tc, ts_route_refused_by_admission_is_503);
  tcase_add_test(tc, ts_route_without_source_is_404);
  tcase_add_test(tc, ts_route_hands_the_subscription_to_the_protocol);
  tcase_add_test(tc, ts_route_dispatch_refusal_is_501_and_frees_the_slot);
  tcase_add_test(tc, ts_route_with_full_client_table_is_501_without_dispatch);
  tcase_add_loop_test(tc, cold_index_is_parked_with_its_request, 0, (int)(sizeof park_cases / sizeof park_cases[0]));
  tcase_add_test(tc, cold_index_with_full_waiter_table_falls_through_to_render);
  tcase_add_test(tc, hls_ready_index_renders_without_parking);
  tcase_add_test(tc, hls_segment_file_renders_without_parking);
  tcase_add_test(tc, llhls_blocking_reload_parks_until_the_part_exists);
  tcase_add_test(tc, llhls_park_refusal_or_plain_request_renders);
  tcase_add_test(tc, dash_manifest_ready_renders_without_parking);
  tcase_add_loop_test(tc, dash_segment_request_prefers_progressive_delivery, 0, (int)(sizeof dashseg_cases / sizeof dashseg_cases[0]));
  tcase_add_test(tc, dash_refused_progressive_delivery_releases_the_subscription);
  tcase_add_test(tc, mp4_route_hands_the_subscription_to_the_protocol);
  tcase_add_test(tc, mp4_route_dispatch_refusal_is_501_and_frees_the_slot);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(httpng_suite());
  int failed;

  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
