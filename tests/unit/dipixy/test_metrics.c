/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "dipixy/core/metrics.h"
#include "dipixy/ts/ts_push.h"

#define THREADS 4
#define NOTES_PER_THREAD 5000
#define SOCK_NAME "m.sock"
#define METRICS_ID "xy-test"

typedef struct {
  const char *name;
  const char *type;
} family_t;

static const family_t families[] = {
  {"dvbipi_xy_connections_total", "counter"},
  {"dvbipi_xy_connections_active", "gauge"},
  {"dvbipi_xy_requests_total", "counter"},
  {"dvbipi_xy_http_errors_total", "counter"},
  {"dvbipi_xy_bytes_served_total", "counter"},
  {"dvbipi_xy_sources_active", "gauge"},
  {"dvbipi_xy_tspush_subscribers_active", "gauge"},
};
#define FAMILIES (int)(sizeof families / sizeof families[0])

static char g_dir[64];
static char g_path[108];
static int g_rfd = -1;

static unsigned long long prom_value(const char *name) {
  char *out;
  size_t len;
  char key[96];
  const char *p;

  ck_assert_int_eq(dipixy_metrics_render_prometheus(&out, &len), 0);
  snprintf(key, sizeof key, "\n%s ", name);
  p = strstr(out, key);
  ck_assert_ptr_nonnull(p);
  return strtoull(p + strlen(key), NULL, 10);
}

START_TEST(prometheus_text_declares_every_family_once) {
  char *out;
  size_t len;
  char pat[160];
  const family_t *f = &families[_i];
  const char *p;

  ck_assert_int_eq(dipixy_metrics_render_prometheus(&out, &len), 0);
  snprintf(pat, sizeof pat, "# TYPE %s %s\n", f->name, f->type);
  p = strstr(out, pat);
  ck_assert_ptr_nonnull(p);
  ck_assert_ptr_null(strstr(p + 1, pat));
  snprintf(pat, sizeof pat, "# HELP %s ", f->name);
  ck_assert_ptr_nonnull(strstr(out, pat));
  snprintf(pat, sizeof pat, "\n%s ", f->name);
  ck_assert_ptr_nonnull(strstr(out, pat));
}
END_TEST

START_TEST(prometheus_text_is_complete_and_newline_terminated) {
  char *out;
  size_t len;

  ck_assert_int_eq(dipixy_metrics_render_prometheus(&out, &len), 0);
  ck_assert_uint_eq(len, strlen(out));
  ck_assert_uint_gt(len, 0);
  ck_assert_uint_lt(len, 2048);
  ck_assert_int_eq(out[len - 1], '\n');
}
END_TEST

START_TEST(prometheus_sample_lines_are_name_and_integer) {
  char *out;
  size_t len;
  int samples = 0;
  char *save = NULL;
  char *copy;

  ck_assert_int_eq(dipixy_metrics_render_prometheus(&out, &len), 0);
  copy = strdup(out);
  ck_assert_ptr_nonnull(copy);
  for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
    char *sp;
    char *end;

    if (line[0] == '#') continue;
    sp = strchr(line, ' ');
    ck_assert_ptr_nonnull(sp);
    ck_assert_ptr_null(strchr(sp + 1, ' '));
    ck_assert_int_eq(strncmp(line, "dvbipi_xy_", 10), 0);
    (void)strtoull(sp + 1, &end, 10);
    ck_assert_int_eq(*end, '\0');
    ck_assert_ptr_ne(end, sp + 1);
    samples++;
  }
  ck_assert_int_eq(samples, FAMILIES);
  free(copy);
}
END_TEST

START_TEST(counters_start_at_zero) {
  ck_assert_uint_eq(prom_value("dvbipi_xy_requests_total"), 0);
  ck_assert_uint_eq(prom_value("dvbipi_xy_http_errors_total"), 0);
  ck_assert_uint_eq(prom_value("dvbipi_xy_connections_total"), 0);
  ck_assert_uint_eq(prom_value("dvbipi_xy_bytes_served_total"), 0);
  ck_assert_uint_eq(prom_value("dvbipi_xy_sources_active"), 0);
  ck_assert_uint_eq(prom_value("dvbipi_xy_tspush_subscribers_active"), 0);
}
END_TEST

START_TEST(notes_bump_their_own_counter_only) {
  for (int i = 0; i < 3; i++) dipixy_metrics_note_request();
  for (int i = 0; i < 2; i++) dipixy_metrics_note_http_error();
  ck_assert_uint_eq(prom_value("dvbipi_xy_requests_total"), 3);
  ck_assert_uint_eq(prom_value("dvbipi_xy_http_errors_total"), 2);
  dipixy_metrics_note_http_error();
  ck_assert_uint_eq(prom_value("dvbipi_xy_requests_total"), 3);
  ck_assert_uint_eq(prom_value("dvbipi_xy_http_errors_total"), 3);
}
END_TEST

static void *note_loop(void *arg) {
  (void)arg;
  for (int i = 0; i < NOTES_PER_THREAD; i++) {
    dipixy_metrics_note_request();
    dipixy_metrics_note_http_error();
    dipixy_metrics_note_http_error();
  }
  return NULL;
}

START_TEST(concurrent_notes_are_not_lost) {
  pthread_t t[THREADS];

  for (int i = 0; i < THREADS; i++) pthread_create(&t[i], NULL, note_loop, NULL);
  for (int i = 0; i < THREADS; i++) pthread_join(t[i], NULL);
  ck_assert_uint_eq(prom_value("dvbipi_xy_requests_total"), (unsigned long long)THREADS * NOTES_PER_THREAD);
  ck_assert_uint_eq(prom_value("dvbipi_xy_http_errors_total"), 2ull * THREADS * NOTES_PER_THREAD);
}
END_TEST

typedef struct {
  char *out;
  size_t len;
  char copy[2048];
} render_t;

static void *render_once(void *arg) {
  render_t *r = arg;

  dipixy_metrics_note_request();
  if (dipixy_metrics_render_prometheus(&r->out, &r->len)) return NULL;
  memcpy(r->copy, r->out, r->len + 1);
  return r;
}

START_TEST(prometheus_buffer_is_per_thread) {
  render_t other;
  char *mine;
  size_t mine_len;
  char before[2048];
  pthread_t t;

  ck_assert_int_eq(dipixy_metrics_render_prometheus(&mine, &mine_len), 0);
  memcpy(before, mine, mine_len + 1);
  memset(&other, 0, sizeof other);
  pthread_create(&t, NULL, render_once, &other);
  pthread_join(t, NULL);
  ck_assert_ptr_nonnull(other.out);
  ck_assert_ptr_ne(other.out, mine);
  ck_assert_str_eq(mine, before);
  ck_assert_str_ne(other.copy, before);
}
END_TEST

static void recv_open(void) {
  struct sockaddr_un addr;

  snprintf(g_dir, sizeof g_dir, "/tmp/dipixy-metrics-XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(g_dir));
  snprintf(g_path, sizeof g_path, "%s/%s", g_dir, SOCK_NAME);
  g_rfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  ck_assert_int_ge(g_rfd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof addr.sun_path, "%s", g_path);
  ck_assert_int_eq(bind(g_rfd, (struct sockaddr *)&addr, sizeof addr), 0);
}

static void recv_close(void) {
  if (g_rfd >= 0) close(g_rfd);
  g_rfd = -1;
  unlink(g_path);
  rmdir(g_dir);
}

static void exporter_open(metrics_exporter_t *mx, const char *id) {
  config_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.metrics_id = id;
  cfg.metrics_sock = g_path;
  cfg.metrics_interval_s = 60;
  dipixy_metrics_init(mx, &cfg);
}

static int next_snapshot(metrics_hdr_t *hdr, unsigned long long *seen, size_t cap) {
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  metrics_reader_t r;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;
  ssize_t n = recv(g_rfd, buf, sizeof buf, 0);

  if (n <= 0) return -1;
  ck_assert_int_eq(metrics_reader_init(&r, buf, (size_t)n, hdr), 0);
  memset(seen, 0, cap * sizeof *seen);
  while (metrics_reader_next(&r, &id, label, sizeof label, &value) == 1)
    if (id >= METRICS_ID_XY_CONNECTIONS_TOTAL && id <= METRICS_ID_XY_TSPUSH_SUBS_ACTIVE) seen[id - METRICS_ID_XY_CONNECTIONS_TOTAL] = value + 1;
  return 0;
}

START_TEST(push_sends_one_snapshot_with_the_counters) {
  metrics_exporter_t mx;
  metrics_hdr_t hdr;
  unsigned long long seen[7];

  recv_open();
  exporter_open(&mx, METRICS_ID);
  ck_assert_int_eq(metrics_exporter_enabled(&mx), 1);
  for (int i = 0; i < 7; i++) dipixy_metrics_note_request();
  for (int i = 0; i < 2; i++) dipixy_metrics_note_http_error();
  dipixy_metrics_push(&mx);
  ck_assert_int_eq(next_snapshot(&hdr, seen, 7), 0);
  ck_assert_int_eq(hdr.component, METRICS_COMPONENT_XY);
  ck_assert_str_eq(hdr.metrics_id, METRICS_ID);
  ck_assert_uint_eq(hdr.sequence, 1);
  ck_assert_uint_eq(seen[METRICS_ID_XY_REQUESTS_TOTAL - METRICS_ID_XY_CONNECTIONS_TOTAL], 7 + 1);
  ck_assert_uint_eq(seen[METRICS_ID_XY_HTTP_ERRORS_TOTAL - METRICS_ID_XY_CONNECTIONS_TOTAL], 2 + 1);
  for (int i = 0; i < 7; i++) ck_assert_uint_ne(seen[i], 0);
  dipixy_metrics_close(&mx);
  recv_close();
}
END_TEST

START_TEST(push_is_gated_by_the_interval) {
  metrics_exporter_t mx;
  metrics_hdr_t hdr;
  unsigned long long seen[7];

  recv_open();
  exporter_open(&mx, METRICS_ID);
  dipixy_metrics_push(&mx);
  dipixy_metrics_push(&mx);
  dipixy_metrics_push(&mx);
  ck_assert_int_eq(next_snapshot(&hdr, seen, 7), 0);
  ck_assert_int_ne(next_snapshot(&hdr, seen, 7), 0);
  dipixy_metrics_close(&mx);
  recv_close();
}
END_TEST

START_TEST(push_without_metrics_id_stays_silent) {
  metrics_exporter_t mx;
  metrics_hdr_t hdr;
  unsigned long long seen[7];

  recv_open();
  exporter_open(&mx, NULL);
  ck_assert_int_eq(metrics_exporter_enabled(&mx), 0);
  dipixy_metrics_push(&mx);
  ck_assert_int_ne(next_snapshot(&hdr, seen, 7), 0);
  dipixy_metrics_close(&mx);
  recv_close();
}
END_TEST

START_TEST(close_disables_the_exporter_and_is_repeatable) {
  metrics_exporter_t mx;
  metrics_hdr_t hdr;
  unsigned long long seen[7];

  recv_open();
  exporter_open(&mx, METRICS_ID);
  dipixy_metrics_close(&mx);
  ck_assert_int_eq(metrics_exporter_enabled(&mx), 0);
  dipixy_metrics_push(&mx);
  dipixy_metrics_close(&mx);
  ck_assert_int_ne(next_snapshot(&hdr, seen, 7), 0);
  recv_close();
}
END_TEST

static unsigned count_id(const metrics_writer_t *w, metrics_id_t want) {
  metrics_reader_t r;
  metrics_hdr_t hdr;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t v;
  unsigned n = 0;

  ck_assert_int_eq(metrics_reader_init(&r, w->buf, w->len, &hdr), 0);
  while (metrics_reader_next(&r, &id, label, sizeof label, &v) == 1) n += id == want;
  return n;
}

START_TEST(queue_metrics_extend_with_the_level) {
  static const struct {
    int level;
    unsigned watermark;
    unsigned dropped;
  } cases[] = {{0, 0, 0}, {1, 0, 0}, {2, 1, 1}, {3, 1, 1}};
  metrics_exporter_t mx;
  metrics_writer_t w;
  int level = cases[_i].level;

  recv_open();
  exporter_open(&mx, METRICS_ID);
  ck_assert_int_eq(metrics_exporter_begin(&mx, &w, "1.0"), 0);
  dipixy_put_queue_metrics(&w, &level);
  ck_assert_uint_eq(count_id(&w, METRICS_ID_XY_TSPUSH_QUEUE_BYTES), 1);
  ck_assert_uint_eq(count_id(&w, METRICS_ID_XY_TSPUSH_QUEUE_MAX_BYTES), 1);
  ck_assert_uint_eq(count_id(&w, METRICS_ID_XY_TSPUSH_QUEUE_MILLISECONDS), 0);
  ck_assert_uint_eq(count_id(&w, METRICS_ID_XY_TSPUSH_QUEUE_HIGH_WATERMARK_BYTES), cases[_i].watermark);
  ck_assert_uint_eq(count_id(&w, METRICS_ID_XY_TSPUSH_QUEUE_DROPPED_TOTAL), cases[_i].dropped);
  dipixy_metrics_close(&mx);
  recv_close();
}
END_TEST

static void setup(void) {
  ts_push_init(0, 8);
}

static Suite *metrics_suite(void) {
  Suite *s = suite_create("dipixy_metrics");
  TCase *tc = tcase_create("core");

  tcase_set_timeout(tc, 20);
  tcase_add_checked_fixture(tc, setup, NULL);
  tcase_add_loop_test(tc, prometheus_text_declares_every_family_once, 0, FAMILIES);
  tcase_add_test(tc, prometheus_text_is_complete_and_newline_terminated);
  tcase_add_test(tc, prometheus_sample_lines_are_name_and_integer);
  tcase_add_test(tc, counters_start_at_zero);
  tcase_add_test(tc, notes_bump_their_own_counter_only);
  tcase_add_test(tc, concurrent_notes_are_not_lost);
  tcase_add_test(tc, prometheus_buffer_is_per_thread);
  tcase_add_test(tc, push_sends_one_snapshot_with_the_counters);
  tcase_add_test(tc, push_is_gated_by_the_interval);
  tcase_add_test(tc, push_without_metrics_id_stays_silent);
  tcase_add_test(tc, close_disables_the_exporter_and_is_repeatable);
  tcase_add_loop_test(tc, queue_metrics_extend_with_the_level, 0, 4);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(metrics_suite());
  int failed;

  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
