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

#include "dipiradiohead/args.h"
#include "dipiradiohead/input/inputset.h"

typedef struct {
  int listen_fd;
  const char *response;
  size_t response_len;
} server_arg_t;

static void *serve_once(void *arg) {
  server_arg_t *a = arg;
  int cfd = accept(a->listen_fd, NULL, NULL);
  struct timeval tv = {2, 0};
  char buf[4096];
  size_t got = 0;

  if (cfd < 0)
    return NULL;
  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (;;) {
    ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
    if (n <= 0)
      break;
    got += (size_t)n;
    if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0)
      break;
  }
  send(cfd, a->response, a->response_len, 0);
  close(cfd);
  return NULL;
}

static int make_listener(unsigned *port_out) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 1), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

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
  snprintf(cfg.inputs[0].sdt_text, sizeof cfg.inputs[0].sdt_text, "A");
  cfg.inputs[1].uri = "http://127.0.0.1:1/b";
  cfg.inputs[1].sid = 20;
  snprintf(cfg.inputs[1].sdt_text, sizeof cfg.inputs[1].sdt_text, "B");
  cfg.inputs[2].uri = "http://127.0.0.1:1/c";
  cfg.inputs[2].sid = 30;
  snprintf(cfg.inputs[2].sdt_text, sizeof cfg.inputs[2].sdt_text, "C");
  cfg.error_retry_s = 1;

  is = inputset_new(&cfg, noop_meta_cb, NULL, NULL);
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
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nbody-bytes";
  char uri[64];
  config_t cfg;
  inputset_t *is;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  memset(&cfg, 0, sizeof cfg);
  cfg.n_inputs = 1;
  cfg.inputs[0].uri = uri;
  cfg.inputs[0].sid = 1;
  snprintf(cfg.inputs[0].sdt_text, sizeof cfg.inputs[0].sdt_text, "Test");

  is = inputset_new(&cfg, noop_meta_cb, NULL, NULL);
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
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nbody-bytes";
  char uri_ok[64];
  config_t cfg;
  inputset_t *is;
  time_t now;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri_ok, sizeof uri_ok, "http://127.0.0.1:%u/stream", port);
  memset(&cfg, 0, sizeof cfg);
  cfg.n_inputs = 2;
  cfg.inputs[0].uri = uri_ok;
  cfg.inputs[0].sid = 1;
  snprintf(cfg.inputs[0].sdt_text, sizeof cfg.inputs[0].sdt_text, "OK");
  cfg.inputs[1].uri = "http://127.0.0.1:1/dead"; /* nothing listens here */
  cfg.inputs[1].sid = 2;
  snprintf(cfg.inputs[1].sdt_text, sizeof cfg.inputs[1].sdt_text, "Dead");
  /* error_retry_s left 0: n_inputs > 1 must still auto-default to a retry, never give up */

  is = inputset_new(&cfg, noop_meta_cb, NULL, NULL);
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
  snprintf(cfg.inputs[0].sdt_text, sizeof cfg.inputs[0].sdt_text, "Dead");
  cfg.error_retry_s = 0;

  is = inputset_new(&cfg, noop_meta_cb, NULL, NULL);
  ck_assert_ptr_nonnull(is);

  drive_all(is, 100);
  ck_assert_ptr_null(inputset_source(is, 0));
  ck_assert_int_eq(inputset_next_deadline(is), INPUTSET_NEVER);

  inputset_free(is);
}
END_TEST

static Suite *inputset_suite(void) {
  Suite *s = suite_create("inputset");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, inputset_pid_allocation_is_index_based);
  tcase_add_test(tc, inputset_connects_and_reports_source);
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
