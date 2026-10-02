/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "dipiradiohead/cli/args.h"
#include "dipiradiohead/input/inputset.h"
#include "lib/sys/ioutil.h"

#include "http_fixture.h"

static void noop_meta_cb(void *ctx, const char *artist, const char *title) {
  (void)ctx;
  (void)artist;
  (void)title;
}

static void drive_all(inputset_t *is, int max_iters) {
  int i;
  time_t now = time(NULL);

  for (i = 0; i < max_iters; i++) {
    unsigned idx;
    struct pollfd pfds[RADIOHEAD_MAX_INPUTS];
    nfds_t n = 0;

    for (idx = 0; idx < inputset_count(is); idx++)
      inputset_service(is, idx, now);
    for (idx = 0; idx < inputset_count(is); idx++) {
      int fd = inputset_poll_fd(is, idx);
      if (fd < 0)
        continue;
      pfds[n].fd = fd;
      pfds[n].events = inputset_poll_events(is, idx);
      pfds[n].revents = 0;
      n++;
    }
    if (n > 0)
      poll(pfds, n, 20);
    else
      usleep(5000);
  }
}

START_TEST(inputset_pid_allocation_is_index_based) {
  config_t cfg;
  inputset_t *is;

  memset(&cfg, 0, sizeof cfg);
  cfg.n_inputs = 3;
  cfg.inputs[0].uri = "http://127.0.0.1:1/a";
  cfg.inputs[0].sid = 10;
  bufcpy(cfg.inputs[0].sdt_text, sizeof cfg.inputs[0].sdt_text, "A");
  cfg.inputs[1].uri = "http://127.0.0.1:1/b";
  cfg.inputs[1].sid = 20;
  bufcpy(cfg.inputs[1].sdt_text, sizeof cfg.inputs[1].sdt_text, "B");
  cfg.inputs[2].uri = "http://127.0.0.1:1/c";
  cfg.inputs[2].sid = 30;
  bufcpy(cfg.inputs[2].sdt_text, sizeof cfg.inputs[2].sdt_text, "C");
  cfg.error_retry_s = 1;

  is = inputset_new(&cfg, noop_meta_cb, NULL, NULL, NULL);
  ck_assert_ptr_nonnull(is);
  ck_assert_uint_eq(inputset_count(is), 3u);

  ck_assert_uint_eq(inputset_pmt_pid(is, 0), 0x1000u);
  ck_assert_uint_eq(inputset_pmt_pid(is, 1), 0x1001u);
  ck_assert_uint_eq(inputset_pmt_pid(is, 2), 0x1002u);
  ck_assert_uint_eq(inputset_audio_pid(is, 0), 0x0100u);
  ck_assert_uint_eq(inputset_audio_pid(is, 1), 0x0101u);
  ck_assert_uint_eq(inputset_sid(is, 1), 20u);
  ck_assert_str_eq(inputset_service_name(is, 2), "C");

  inputset_free(is);
}
END_TEST

START_TEST(inputset_connects_and_reports_source) {
  unsigned port;
  int listen_fd = fixture_listener(&port);
  pthread_t th;
  http_fixture_t fx;
  const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nbody-bytes";
  char uri[64];
  config_t cfg;
  inputset_t *is;

  fixture_single(&fx, listen_fd, resp, strlen(resp));
  ck_assert_int_eq(pthread_create(&th, NULL, fixture_serve, &fx), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  memset(&cfg, 0, sizeof cfg);
  cfg.n_inputs = 1;
  cfg.inputs[0].uri = uri;
  cfg.inputs[0].sid = 1;
  bufcpy(cfg.inputs[0].sdt_text, sizeof cfg.inputs[0].sdt_text, "Test");

  is = inputset_new(&cfg, noop_meta_cb, NULL, NULL, NULL);
  ck_assert_ptr_nonnull(is);
  ck_assert_ptr_null(inputset_source(is, 0));

  drive_all(is, 300);
  ck_assert_ptr_nonnull(inputset_source(is, 0));

  inputset_mark_down(is, 0, time(NULL));
  ck_assert_ptr_null(inputset_source(is, 0));

  inputset_free(is);
  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(inputset_retries_independently_per_slot) {
  unsigned port;
  int listen_fd = fixture_listener(&port);
  pthread_t th;
  http_fixture_t fx;
  const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nbody-bytes";
  char uri_ok[64];
  config_t cfg;
  inputset_t *is;
  time_t now;

  fixture_single(&fx, listen_fd, resp, strlen(resp));
  ck_assert_int_eq(pthread_create(&th, NULL, fixture_serve, &fx), 0);

  snprintf(uri_ok, sizeof uri_ok, "http://127.0.0.1:%u/stream", port);
  memset(&cfg, 0, sizeof cfg);
  cfg.n_inputs = 2;
  cfg.inputs[0].uri = uri_ok;
  cfg.inputs[0].sid = 1;
  bufcpy(cfg.inputs[0].sdt_text, sizeof cfg.inputs[0].sdt_text, "OK");
  cfg.inputs[1].uri = "http://127.0.0.1:1/dead"; /* nothing listens here */
  cfg.inputs[1].sid = 2;
  bufcpy(cfg.inputs[1].sdt_text, sizeof cfg.inputs[1].sdt_text, "Dead");
  /* error_retry_s left 0: n_inputs > 1 must still auto-default to a retry, never give up */

  is = inputset_new(&cfg, noop_meta_cb, NULL, NULL, NULL);
  ck_assert_ptr_nonnull(is);

  drive_all(is, 300);
  ck_assert_ptr_nonnull(inputset_source(is, 0)); /* good source connected */
  ck_assert_ptr_null(inputset_source(is, 1));    /* dead source stayed down ... */

  now = time(NULL);
  ck_assert_int_ne(inputset_next_deadline(is), INPUTSET_NEVER); /* ... but scheduled to retry */
  ck_assert(inputset_next_deadline(is) >= now);

  inputset_free(is);
  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(inputset_single_input_no_retry_when_error_retry_s_is_zero) {
  config_t cfg;
  inputset_t *is;

  memset(&cfg, 0, sizeof cfg);
  cfg.n_inputs = 1;
  cfg.inputs[0].uri = "http://127.0.0.1:1/dead";
  cfg.inputs[0].sid = 1;
  bufcpy(cfg.inputs[0].sdt_text, sizeof cfg.inputs[0].sdt_text, "Dead");
  cfg.error_retry_s = 0;

  is = inputset_new(&cfg, noop_meta_cb, NULL, NULL, NULL);
  ck_assert_ptr_nonnull(is);

  drive_all(is, 100);
  ck_assert_ptr_null(inputset_source(is, 0));
  ck_assert_int_eq(inputset_next_deadline(is), INPUTSET_NEVER);

  inputset_free(is);
}
END_TEST

START_TEST(inputset_accessors_report_the_configured_slots) {
  config_t cfg;
  inputset_t *is;

  memset(&cfg, 0, sizeof cfg);
  cfg.n_inputs = 3;
  cfg.error_retry_s = 7;
  bufcpy(cfg.default_provider_text, sizeof cfg.default_provider_text, "Default Prov");
  for (unsigned i = 0; i < 3; i++) {
    cfg.inputs[i].uri = "http://127.0.0.1:1/x";
    cfg.inputs[i].sid = 100 + i;
  }
  bufcpy(cfg.inputs[0].sdt_text, sizeof cfg.inputs[0].sdt_text, "Alpha");
  bufcpy(cfg.inputs[1].sdt_text, sizeof cfg.inputs[1].sdt_text, "Beta");
  bufcpy(cfg.inputs[2].sdt_text, sizeof cfg.inputs[2].sdt_text, "Gamma");
  bufcpy(cfg.inputs[0].provider_text, sizeof cfg.inputs[0].provider_text, "Own Prov 0");
  bufcpy(cfg.inputs[2].provider_text, sizeof cfg.inputs[2].provider_text, "Own Prov 2");

  is = inputset_new(&cfg, noop_meta_cb, NULL, NULL, NULL);
  ck_assert_ptr_nonnull(is);
  ck_assert_uint_eq(inputset_count(is), 3u);
  for (unsigned i = 0; i < 3; i++) {
    ck_assert_uint_eq(inputset_sid(is, i), 100 + i);
    ck_assert_uint_eq(inputset_pmt_pid(is, i), 0x1000u + i);
    ck_assert_uint_eq(inputset_audio_pid(is, i), 0x0100u + i);
    ck_assert_int_eq(inputset_poll_fd(is, i), -1);
    ck_assert_int_eq(inputset_poll_events(is, i), 0);
  }
  ck_assert_str_eq(inputset_service_name(is, 0), "Alpha");
  ck_assert_str_eq(inputset_service_name(is, 1), "Beta");
  ck_assert_str_eq(inputset_service_name(is, 2), "Gamma");
  ck_assert_str_eq(inputset_provider_name(is, 0), "Own Prov 0");
  ck_assert_str_eq(inputset_provider_name(is, 1), "Default Prov");
  ck_assert_str_eq(inputset_provider_name(is, 2), "Own Prov 2");
  ck_assert_int_eq((int)inputset_next_deadline(is), 0);
  inputset_free(is);
}
END_TEST

START_TEST(inputset_poll_accessors_follow_the_slot_state) {
  unsigned port;
  int listen_fd = fixture_listener(&port);
  pthread_t th;
  http_fixture_t fx;
  const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nbody-bytes";
  char uri[64];
  config_t cfg;
  inputset_t *is;

  fixture_single(&fx, listen_fd, resp, strlen(resp));
  ck_assert_int_eq(pthread_create(&th, NULL, fixture_serve, &fx), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  memset(&cfg, 0, sizeof cfg);
  cfg.n_inputs = 1;
  cfg.inputs[0].uri = uri;
  cfg.inputs[0].sid = 1;
  bufcpy(cfg.inputs[0].sdt_text, sizeof cfg.inputs[0].sdt_text, "Test");
  cfg.error_retry_s = 30;

  is = inputset_new(&cfg, noop_meta_cb, NULL, NULL, NULL);
  ck_assert_ptr_nonnull(is);
  ck_assert_int_eq(inputset_poll_fd(is, 0), -1);

  drive_all(is, 300);
  ck_assert_ptr_nonnull(inputset_source(is, 0));
  ck_assert_int_ge(inputset_poll_fd(is, 0), 0);
  ck_assert_int_eq(inputset_poll_events(is, 0), POLLIN);

  inputset_mark_down(is, 0, 1000);
  ck_assert_int_eq(inputset_poll_fd(is, 0), -1);
  ck_assert_int_eq(inputset_poll_events(is, 0), 0);
  ck_assert_int_eq((int)inputset_next_deadline(is), 1030);

  inputset_free(is);
  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

static Suite *inputset_suite(void) {
  Suite *s = suite_create("inputset");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, inputset_pid_allocation_is_index_based);
  tcase_add_test(tc, inputset_accessors_report_the_configured_slots);
  tcase_add_test(tc, inputset_connects_and_reports_source);
  tcase_add_test(tc, inputset_poll_accessors_follow_the_slot_state);
  tcase_add_test(tc, inputset_retries_independently_per_slot);
  tcase_add_test(tc, inputset_single_input_no_retry_when_error_retry_s_is_zero);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(inputset_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
