/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <poll.h>
#include <sys/socket.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/sys/signal.h"

#include "dipiradiohead/radiohead/priv.h"
#include "input/http_fixture.h"

#define MP3_FRAME_LEN FIXTURE_MP3_FRAME_LEN
#define MP3_FRAMES 20
#define NOW 1000.0

static const mpts_program_ops_t no_program_ops = {NULL, NULL, NULL};

typedef struct {
  int listen_fd;
  int served;
  pthread_t th;
  http_fixture_t fx;
  unsigned char resp[24576];
  config_t cfg;
  char uri[64];
  inputset_t *is;
  mpts_t *mpts;
  tspacketizer_t *tsps[1];
  meta_state_t metas[1];
  uint64_t timeline[1];
  double pace_deadline[1];
  int was_connected[1];
  input_metrics_t input_stats[1];
  unsigned long long last_synced_bytes[1];
  radio_metrics_t rm;
  out_ctx_t out;
  mpts_tick_t tk;
} rig_t;

static void noop_meta_cb(void *ctx, const char *artist, const char *title) {
  (void)ctx;
  (void)artist;
  (void)title;
}

static void rig_init(rig_t *r, const char *uri_or_null, const unsigned char *body, size_t len) {
  static const char head[] = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\n\r\n";
  psi_pat_entry_t entries[1] = {{7, 0x1000}};

  memset(r, 0, sizeof *r);
  r->cfg.n_inputs = 1;
  r->cfg.inputs[0].sid = 7;
  bufcpy(r->cfg.inputs[0].sdt_text, sizeof r->cfg.inputs[0].sdt_text, "Svc");
  r->cfg.tsid = 1;
  r->cfg.onid = 1;
  if (uri_or_null) {
    bufcpy(r->uri, sizeof r->uri, uri_or_null);
  } else {
    unsigned port;

    if (!body) abort();
    ck_assert_uint_le(sizeof head - 1 + len, sizeof r->resp);
    memcpy(r->resp, head, sizeof head - 1);
    memcpy(r->resp + sizeof head - 1, body, len);
    r->listen_fd = fixture_listener(&port);
    fixture_single(&r->fx, r->listen_fd, (const char *)r->resp, sizeof head - 1 + len);
    ck_assert_int_eq(pthread_create(&r->th, NULL, fixture_serve, &r->fx), 0);
    r->served = 1;
    snprintf(r->uri, sizeof r->uri, "http://127.0.0.1:%u/stream", port);
  }
  r->cfg.inputs[0].uri = r->uri;
  r->is = inputset_new(&r->cfg, noop_meta_cb, NULL, NULL, NULL);
  ck_assert_ptr_nonnull(r->is);
  r->mpts = mpts_new(1, 1, "", entries, 1, &no_program_ops);
  ck_assert_ptr_nonnull(r->mpts);

  r->tk.is = r->is;
  r->tk.mpts = r->mpts;
  r->tk.cfg = &r->cfg;
  r->tk.tsps = r->tsps;
  r->tk.metas = r->metas;
  r->tk.timeline = r->timeline;
  r->tk.pace_deadline = r->pace_deadline;
  r->tk.was_connected = r->was_connected;
  r->tk.input_stats = r->input_stats;
  r->tk.last_synced_bytes = r->last_synced_bytes;
  r->tk.rm = &r->rm;
  r->tk.out = &r->out;
  r->tk.metrics_on = 1;
  r->tk.now = NOW;
  r->tk.now_t = 1000;
}

static void rig_connect(rig_t *r) {
  for (int i = 0; i < 300 && !inputset_source(r->is, 0); i++) {
    struct pollfd pfd;
    int fd;

    inputset_service(r->is, 0, 1000);
    fd = inputset_poll_fd(r->is, 0);
    if (fd < 0) continue;
    pfd.fd = fd;
    pfd.events = inputset_poll_events(r->is, 0);
    pfd.revents = 0;
    poll(&pfd, 1, 20);
  }
  ck_assert_ptr_nonnull(inputset_source(r->is, 0));
}

static void rig_free(rig_t *r) {
  if (r->tsps[0]) tspacketizer_free(r->tsps[0]);
  mpts_free(r->mpts);
  inputset_free(r->is);
  if (r->served) {
    pthread_join(r->th, NULL);
    close(r->listen_fd);
  }
}

static void wait_readable(const rig_t *r) {
  struct pollfd pfd;

  pfd.fd = inputset_poll_fd(r->is, 0);
  pfd.events = POLLIN;
  pfd.revents = 0;
  if (pfd.fd >= 0) poll(&pfd, 1, 50);
}

typedef struct {
  const char *name;
  unsigned slot[3];
  short revents[3];
  nfds_t npfd;
  uint32_t want;
} ready_case_t;

static const ready_case_t ready_cases[] = {
    {"no descriptors", {0, 0, 0}, {0, 0, 0}, 0, 0u},
    {"nothing ready", {0, 1, 2}, {0, 0, 0}, 3, 0u},
    {"readable", {0, 1, 2}, {POLLIN, 0, POLLIN}, 3, 0x5u},
    {"error counts as ready", {3, 4, 5}, {POLLERR, 0, 0}, 3, 0x8u},
    {"hangup counts as ready", {3, 4, 5}, {0, POLLHUP, 0}, 3, 0x10u},
    {"writable alone does not count", {0, 1, 2}, {POLLOUT, 0, 0}, 3, 0u},
    {"sparse slots", {7, 31, 2}, {POLLIN, POLLIN, 0}, 3, 0x80000080u},
    {"only the first npfd entries count", {0, 1, 2}, {POLLIN, POLLIN, POLLIN}, 2, 0x3u},
};

START_TEST(compute_ready_mask_sets_one_bit_per_ready_slot) {
  const ready_case_t *c = &ready_cases[_i];
  struct pollfd pfds[3];

  for (int i = 0; i < 3; i++) {
    pfds[i].fd = -1;
    pfds[i].events = POLLIN;
    pfds[i].revents = c->revents[i];
  }
  ck_assert_msg(compute_ready_mask(c->slot, pfds, c->npfd) == c->want, "%s: mask 0x%x", c->name, compute_ready_mask(c->slot, pfds, c->npfd));
}
END_TEST

START_TEST(slot_without_a_source_is_skipped) {
  rig_t *r = calloc(1, sizeof *r);

  if (!r) abort();
  rig_init(r, "http://127.0.0.1:1/dead", NULL, 0);
  inputset_service(r->is, 0, 1000);
  r->tk.ready_mask = 1;
  ck_assert_int_eq(process_input_slot(&r->tk, 0), 0);
  ck_assert_int_eq(r->was_connected[0], 0);
  ck_assert_ptr_null(r->tsps[0]);
  ck_assert_uint_eq(r->out.packets, 0u);
  rig_free(r);
  free(r);
}
END_TEST

START_TEST(first_sight_of_a_connected_source_resets_the_slot_clock) {
  static unsigned char body[100];
  rig_t *r = calloc(1, sizeof *r);

  if (!r) abort();
  rig_init(r, NULL, body, sizeof body);
  rig_connect(r);
  r->timeline[0] = 99;
  r->last_synced_bytes[0] = 77;
  r->pace_deadline[0] = 5.0;
  r->tk.ready_mask = 0;
  ck_assert_int_eq(process_input_slot(&r->tk, 0), 0);
  ck_assert_int_eq(r->was_connected[0], 1);
  ck_assert_uint_eq(r->timeline[0], 0u);
  ck_assert_uint_eq(r->last_synced_bytes[0], 0u);
  ck_assert_double_eq(r->pace_deadline[0], NOW);
  ck_assert_ptr_null(r->tsps[0]);
  ck_assert_uint_eq(r->out.packets, 0u);
  rig_free(r);
  free(r);
}
END_TEST

START_TEST(ready_slot_creates_the_program_and_feeds_paced_frames) {
  static unsigned char body[MP3_FRAMES * MP3_FRAME_LEN];
  rig_t *r = calloc(1, sizeof *r);
  unsigned long long frames;

  if (!r) abort();
  fixture_mp3_frames(body, MP3_FRAMES);
  rig_init(r, NULL, body, sizeof body);
  rig_connect(r);
  r->tk.ready_mask = 1;
  bufcpy(r->metas[0].artist, sizeof r->metas[0].artist, "Artist");
  bufcpy(r->metas[0].title, sizeof r->metas[0].title, "Title");
  r->metas[0].dirty = 1;
  for (int i = 0; i < 50 && !r->tsps[0]; i++) {
    wait_readable(r);
    ck_assert_int_eq(process_input_slot(&r->tk, 0), 0);
  }
  ck_assert_ptr_nonnull(r->tsps[0]);
  frames = r->rm.frames_total[SRC_MPEG_AUDIO];
  ck_assert_uint_ge(frames, 1u);
  ck_assert_uint_le(frames, 13u);
  ck_assert_uint_le(llabs((long long)timeline_pts(r->timeline[0]) - (long long)(frames * 1152u * 90000u / 44100u)), 1);
  ck_assert_double_eq_tol(r->pace_deadline[0], NOW + (double)(frames * 1152u) / 44100.0, 1e-6);
  ck_assert_int_eq(r->metas[0].dirty, 0);
  ck_assert_int_eq(tspacketizer_eit_pending(r->tsps[0]), 1);
  ck_assert_uint_gt(r->out.packets, 0u);
  ck_assert_uint_gt(r->input_stats[0].bytes_total, 0u);
  ck_assert(r->input_stats[0].last_data_time > 0.0);
  ck_assert_uint_eq(r->rm.framing_errors_total, 0u);
  rig_free(r);
  free(r);
}
END_TEST

START_TEST(codec_change_on_an_existing_program_updates_the_packetizer) {
  static unsigned char body[MP3_FRAMES * MP3_FRAME_LEN];
  rig_t *r = calloc(1, sizeof *r);
  unsigned long long frames;

  if (!r) abort();
  fixture_mp3_frames(body, MP3_FRAMES);
  rig_init(r, NULL, body, sizeof body);
  rig_connect(r);
  r->tk.ready_mask = 1;
  for (int i = 0; i < 50 && !r->tsps[0]; i++) {
    wait_readable(r);
    ck_assert_int_eq(process_input_slot(&r->tk, 0), 0);
  }
  ck_assert_ptr_nonnull(r->tsps[0]);
  ck_assert_int_eq(tspacketizer_set_codec(r->tsps[0], 0x0F, 0x29), 1);
  frames = r->rm.frames_total[SRC_MPEG_AUDIO];
  r->pace_deadline[0] = 0.0;
  for (int i = 0; i < 50 && r->rm.frames_total[SRC_MPEG_AUDIO] == frames; i++) {
    wait_readable(r);
    ck_assert_int_eq(process_input_slot(&r->tk, 0), 0);
  }
  ck_assert_uint_gt(r->rm.frames_total[SRC_MPEG_AUDIO], frames);
  ck_assert_int_eq(tspacketizer_set_codec(r->tsps[0], 0x0F, 0x29), 1);
  rig_free(r);
  free(r);
}
END_TEST

START_TEST(pacing_deadline_ahead_of_the_clock_defers_all_frames) {
  static unsigned char body[MP3_FRAMES * MP3_FRAME_LEN];
  rig_t *r = calloc(1, sizeof *r);

  if (!r) abort();
  fixture_mp3_frames(body, MP3_FRAMES);
  rig_init(r, NULL, body, sizeof body);
  rig_connect(r);
  r->was_connected[0] = 1;
  r->pace_deadline[0] = NOW + 10.0;
  r->tk.ready_mask = 1;
  wait_readable(r);
  ck_assert_int_eq(process_input_slot(&r->tk, 0), 0);
  ck_assert_uint_eq(r->rm.frames_total[SRC_MPEG_AUDIO], 0u);
  ck_assert_ptr_null(r->tsps[0]);
  ck_assert_double_eq(r->pace_deadline[0], NOW + 10.0);
  rig_free(r);
  free(r);
}
END_TEST

START_TEST(framing_failure_marks_the_input_down_and_counts_the_error) {
  static unsigned char body[20000];
  rig_t *r = calloc(1, sizeof *r);

  if (!r) abort();
  rig_init(r, NULL, body, sizeof body);
  rig_connect(r);
  r->input_stats[0].up = 1;
  r->tk.ready_mask = 1;
  for (int i = 0; i < 100 && inputset_source(r->is, 0); i++) {
    wait_readable(r);
    ck_assert_int_eq(process_input_slot(&r->tk, 0), 0);
  }
  ck_assert_ptr_null(inputset_source(r->is, 0));
  ck_assert_uint_eq(r->rm.framing_errors_total, 1u);
  ck_assert_int_eq(r->input_stats[0].up, 0);
  ck_assert_uint_eq(r->input_stats[0].errors_total[NET_ERR_FORMAT], 1u);
  ck_assert_ptr_null(r->tsps[0]);
  rig_free(r);
  free(r);
}
END_TEST

typedef struct {
  const char *name;
  int cas;
  int cas_bad;
  int mcast_bad;
  int inspect;
  int want_rc;
} run_case_t;

static const run_case_t run_cases[] = {
  {"plain output", 0, 0, 0, 0, 0},
  {"inspectors enabled", 0, 0, 0, 1, 0},
  {"own cas", 1, 0, 0, 0, 0},
  {"cas that cannot start", 1, 1, 0, 0, 1},
  {"multicast target that cannot open", 0, 0, 1, 0, 1},
};

START_TEST(run_mpts_sets_up_and_tears_down_for_each_configuration) {
  const run_case_t *c = &run_cases[_i];
  static config_t cfg;
  metrics_exporter_t mx;
  int rc;

  memset(&cfg, 0, sizeof cfg);
  cfg.n_inputs = 2;
  cfg.tsid = 1;
  cfg.onid = 1;
  for (unsigned i = 0; i < 2; i++) {
    cfg.inputs[i].sid = i + 1;
    cfg.inputs[i].uri = "http://127.0.0.1:1/stream";
    bufcpy(cfg.inputs[i].sdt_text, sizeof cfg.inputs[i].sdt_text, "Svc");
  }
  if (c->inspect) cfg.metrics_inspect_ts = METRICS_INSPECT_TS_BASIC;
  if (c->cas) {
    cfg.cas_algo = CAS_ALGO_CSA2;
    cfg.cas_cp_duration_ms = c->cas_bad ? 0 : 10000;
    cfg.n_cas_vendors = 1;
    bufcpy(cfg.cas_vendors[0].ecmg_host, sizeof cfg.cas_vendors[0].ecmg_host, "127.0.0.1");
    cfg.cas_vendors[0].ecmg_port = 1;
    cfg.cas_vendors[0].super_cas_id = (0x4A75u << 16) | 1u;
    cfg.cas_vendors[0].ecm_id = 1;
    cfg.cas_vendors[0].ecm_pid = 0x1FF0;
    cfg.cas_vendors[0].emm_pid = 0x1FF1;
  }
  if (c->mcast_bad) {
    cfg.family = AF_INET;
    bufcpy(cfg.mcast_group, sizeof cfg.mcast_group, "not-an-address");
    cfg.mcast_port = 5000;
  }
  metrics_exporter_init(&mx, METRICS_COMPONENT_RADIOHEAD, NULL, NULL, 0.0);
  signals_install();
  raise(SIGTERM);
  rc = radiohead_run_mpts(&cfg, &mx);
  ck_assert_msg(rc == c->want_rc, "%s: rc %d", c->name, rc);
}
END_TEST

static Suite *mpts_suite(void) {
  Suite *s = suite_create("dipiradiohead_mpts");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_loop_test(tc, compute_ready_mask_sets_one_bit_per_ready_slot, 0, (int)(sizeof ready_cases / sizeof ready_cases[0]));
  tcase_add_test(tc, slot_without_a_source_is_skipped);
  tcase_add_test(tc, first_sight_of_a_connected_source_resets_the_slot_clock);
  tcase_add_test(tc, ready_slot_creates_the_program_and_feeds_paced_frames);
  tcase_add_test(tc, codec_change_on_an_existing_program_updates_the_packetizer);
  tcase_add_test(tc, pacing_deadline_ahead_of_the_clock_defers_all_frames);
  tcase_add_test(tc, framing_failure_marks_the_input_down_and_counts_the_error);
  tcase_add_loop_test(tc, run_mpts_sets_up_and_tears_down_for_each_configuration, 0, (int)(sizeof run_cases / sizeof run_cases[0]));
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(mpts_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
