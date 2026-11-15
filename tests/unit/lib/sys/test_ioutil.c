/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../alloc_fail.h"
#include "lib/sys/ioutil.h"

ssize_t __real_getrandom(void *buf, size_t len, unsigned flags);
static int g_getrandom_fail;

ssize_t __wrap_getrandom(void *buf, size_t len, unsigned flags) {
  if (g_getrandom_fail) {
    errno = ENOSYS;
    return -1;
  }
  return __real_getrandom(buf, len, flags);
}

#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
const char *__asan_default_options(void) { return "allocator_may_return_null=1"; }
#endif
#if defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer)
const char *__tsan_default_options(void) { return "allocator_may_return_null=1"; }
#endif

START_TEST(rand_seed_varies) {
  uint32_t a = rand_seed32();
  uint32_t b = rand_seed32();
  uint32_t c = rand_seed32();
  ck_assert(a != b || b != c);
}
END_TEST

static FILE *tmpfile_with(const void *data, size_t n) {
  FILE *f = tmpfile();
  ck_assert_ptr_nonnull(f);
  if (n) ck_assert_uint_eq(fwrite(data, 1, n, f), n);
  rewind(f);
  return f;
}

START_TEST(read_all_sizes) {
  static const size_t sizes[] = {0, 1, 100, 65534, 65535, 65536, 70000, 200000};
  size_t n = sizes[_i];
  unsigned char *src = malloc(n ? n : 1);
  char *out = NULL;
  size_t len = 0;
  FILE *f;
  ck_assert_ptr_nonnull(src);
  for (size_t i = 0; i < n; i++) src[i] = (unsigned char)('a' + i % 26);
  f = tmpfile_with(src, n);
  ck_assert_int_eq(read_all(f, &out, &len), 0);
  ck_assert_uint_eq(len, n);
  ck_assert_int_eq(memcmp(out, src, n), 0);
  ck_assert_int_eq(out[n], 0);
  free(out);
  fclose(f);
  free(src);
}
END_TEST

START_TEST(read_all_from_pipe_grows) {
  int fds[2];
  FILE *f;
  char *out = NULL;
  size_t len = 0;
  unsigned char *src = malloc(300000);
  ck_assert_ptr_nonnull(src);
  memset(src, 'p', 300000);
  ck_assert_int_eq(pipe(fds), 0);
  ck_assert_int_eq(fcntl(fds[1], F_SETPIPE_SZ, 1 << 20) > 0, 1);
  ck_assert_int_eq(write(fds[1], src, 300000), 300000);
  close(fds[1]);
  f = fdopen(fds[0], "r");
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(read_all(f, &out, &len), 0);
  ck_assert_uint_eq(len, 300000u);
  free(out);
  fclose(f);
  free(src);
}
END_TEST

START_TEST(rand_seed_falls_back_without_getrandom) {
  uint32_t a;
  uint32_t b;
  g_getrandom_fail = 1;
  a = rand_seed32();
  b = rand_seed32();
  g_getrandom_fail = 0;
  ck_assert_uint_eq(a, b);
}
END_TEST

START_TEST(read_all_survives_allocation_failures) {
  size_t size = 300000;
  char *src = malloc(size);
  ck_assert_ptr_nonnull(src);
  memset(src, 'x', size);
  for (long n = 0;; n++) {
    FILE *f = fmemopen(src, size, "r");
    char *out = NULL;
    size_t len = 0;
    int hit;
    int rc;
    ck_assert_ptr_nonnull(f);
    alloc_fail_at(n);
    rc = read_all(f, &out, &len);
    hit = alloc_fail_end();
    if (!rc) free(out);
    fclose(f);
    if (!hit) break;
  }
  free(src);
}
END_TEST

START_TEST(read_all_reports_stream_errors) {
  FILE *f = fopen("/dev/null", "w");
  char *out = NULL;
  size_t len = 0;
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(read_all(f, &out, &len), -1);
  fclose(f);
}
END_TEST

START_TEST(read_line_forms) {
  static const char data[] = "short\nexactly7\nthis line is much too long\nlast";
  FILE *f = tmpfile_with(data, sizeof data - 1);
  char buf[9];
  ck_assert_int_eq(read_line(f, buf, sizeof buf), 1);
  ck_assert_str_eq(buf, "short\n");
  ck_assert_int_eq(read_line(f, buf, sizeof buf), 1);
  ck_assert_str_eq(buf, "exactly7");
  ck_assert_int_eq(read_line(f, buf, sizeof buf), -1);
  ck_assert_int_eq(read_line(f, buf, sizeof buf), 1);
  ck_assert_str_eq(buf, "last");
  ck_assert_int_eq(read_line(f, buf, sizeof buf), 0);
  fclose(f);
}
END_TEST

START_TEST(read_line_exact_fit_at_eof_and_newline) {
  static const char a[] = "12345678";
  static const char b[] = "12345678\nnext\n";
  FILE *f = tmpfile_with(a, sizeof a - 1);
  char buf[9];
  ck_assert_int_eq(read_line(f, buf, sizeof buf), 1);
  fclose(f);
  f = tmpfile_with(b, sizeof b - 1);
  ck_assert_int_eq(read_line(f, buf, sizeof buf), 1);
  ck_assert_int_eq(read_line(f, buf, sizeof buf), 1);
  ck_assert_str_eq(buf, "next\n");
  fclose(f);
}
END_TEST

static void fill_pipe(int fd) {
  unsigned char junk[4096];
  memset(junk, 'j', sizeof junk);
  for (;;) {
    ssize_t w = write(fd, junk, sizeof junk);
    if (w < 0) {
      ck_assert_int_eq(errno, EAGAIN);
      break;
    }
  }
}


START_TEST(pipe_write_all_plain) {
  int fds[2];
  unsigned char data[1000];
  unsigned char back[1000];
  atomic_int stop = 0;
  memset(data, 'd', sizeof data);
  ck_assert_int_eq(pipe(fds), 0);
  ck_assert_int_eq(pipe_write_all(fds[1], data, sizeof data, &stop), 0);
  ck_assert_int_eq(pipe_write_all(fds[1], data, 0, &stop), 0);
  ck_assert_int_eq(read(fds[0], back, sizeof back), (ssize_t)sizeof back);
  ck_assert_int_eq(memcmp(data, back, sizeof data), 0);
  close(fds[0]);
  close(fds[1]);
}
END_TEST

START_TEST(pipe_write_all_stops_on_flag_when_full) {
  int fds[2];
  unsigned char data[100000];
  atomic_int stop = 1;
  memset(data, 'd', sizeof data);
  ck_assert_int_eq(pipe2(fds, O_NONBLOCK), 0);
  fill_pipe(fds[1]);
  ck_assert_int_eq(pipe_write_all(fds[1], data, sizeof data, &stop), -1);
  close(fds[0]);
  close(fds[1]);
}
END_TEST

typedef struct {
  int fd;
  atomic_int *stop;
  const unsigned char *data;
  size_t n;
  atomic_int rc;
} writer_t;

static void *writer_main(void *arg) {
  writer_t *w = arg;
  atomic_store(&w->rc, pipe_write_all(w->fd, w->data, w->n, w->stop));
  return NULL;
}

START_TEST(pipe_write_all_waits_for_reader_after_eagain) {
  int fds[2];
  unsigned char *data = malloc(300000);
  atomic_int stop = 0;
  pthread_t th;
  writer_t w;
  ck_assert_ptr_nonnull(data);
  memset(data, 'd', 300000);
  ck_assert_int_eq(pipe2(fds, O_NONBLOCK), 0);
  fill_pipe(fds[1]);
  w.fd = fds[1];
  w.stop = &stop;
  w.data = data;
  w.n = 300000;
  atomic_store(&w.rc, 99);
  ck_assert_int_eq(pthread_create(&th, NULL, writer_main, &w), 0);
  usleep(250000);
  {
    size_t total = 0;
    unsigned char buf[65536];
    ssize_t r;
    while (total < 300000) {
      r = read(fds[0], buf, sizeof buf);
      if (r > 0) total += (size_t)r;
      else usleep(1000);
      if (total > 400000) break;
    }
  }
  pthread_join(th, NULL);
  ck_assert_int_eq(atomic_load(&w.rc), 0);
  close(fds[0]);
  close(fds[1]);
  free(data);
}
END_TEST

START_TEST(pipe_write_all_reports_broken_pipe) {
  int fds[2];
  unsigned char data[100];
  atomic_int stop = 0;
  memset(data, 'd', sizeof data);
  signal(SIGPIPE, SIG_IGN);
  ck_assert_int_eq(pipe(fds), 0);
  close(fds[0]);
  ck_assert_int_eq(pipe_write_all(fds[1], data, sizeof data, &stop), -1);
  close(fds[1]);
}
END_TEST

static int w_done(const writer_t *w) {
  return atomic_load(&w->rc) != 99;
}

static void noop_handler(int sig) {
  (void)sig;
}

START_TEST(pipe_write_all_retries_after_eintr) {
  int fds[2];
  unsigned char *data = malloc(200000);
  atomic_int stop = 0;
  pthread_t th;
  writer_t w;
  struct sigaction sa;
  ck_assert_ptr_nonnull(data);
  memset(data, 'd', 200000);
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = noop_handler;
  sigaction(SIGUSR1, &sa, NULL);
  ck_assert_int_eq(pipe2(fds, O_NONBLOCK), 0);
  fill_pipe(fds[1]);
  ck_assert_int_eq(fcntl(fds[1], F_SETFL, 0), 0);
  w.fd = fds[1];
  w.stop = &stop;
  w.data = data;
  w.n = 200000;
  atomic_store(&w.rc, 99);
  ck_assert_int_eq(pthread_create(&th, NULL, writer_main, &w), 0);
  usleep(200000);
  pthread_kill(th, SIGUSR1);
  usleep(100000);
  {
    size_t total = 0;
    unsigned char buf[65536];
    ssize_t r;
    ck_assert_int_eq(fcntl(fds[0], F_SETFL, O_NONBLOCK), 0);
    while (!w_done(&w) || total < 200000) {
      r = read(fds[0], buf, sizeof buf);
      if (r > 0) total += (size_t)r;
      else usleep(1000);
      if (total > 400000) break;
    }
  }
  pthread_join(th, NULL);
  ck_assert_int_eq(atomic_load(&w.rc), 0);
  close(fds[0]);
  close(fds[1]);
  free(data);
}
END_TEST

static Suite *ioutil_suite(void) {
  Suite *s = suite_create("ioutil");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 60);
  tcase_add_test(tc, rand_seed_varies);
  tcase_add_loop_test(tc, read_all_sizes, 0, 8);
  tcase_add_test(tc, read_all_from_pipe_grows);
  tcase_add_test(tc, read_all_reports_stream_errors);
  tcase_add_test(tc, read_line_forms);
  tcase_add_test(tc, read_line_exact_fit_at_eof_and_newline);
  tcase_add_test(tc, pipe_write_all_plain);
  tcase_add_test(tc, pipe_write_all_stops_on_flag_when_full);
  tcase_add_test(tc, pipe_write_all_waits_for_reader_after_eagain);
  tcase_add_test(tc, pipe_write_all_reports_broken_pipe);
  tcase_add_test(tc, pipe_write_all_retries_after_eintr);
  tcase_add_test(tc, rand_seed_falls_back_without_getrandom);
  tcase_add_test(tc, read_all_survives_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ioutil_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
