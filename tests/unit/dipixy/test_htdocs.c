/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/core/htdocs.h"
#include "dipixy/reactor/qsbr.h"

#include "lib/sys/signal.h"

#define BUILTIN_PAGE "<html>builtin</html>\n"
#define BIG_PAGE_BYTES 200000
#define RELOADS 6

const char g_htdocs_index_html[] = BUILTIN_PAGE;
const size_t g_htdocs_index_html_len = sizeof BUILTIN_PAGE - 1;

static qsbr_domain_t *g_domain;

qsbr_domain_t *reactor_qsbr(void) { return g_domain; }

static char g_dir[64];
static char g_file[128];
static config_t g_cfg;

static void write_page(const char *content, size_t len) {
  FILE *f = fopen(g_file, "w");

  ck_assert_ptr_nonnull(f);
  ck_assert_uint_eq(fwrite(content, 1, len, f), len);
  fclose(f);
}

static void expect_page(const char *content, size_t len) {
  const char *buf;
  size_t got;

  htdocs_get(&buf, &got);
  ck_assert_uint_eq(got, len);
  ck_assert_mem_eq(buf, content, len);
}

static void hup(void) {
  ck_assert_int_eq(raise(SIGHUP), 0);
  htdocs_template_reload_check();
}

static void setup(void) {
  snprintf(g_dir, sizeof g_dir, "/tmp/dipixy-htdocs-XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(g_dir));
  snprintf(g_file, sizeof g_file, "%s/page.html", g_dir);
  memset(&g_cfg, 0, sizeof g_cfg);
  g_domain = qsbr_domain_create(1);
  signals_install();
}

static void teardown(void) {
  unlink(g_file);
  rmdir(g_dir);
  qsbr_domain_destroy(g_domain);
  g_domain = NULL;
}

START_TEST(builtin_page_is_served_without_a_template) {
  htdocs_template_init(&g_cfg);
  expect_page(BUILTIN_PAGE, sizeof BUILTIN_PAGE - 1);
}
END_TEST

START_TEST(template_file_replaces_the_builtin_page) {
  static const char page[] = "<p>custom</p>\n";

  write_page(page, sizeof page - 1);
  g_cfg.status_template = g_file;
  htdocs_template_init(&g_cfg);
  expect_page(page, sizeof page - 1);
}
END_TEST

START_TEST(unreadable_template_keeps_the_builtin_page) {
  g_cfg.status_template = g_file;
  htdocs_template_init(&g_cfg);
  expect_page(BUILTIN_PAGE, sizeof BUILTIN_PAGE - 1);
}
END_TEST

START_TEST(template_appearing_later_is_picked_up_by_reload) {
  static const char page[] = "late";

  g_cfg.status_template = g_file;
  htdocs_template_init(&g_cfg);
  write_page(page, sizeof page - 1);
  expect_page(BUILTIN_PAGE, sizeof BUILTIN_PAGE - 1);
  hup();
  expect_page(page, sizeof page - 1);
}
END_TEST

START_TEST(reload_swaps_content_only_after_sighup) {
  static const char first[] = "first";
  static const char second[] = "second page";

  write_page(first, sizeof first - 1);
  g_cfg.status_template = g_file;
  htdocs_template_init(&g_cfg);
  write_page(second, sizeof second - 1);
  htdocs_template_reload_check();
  expect_page(first, sizeof first - 1);
  hup();
  expect_page(second, sizeof second - 1);
  htdocs_template_reload_check();
  expect_page(second, sizeof second - 1);
}
END_TEST

START_TEST(failed_reload_keeps_the_previous_page) {
  static const char page[] = "kept";

  write_page(page, sizeof page - 1);
  g_cfg.status_template = g_file;
  htdocs_template_init(&g_cfg);
  unlink(g_file);
  hup();
  expect_page(page, sizeof page - 1);
  write_page("new", 3);
  hup();
  expect_page("new", 3);
}
END_TEST

START_TEST(reload_without_a_template_path_ignores_sighup) {
  htdocs_template_init(&g_cfg);
  hup();
  expect_page(BUILTIN_PAGE, sizeof BUILTIN_PAGE - 1);
}
END_TEST

START_TEST(empty_template_is_served_as_empty) {
  write_page("", 0);
  g_cfg.status_template = g_file;
  htdocs_template_init(&g_cfg);
  expect_page("", 0);
}
END_TEST

START_TEST(template_larger_than_the_read_chunks_round_trips) {
  char *page = malloc(BIG_PAGE_BYTES);

  ck_assert_ptr_nonnull(page);
  for (size_t i = 0; i < BIG_PAGE_BYTES; i++) page[i] = (char)('a' + i % 23);
  write_page(page, BIG_PAGE_BYTES);
  g_cfg.status_template = g_file;
  htdocs_template_init(&g_cfg);
  expect_page(page, BIG_PAGE_BYTES);
  page[0] = 'Z';
  write_page(page, BIG_PAGE_BYTES);
  hup();
  expect_page(page, BIG_PAGE_BYTES);
  free(page);
}
END_TEST

START_TEST(retired_page_stays_readable_until_workers_are_quiescent) {
  static const char first[] = "first page";
  const char *held;
  size_t held_len;

  write_page(first, sizeof first - 1);
  g_cfg.status_template = g_file;
  htdocs_template_init(&g_cfg);
  htdocs_get(&held, &held_len);
  for (int i = 0; i < RELOADS; i++) {
    char next[16];
    int n = snprintf(next, sizeof next, "page %d", i);

    write_page(next, (size_t)n);
    hup();
    ck_assert_mem_eq(held, first, sizeof first - 1);
  }
  qsbr_worker_quiescent(g_domain, 0);
  write_page("last", 4);
  hup();
  expect_page("last", 4);
}
END_TEST

static _Atomic int g_reader_stop;
static _Atomic int g_reader_bad;

static void *reader_loop(void *arg) {
  (void)arg;
  while (!atomic_load_explicit(&g_reader_stop, memory_order_acquire)) {
    const char *buf;
    size_t len;

    htdocs_get(&buf, &len);
    if (len < 4 || buf[0] != 'p') atomic_store_explicit(&g_reader_bad, 1, memory_order_relaxed);
    for (size_t i = 2; i < len; i++) if (buf[i] != buf[1]) atomic_store_explicit(&g_reader_bad, 1, memory_order_relaxed);
    qsbr_worker_quiescent(g_domain, 0);
  }
  return NULL;
}

START_TEST(readers_never_see_a_torn_or_freed_page) {
  pthread_t t;
  char page[64];

  memset(page, 'a', sizeof page);
  page[0] = 'p';
  write_page(page, sizeof page);
  g_cfg.status_template = g_file;
  htdocs_template_init(&g_cfg);
  atomic_store(&g_reader_stop, 0);
  atomic_store(&g_reader_bad, 0);
  pthread_create(&t, NULL, reader_loop, NULL);
  for (int i = 0; i < RELOADS; i++) {
    memset(page + 1, 'b' + i, sizeof page - 1);
    write_page(page, sizeof page);
    hup();
    usleep(2000);
  }
  atomic_store_explicit(&g_reader_stop, 1, memory_order_release);
  pthread_join(t, NULL);
  ck_assert_int_eq(atomic_load(&g_reader_bad), 0);
}
END_TEST

static Suite *htdocs_suite(void) {
  Suite *s = suite_create("dipixy_htdocs");
  TCase *tc = tcase_create("core");

  tcase_set_timeout(tc, 20);
  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_test(tc, builtin_page_is_served_without_a_template);
  tcase_add_test(tc, template_file_replaces_the_builtin_page);
  tcase_add_test(tc, unreadable_template_keeps_the_builtin_page);
  tcase_add_test(tc, template_appearing_later_is_picked_up_by_reload);
  tcase_add_test(tc, reload_swaps_content_only_after_sighup);
  tcase_add_test(tc, failed_reload_keeps_the_previous_page);
  tcase_add_test(tc, reload_without_a_template_path_ignores_sighup);
  tcase_add_test(tc, empty_template_is_served_as_empty);
  tcase_add_test(tc, template_larger_than_the_read_chunks_round_trips);
  tcase_add_test(tc, retired_page_stays_readable_until_workers_are_quiescent);
  tcase_add_test(tc, readers_never_see_a_torn_or_freed_page);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(htdocs_suite());
  int failed;

  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
