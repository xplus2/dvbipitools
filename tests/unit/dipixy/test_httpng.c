/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/core/htdocs.h"
#include "dipixy/dlna/gena.h"
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
  int n_tspush_raw;
  int n_ts_cold;
  int ts_cold_ret;
  ts_cold_park_req_t ts_cold;
  int n_content;
  int content_status;
  char content_type[64];
  size_t content_len;
  int content_has_body;
  size_t content_nextra;
  char content_sid[64];
  int n_ws;
  int n_401;
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
  g_fake.n_401++;
}

static void fake_content(void *conn, void *req, int status, const char *ctype, const httpng_hdr_t *extra, size_t nextra, uint8_t *body, size_t len, const char *origin) {
  (void)conn;
  (void)req;
  (void)origin;
  g_fake.n_content++;
  g_fake.content_status = status;
  snprintf(g_fake.content_type, sizeof g_fake.content_type, "%s", ctype ? ctype : "");
  g_fake.content_len = len;
  g_fake.content_has_body = body != NULL;
  g_fake.content_nextra = nextra;
  g_fake.content_sid[0] = '\0';
  for (size_t i = 0; i < nextra; i++)
    if (!strcmp(extra[i].name, "sid")) snprintf(g_fake.content_sid, sizeof g_fake.content_sid, "%s", extra[i].value);
  free(body);
}

static int fake_ts_cold(void *conn, void *req, const ts_cold_park_req_t *pr) {
  (void)conn;
  (void)req;
  g_fake.n_ts_cold++;
  g_fake.ts_cold = *pr;
  return g_fake.ts_cold_ret;
}

static void fake_hls(void *conn, void *req, int handled, const hls_resp_t *resp, const char *origin) {
  (void)conn;
  (void)req;
  (void)origin;
  g_fake.n_hls++;
  g_fake.hls_handled = handled;
  g_fake.hls_status = handled ? resp->status : 0;
  if (handled) hls_resp_body_release(resp->body, resp->owner, resp->zc);
}

static void fake_ws(void *conn, void *req) {
  (void)conn;
  (void)req;
  g_fake.n_ws++;
}

static int fake_admission(void *conn) {
  (void)conn;
  g_fake.n_admission++;
  return g_fake.admission;
}

static int fake_tspush(void *conn, void *req, int sub, int rawaudio) {
  (void)conn;
  (void)req;
  g_fake.n_tspush++;
  g_fake.n_tspush_raw += rawaudio;
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
  .ts_cold_try_park = fake_ts_cold,
  .respond_content = fake_content,
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
  htdocs_template_init(&g_cfg);
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

static void teardown(void) {
  gena_stop();
}

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

START_TEST(route_with_full_client_table_is_503) {
  const route_case_t *rc = &setup_cases[_i];

  open_stdin_source();
  occupy_client_table();
  run(rc->fn, &(call_t){.path = rc->path});
  ck_assert_int_eq(g_fake.n_status, 1);
  ck_assert_str_eq(g_fake.status, "503");
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

START_TEST(ts_route_dispatch_refusal_is_503_and_frees_the_slot) {
  open_stdin_source();
  g_fake.tspush_ret = 0;
  run(dispatch_ts_route, &(call_t){.path = "/stdin/ts"});
  ck_assert_int_eq(g_fake.n_tspush, 1);
  ck_assert_str_eq(g_fake.status, "503");
  capture_flush_deferred_quiescent();
  run(dispatch_ts_route, &(call_t){.path = "/stdin/ts"});
  ck_assert_int_eq(g_fake.n_tspush, 2);
  ck_assert_int_eq(g_fake.tspush_sub, 0);
}
END_TEST

START_TEST(ts_route_with_full_client_table_is_503_without_dispatch) {
  open_stdin_source();
  occupy_client_table();
  run(dispatch_ts_route, &(call_t){.path = "/stdin/ts"});
  ck_assert_int_eq(g_fake.n_tspush, 0);
  ck_assert_str_eq(g_fake.status, "503");
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
  {"/stdin/hls", HLS_COLD_HLS, SEG_CONTAINER_TS, "index.m3u8", 0, 14000},
  {"/stdin/hls-fmp4", HLS_COLD_HLS, SEG_CONTAINER_FMP4, "index.m3u8", 0, 14000},
  {"/stdin/llhls", HLS_COLD_LLHLS, SEG_CONTAINER_TS, "index_ll.m3u8", 0, 14000},
  {"/stdin/dash", HLS_COLD_DASH, SEG_CONTAINER_FMP4, "manifest.mpd", 0, 14000},
  {"/stdin/lldash", HLS_COLD_DASH, SEG_CONTAINER_FMP4, "manifest.mpd", 1, 14000},
  {"/stdin/mp4", HLS_COLD_MP4, SEG_CONTAINER_FMP4, "", 0, 14000},
};

static void (*route_fn_for(const char *path))(httpng_req_t *) {
  if (strstr(path, "mp4") && !strstr(path, "fmp4")) return &dispatch_mp4_route;
  if (strstr(path, "dash")) return &dispatch_dash_route;
  if (strstr(path, "llhls")) return &dispatch_llhls_route;
  return &dispatch_hls_route;
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
  capture_flush_deferred_quiescent();
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

START_TEST(mp4_route_dispatch_refusal_is_503_and_frees_the_slot) {
  int refused;

  open_stdin_source();
  g_fake.cold_ret = 1;
  run(dispatch_mp4_route, &(call_t){.path = "/stdin/mp4"});
  push_ready_store(SEG_CONTAINER_FMP4, 0.0);
  g_fake.mp4push_ret = 0;
  run(dispatch_mp4_route, &(call_t){.path = "/stdin/mp4"});
  ck_assert_int_eq(g_fake.n_mp4push, 1);
  ck_assert_str_eq(g_fake.status, "503");
  refused = g_fake.mp4push_sub;
  capture_flush_deferred_quiescent();
  g_fake.mp4push_ret = 1;
  run(dispatch_mp4_route, &(call_t){.path = "/stdin/mp4"});
  ck_assert_int_eq(g_fake.n_mp4push, 2);
  ck_assert_int_eq(g_fake.mp4push_sub, refused);
}
END_TEST

START_TEST(rawaudio_route_subscribes_with_the_rawaudio_flag) {
  open_stdin_source();
  run(dispatch_ts_route, &(call_t){.path = "/stdin/rawaudio"});
  ck_assert_int_eq(g_fake.n_tspush, 1);
  ck_assert_int_eq(g_fake.n_tspush_raw, 1);
  ck_assert_int_eq(g_fake.n_status, 0);
}
END_TEST

START_TEST(ts_head_answers_headers_only) {
  open_stdin_source();
  run(dispatch_ts_route, &(call_t){.path = "/stdin/ts", .is_head = 1});
  ck_assert_int_eq(g_fake.n_content, 1);
  ck_assert_int_eq(g_fake.content_status, 200);
  ck_assert_str_eq(g_fake.content_type, "video/mp2t");
  ck_assert_int_eq((int)(g_fake.content_len == HTTPNG_NO_LENGTH), 1);
  ck_assert_int_eq(g_fake.content_has_body, 0);
  ck_assert_int_eq(g_fake.n_tspush, 0);
}
END_TEST

START_TEST(rawaudio_head_is_audio_mpeg) {
  open_stdin_source();
  run(dispatch_ts_route, &(call_t){.path = "/stdin/rawaudio", .is_head = 1});
  ck_assert_str_eq(g_fake.content_type, "audio/mpeg");
}
END_TEST

START_TEST(ts_without_traffic_parks_when_startup_timeout_is_set) {
  open_stdin_source();
  g_cfg.ts_startup_timeout_s = 2.0;
  g_fake.ts_cold_ret = 1;
  run(dispatch_ts_route, &(call_t){.path = "/stdin/spts"});
  ck_assert_int_eq(g_fake.n_ts_cold, 1);
  ck_assert_int_eq(g_fake.ts_cold.spts, 1);
  ck_assert_int_eq(g_fake.ts_cold.rawaudio, 0);
  ck_assert_int_eq(g_fake.ts_cold.timeout_ms, 2000);
  ck_assert_int_eq(g_fake.n_tspush, 0);
  capture_close(g_fake.ts_cold.cap_ctx);
}
END_TEST

START_TEST(ts_park_refusal_streams_immediately) {
  open_stdin_source();
  g_cfg.ts_startup_timeout_s = 2.0;
  g_fake.ts_cold_ret = 0;
  run(dispatch_ts_route, &(call_t){.path = "/stdin/ts"});
  ck_assert_int_eq(g_fake.n_ts_cold, 1);
  ck_assert_int_eq(g_fake.n_tspush, 1);
}
END_TEST

static void dispatch_gena(const char *method, const char *path, const char *sid, const char *nt, const char *callback, const char *authz, const char *protocol) {
  char pathbuf[128];
  char method_buf[16];
  httpng_req_hdrs_t hdrs;

  snprintf(pathbuf, sizeof pathbuf, "%s", path);
  snprintf(method_buf, sizeof method_buf, "%s", method);
  memset(&hdrs, 0, sizeof hdrs);
  hdrs.method = method_buf;
  hdrs.path = pathbuf;
  hdrs.inm = "";
  hdrs.origin = "";
  hdrs.authz = authz;
  hdrs.protocol = protocol;
  hdrs.host = "";
  hdrs.soapaction = "";
  hdrs.sid = sid;
  hdrs.nt = nt;
  hdrs.callback = callback;
  httpng_dispatch(&g_ops, FAKE_CONN, FAKE_REQ, &hdrs, "192.0.2.1", -1);
}

static void dispatch_request(const char *method, const char *path, const char *authz, const char *protocol) {
  dispatch_gena(method, path, "", "", "", authz, protocol);
}

START_TEST(index_page_is_served) {
  static const char *const paths[] = {"/", "/index.html"};

  dispatch_request("GET", paths[_i], "", "");
  ck_assert_int_eq(g_fake.n_content, 1);
  ck_assert_int_eq(g_fake.content_status, 200);
  ck_assert_str_eq(g_fake.content_type, "text/html; charset=utf-8");
  ck_assert_int_eq(g_fake.content_has_body, 1);
}
END_TEST

START_TEST(index_page_head_has_length_but_no_body) {
  dispatch_request("HEAD", "/", "", "");
  ck_assert_int_eq(g_fake.n_content, 1);
  ck_assert_int_gt((int)g_fake.content_len, 0);
  ck_assert_int_eq(g_fake.content_has_body, 0);
}
END_TEST

START_TEST(index_page_is_404_without_status_ui) {
  g_cfg.no_status = 1;
  dispatch_request("GET", "/", "", "");
  ck_assert_str_eq(g_fake.status, "404");
  ck_assert_int_eq(g_fake.n_content, 0);
}
END_TEST

START_TEST(status_json_is_served) {
  dispatch_request("GET", "/ui/status.js", "", "");
  ck_assert_int_eq(g_fake.n_content, 1);
  ck_assert_str_eq(g_fake.content_type, "application/json");
  ck_assert_int_eq(g_fake.content_has_body, 1);
}
END_TEST

START_TEST(ui_paths_require_the_configured_credential) {
  snprintf(g_cfg.http_auth, sizeof g_cfg.http_auth, "%s", "Basic dTpw");
  dispatch_request("GET", "/", "", "");
  ck_assert_int_eq(g_fake.n_401, 1);
  ck_assert_int_eq(g_fake.n_content, 0);
  dispatch_request("GET", "/", "Basic dTpw", "");
  ck_assert_int_eq(g_fake.n_content, 1);
}
END_TEST

START_TEST(websocket_connect_accepts_both_ui_ws_spellings) {
  static const char *const paths[] = {"/ui/ws/", "/ui/ws"};

  dispatch_request("CONNECT", paths[_i], "", "websocket");
  ck_assert_int_eq(g_fake.n_ws, 1);
  ck_assert_int_eq(g_fake.n_status, 0);
}
END_TEST

START_TEST(websocket_connect_elsewhere_is_404) {
  dispatch_request("CONNECT", "/ui/other", "", "websocket");
  ck_assert_int_eq(g_fake.n_ws, 0);
  ck_assert_str_eq(g_fake.status, "404");
}
END_TEST

START_TEST(dlna_descriptors_are_served_when_enabled) {
  g_cfg.enable_dlna = 1;
  dispatch_request("GET", "/dlna/cd_scpd.xml", "", "");
  ck_assert_int_eq(g_fake.n_content, 1);
  ck_assert_str_eq(g_fake.content_type, "text/xml; charset=utf-8");
  ck_assert_int_eq(g_fake.content_has_body, 1);
}
END_TEST

START_TEST(dlna_descriptors_are_404_when_disabled) {
  dispatch_request("GET", "/dlna/cd_scpd.xml", "", "");
  ck_assert_int_eq(g_fake.n_content, 0);
  ck_assert_str_eq(g_fake.status, "404");
}
END_TEST

START_TEST(dlna_methods_without_dlna_are_405) {
  static const char *const methods[] = {"POST", "SUBSCRIBE", "UNSUBSCRIBE"};

  dispatch_request(methods[_i], "/dlna/cd_control", "", "");
  ck_assert_str_eq(g_fake.status, "405");
}
END_TEST

START_TEST(dlna_wrong_path_is_405) {
  g_cfg.enable_dlna = 1;
  dispatch_request("POST", "/dlna/nope", "", "");
  ck_assert_str_eq(g_fake.status, "405");
}
END_TEST

START_TEST(dlna_subscribe_returns_sid_and_timeout) {
  g_cfg.enable_dlna = 1;
  dispatch_gena("SUBSCRIBE", "/dlna/cd_event", "", "upnp:event", "<http://192.0.2.1:9/ev>", "", "");
  ck_assert_int_eq(g_fake.n_content, 1);
  ck_assert_int_eq(g_fake.content_status, 200);
  ck_assert_int_eq((int)g_fake.content_nextra, 2);
}
END_TEST

START_TEST(dlna_unsubscribe_is_empty_200) {
  g_cfg.enable_dlna = 1;
  char sid[sizeof g_fake.content_sid];

  dispatch_gena("SUBSCRIBE", "/dlna/cm_event", "", "upnp:event", "<http://192.0.2.1:9/ev>", "", "");
  ck_assert_int_eq(g_fake.n_content, 1);
  snprintf(sid, sizeof sid, "%s", g_fake.content_sid);
  ck_assert_str_ne(sid, "");
  dispatch_gena("UNSUBSCRIBE", "/dlna/cm_event", sid, "", "", "", "");
  ck_assert_int_eq(g_fake.n_content, 2);
  ck_assert_int_eq(g_fake.content_status, 200);
  ck_assert_int_eq((int)g_fake.content_len, 0);
}
END_TEST

START_TEST(dlna_control_without_soapaction_is_400) {
  g_cfg.enable_dlna = 1;
  dispatch_request("POST", "/dlna/cd_control", "", "");
  ck_assert_str_eq(g_fake.status, "400");
}
END_TEST

START_TEST(unknown_method_is_405) {
  dispatch_request("PUT", "/stdin/ts", "", "");
  ck_assert_str_eq(g_fake.status, "405");
}
END_TEST

static Suite *httpng_suite(void) {
  Suite *s = suite_create("dipixy_httpng");
  TCase *tc = tcase_create("routes");

  tcase_set_timeout(tc, 20);
  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_loop_test(tc, route_without_source_is_404, 0, SETUP_CASES);
  tcase_add_loop_test(tc, route_with_full_client_table_is_503, 0, SETUP_CASES);
  tcase_add_test(tc, ts_route_refused_by_admission_is_503);
  tcase_add_test(tc, ts_route_without_source_is_404);
  tcase_add_test(tc, ts_route_hands_the_subscription_to_the_protocol);
  tcase_add_test(tc, ts_route_dispatch_refusal_is_503_and_frees_the_slot);
  tcase_add_test(tc, ts_route_with_full_client_table_is_503_without_dispatch);
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
  tcase_add_test(tc, mp4_route_dispatch_refusal_is_503_and_frees_the_slot);
  tcase_add_test(tc, rawaudio_route_subscribes_with_the_rawaudio_flag);
  tcase_add_test(tc, ts_head_answers_headers_only);
  tcase_add_test(tc, rawaudio_head_is_audio_mpeg);
  tcase_add_test(tc, ts_without_traffic_parks_when_startup_timeout_is_set);
  tcase_add_test(tc, ts_park_refusal_streams_immediately);
  tcase_add_loop_test(tc, index_page_is_served, 0, 2);
  tcase_add_test(tc, index_page_head_has_length_but_no_body);
  tcase_add_test(tc, index_page_is_404_without_status_ui);
  tcase_add_test(tc, status_json_is_served);
  tcase_add_test(tc, ui_paths_require_the_configured_credential);
  tcase_add_loop_test(tc, websocket_connect_accepts_both_ui_ws_spellings, 0, 2);
  tcase_add_test(tc, websocket_connect_elsewhere_is_404);
  tcase_add_test(tc, dlna_descriptors_are_served_when_enabled);
  tcase_add_test(tc, dlna_descriptors_are_404_when_disabled);
  tcase_add_loop_test(tc, dlna_methods_without_dlna_are_405, 0, 3);
  tcase_add_test(tc, dlna_wrong_path_is_405);
  tcase_add_test(tc, dlna_subscribe_returns_sid_and_timeout);
  tcase_add_test(tc, dlna_unsubscribe_is_empty_200);
  tcase_add_test(tc, dlna_control_without_soapaction_is_400);
  tcase_add_test(tc, unknown_method_is_405);
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
