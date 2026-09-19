/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/config/yamlcfg.h"

#define TOOL "testtool"
#define NKEYS(k) (sizeof(k) / sizeof((k)[0]))

typedef struct {
  const char *str;
  int flag;
  unsigned num;
  unsigned port;
  const char *list[8];
  unsigned nlist;
  int begins;
  int ends;
  int item_fail;
} tcfg_t;

static int ap_str(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((tcfg_t *)c)->str, v, e, n);
}

static int ap_bool(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((tcfg_t *)c)->flag, v, e, n);
}

static int ap_num(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((tcfg_t *)c)->num, v, 1, 100, e, n);
}

static int ap_port(void *c, const char *v, char *e, size_t n) {
  tcfg_t *t = c;
  return yamlcfg_set_uint(&t->port, v, 1, 65535, e, n);
}

static int ap_list(void *c, const char *v, char *e, size_t n) {
  tcfg_t *t = c;
  if (t->nlist >= sizeof t->list / sizeof t->list[0]) {
    snprintf(e, n, "too many");
    return -1;
  }
  return yamlcfg_set_str(&t->list[t->nlist++], v, e, n);
}

static int item_cb(void *c, const char *list, int begin, char *e, size_t n) {
  tcfg_t *t = c;
  (void)list;
  if (begin) t->begins++;
  else t->ends++;
  if (t->item_fail) {
    snprintf(e, n, "item rejected");
    return -1;
  }
  return 0;
}

static const yamlcfg_key_t KEYS[] = {
  {"name", ap_str, 0, 0},
  {"on", ap_bool, 0, 0},
  {"count", ap_num, 0, 0},
  {"net.port", ap_port, 0, 0},
  {"file", ap_str, 1, 0},
  {"list", ap_list, 0, 1},
  {"items", ap_list, 0, YAMLCFG_LIST_KEYED},
  {"items.opt", ap_num, 0, 0},
};

static void write_cfg(char *path, const char *text) {
  int fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq((int)write(fd, text, strlen(text)), (int)strlen(text));
  close(fd);
}

static int load_text(yamlcfg_t *y, tcfg_t *t, int mode, const char *text, yamlcfg_item_fn item) {
  char path[] = "/tmp/yamlcfg_test_XXXXXX";
  int rc;
  memset(t, 0, sizeof *t);
  write_cfg(path, text);
  rc = yamlcfg_load_items(y, TOOL, mode, path, NULL, KEYS, NKEYS(KEYS), t, item);
  unlink(path);
  return rc;
}

START_TEST(parse_bool_accepts_all_spellings) {
  static const char *const yes[] = {"on", "yes", "true", "1", "ON", "Yes", "TRUE"};
  static const char *const no[] = {"off", "no", "false", "0", "OFF", "No", "False"};
  int v;
  for (size_t i = 0; i < sizeof yes / sizeof yes[0]; i++) {
    v = -1;
    ck_assert_int_eq(yamlcfg_parse_bool(yes[i], &v), 0);
    ck_assert_int_eq(v, 1);
  }
  for (size_t i = 0; i < sizeof no / sizeof no[0]; i++) {
    v = -1;
    ck_assert_int_eq(yamlcfg_parse_bool(no[i], &v), 0);
    ck_assert_int_eq(v, 0);
  }
}
END_TEST

START_TEST(parse_bool_rejects_garbage_and_keeps_output) {
  int v = 7;
  ck_assert_int_ne(yamlcfg_parse_bool("maybe", &v), 0);
  ck_assert_int_ne(yamlcfg_parse_bool("", &v), 0);
  ck_assert_int_ne(yamlcfg_parse_bool("2", &v), 0);
  ck_assert_int_eq(v, 7);
}
END_TEST

START_TEST(set_bool_reports_invalid_value) {
  int v = 0;
  char err[64] = "";
  ck_assert_int_eq(yamlcfg_set_bool(&v, "yes", err, sizeof err), 0);
  ck_assert_int_eq(v, 1);
  ck_assert_int_ne(yamlcfg_set_bool(&v, "nope", err, sizeof err), 0);
  ck_assert_ptr_nonnull(strstr(err, "nope"));
}
END_TEST

START_TEST(set_str_copies_value) {
  const char *s = NULL;
  char buf[] = "hello";
  char err[64] = "";
  ck_assert_int_eq(yamlcfg_set_str(&s, buf, err, sizeof err), 0);
  buf[0] = 'X';
  ck_assert_str_eq(s, "hello");
}
END_TEST

START_TEST(set_str_rejects_empty) {
  const char *s = NULL;
  char err[64] = "";
  ck_assert_int_ne(yamlcfg_set_str(&s, "", err, sizeof err), 0);
  ck_assert_ptr_null(s);
  ck_assert_str_eq(err, "empty value");
}
END_TEST

START_TEST(set_uint_enforces_inclusive_range) {
  unsigned v = 0;
  char err[64] = "";
  ck_assert_int_eq(yamlcfg_set_uint(&v, "5", 5, 10, err, sizeof err), 0);
  ck_assert_uint_eq(v, 5u);
  ck_assert_int_eq(yamlcfg_set_uint(&v, "10", 5, 10, err, sizeof err), 0);
  ck_assert_uint_eq(v, 10u);
  ck_assert_int_ne(yamlcfg_set_uint(&v, "4", 5, 10, err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_uint(&v, "11", 5, 10, err, sizeof err), 0);
  ck_assert_uint_eq(v, 10u);
  ck_assert_ptr_nonnull(strstr(err, "5..10"));
}
END_TEST

START_TEST(set_uint_rejects_non_numeric) {
  unsigned v = 3;
  char err[64] = "";
  ck_assert_int_ne(yamlcfg_set_uint(&v, "abc", 0, 10, err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_uint(&v, "5x", 0, 10, err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_uint(&v, "-1", 0, 10, err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_uint(&v, "", 0, 10, err, sizeof err), 0);
  ck_assert_uint_eq(v, 3u);
}
END_TEST

START_TEST(set_double_enforces_bounds) {
  double v = 0;
  char err[64] = "";
  ck_assert_int_eq(yamlcfg_set_double(&v, "1.5", 0, 2, 0, err, sizeof err), 0);
  ck_assert_double_eq(v, 1.5);
  ck_assert_int_eq(yamlcfg_set_double(&v, "0", 0, 2, 0, err, sizeof err), 0);
  ck_assert_int_eq(yamlcfg_set_double(&v, "2", 0, 2, 0, err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_double(&v, "2.001", 0, 2, 0, err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_double(&v, "-0.1", 0, 2, 0, err, sizeof err), 0);
}
END_TEST

START_TEST(set_double_exclusive_minimum) {
  double v = 9;
  char err[64] = "";
  ck_assert_int_ne(yamlcfg_set_double(&v, "0", 0, 2, 1, err, sizeof err), 0);
  ck_assert_double_eq(v, 9);
  ck_assert_ptr_nonnull(strstr(err, ">"));
  ck_assert_int_eq(yamlcfg_set_double(&v, "0.001", 0, 2, 1, err, sizeof err), 0);
}
END_TEST

START_TEST(set_double_rejects_malformed_and_non_finite) {
  double v = 9;
  char err[64] = "";
  ck_assert_int_ne(yamlcfg_set_double(&v, "", 0, 2, 0, err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_double(&v, "1.5x", 0, 2, 0, err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_double(&v, "abc", 0, 2, 0, err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_double(&v, "nan", -1e9, 1e9, 0, err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_double(&v, "inf", -1e9, 1e9, 0, err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_double(&v, "1e999", -1e9, 1e9, 0, err, sizeof err), 0);
  ck_assert_double_eq(v, 9);
}
END_TEST

START_TEST(set_lang_requires_three_letters) {
  char lang[4] = "";
  char err[64] = "";
  ck_assert_int_eq(yamlcfg_set_lang(lang, "eng", err, sizeof err), 0);
  ck_assert_str_eq(lang, "eng");
  ck_assert_int_ne(yamlcfg_set_lang(lang, "en", err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_lang(lang, "engl", err, sizeof err), 0);
  ck_assert_int_ne(yamlcfg_set_lang(lang, "", err, sizeof err), 0);
  ck_assert_str_eq(lang, "eng");
}
END_TEST

START_TEST(set_addrport_parses_ipv4) {
  int fam = 0;
  char addr[64] = "";
  unsigned port = 0;
  char err[64] = "";
  ck_assert_int_eq(yamlcfg_set_addrport(&fam, addr, sizeof addr, &port, "239.1.1.1:5000", err, sizeof err), 0);
  ck_assert_int_eq(fam, AF_INET);
  ck_assert_str_eq(addr, "239.1.1.1");
  ck_assert_uint_eq(port, 5000u);
}
END_TEST

START_TEST(set_addrport_parses_ipv6) {
  int fam = 0;
  char addr[64] = "";
  unsigned port = 0;
  char err[64] = "";
  ck_assert_int_eq(yamlcfg_set_addrport(&fam, addr, sizeof addr, &port, "[ff02::1]:6000", err, sizeof err), 0);
  ck_assert_int_eq(fam, AF_INET6);
  ck_assert_str_eq(addr, "ff02::1");
  ck_assert_uint_eq(port, 6000u);
}
END_TEST

START_TEST(set_addrport_accepts_null_family) {
  char addr[64] = "";
  unsigned port = 0;
  char err[64] = "";
  ck_assert_int_eq(yamlcfg_set_addrport(NULL, addr, sizeof addr, &port, "10.0.0.1:80", err, sizeof err), 0);
  ck_assert_str_eq(addr, "10.0.0.1");
  ck_assert_uint_eq(port, 80u);
}
END_TEST

START_TEST(set_addrport_rejects_bad_input) {
  static const char *const bad[] = {"", "239.1.1.1", "239.1.1.1:0", "239.1.1.1:70000", "239.1.1:5000", "host:80", ":80", "[::1]", "[::1:80", "[::1]80"};
  int fam = 0;
  char addr[64] = "";
  unsigned port = 0;
  char err[64];
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    err[0] = 0;
    ck_assert_msg(yamlcfg_set_addrport(&fam, addr, sizeof addr, &port, bad[i], err, sizeof err) != 0, "accepted '%s'", bad[i]);
    ck_assert_ptr_nonnull(strstr(err, "addr:port"));
  }
}
END_TEST

START_TEST(set_addrport_rejects_small_addr_buffer) {
  int fam = 0;
  char addr[8] = "";
  unsigned port = 0;
  char err[64] = "";
  ck_assert_int_ne(yamlcfg_set_addrport(&fam, addr, sizeof addr, &port, "239.255.255.255:1234", err, sizeof err), 0);
}
END_TEST

START_TEST(set_color_maps_modes) {
  int v = -1;
  int seen[3];
  char err[64] = "";
  ck_assert_int_eq(yamlcfg_set_color(&v, "auto", err, sizeof err), 0);
  seen[0] = v;
  ck_assert_int_eq(yamlcfg_set_color(&v, "always", err, sizeof err), 0);
  seen[1] = v;
  ck_assert_int_eq(yamlcfg_set_color(&v, "never", err, sizeof err), 0);
  seen[2] = v;
  ck_assert_int_ne(seen[0], seen[1]);
  ck_assert_int_ne(seen[1], seen[2]);
  ck_assert_int_ne(seen[0], seen[2]);
}
END_TEST

START_TEST(set_color_rejects_unknown) {
  int v = 42;
  char err[64] = "";
  ck_assert_int_ne(yamlcfg_set_color(&v, "sometimes", err, sizeof err), 0);
  ck_assert_int_eq(v, 42);
  ck_assert_ptr_nonnull(strstr(err, "auto|always|never"));
}
END_TEST

START_TEST(load_without_any_path_is_absent) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(yamlcfg_load(&y, TOOL, 0, NULL, NULL, KEYS, NKEYS(KEYS), &t), YAMLCFG_ABSENT);
}
END_TEST

START_TEST(load_missing_default_is_absent) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(yamlcfg_load(&y, TOOL, 0, NULL, "/nonexistent/dir/cfg.yaml", KEYS, NKEYS(KEYS), &t), YAMLCFG_ABSENT);
}
END_TEST

START_TEST(load_missing_default_is_error_in_check_mode) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(yamlcfg_load(&y, TOOL, YAMLCFG_CHECK, NULL, "/nonexistent/dir/cfg.yaml", KEYS, NKEYS(KEYS), &t), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_missing_explicit_path_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(yamlcfg_load(&y, TOOL, 0, "/nonexistent/dir/cfg.yaml", NULL, KEYS, NKEYS(KEYS), &t), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_directory_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(yamlcfg_load(&y, TOOL, 0, "/tmp", NULL, KEYS, NKEYS(KEYS), &t), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_explicit_path_wins_over_default) {
  yamlcfg_t y;
  tcfg_t t;
  char path[] = "/tmp/yamlcfg_test_XXXXXX";
  memset(&t, 0, sizeof t);
  write_cfg(path, "count: 7\n");
  ck_assert_int_eq(yamlcfg_load(&y, TOOL, 0, path, "/nonexistent/dir/cfg.yaml", KEYS, NKEYS(KEYS), &t), YAMLCFG_LOADED);
  unlink(path);
  ck_assert_uint_eq(t.num, 7u);
  ck_assert_str_eq(y.path, path);
  ck_assert_str_eq(y.tool, TOOL);
}
END_TEST

START_TEST(load_applies_scalars) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "name: hello\non: yes\ncount: 42\n", NULL), YAMLCFG_LOADED);
  ck_assert_str_eq(t.str, "hello");
  ck_assert_int_eq(t.flag, 1);
  ck_assert_uint_eq(t.num, 42u);
  ck_assert_uint_eq(y.warnings, 0u);
}
END_TEST

START_TEST(load_accepts_quoted_and_empty_document) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "name: \"a b: c\"\n", NULL), YAMLCFG_LOADED);
  ck_assert_str_eq(t.str, "a b: c");
  ck_assert_int_eq(load_text(&y, &t, 0, "", NULL), YAMLCFG_LOADED);
  ck_assert_int_eq(load_text(&y, &t, 0, "# only a comment\n", NULL), YAMLCFG_LOADED);
}
END_TEST

START_TEST(load_resolves_nested_key_paths) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "net:\n  port: 8080\nname: after\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(t.port, 8080u);
  ck_assert_str_eq(t.str, "after");
}
END_TEST

START_TEST(load_skips_null_values) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "name:\ncount: ~\non: null\n", NULL), YAMLCFG_LOADED);
  ck_assert_ptr_null(t.str);
  ck_assert_uint_eq(t.num, 0u);
  ck_assert_int_eq(t.flag, 0);
  ck_assert_uint_eq(y.warnings, 0u);
}
END_TEST

START_TEST(load_quoted_null_is_a_value) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "name: \"null\"\n", NULL), YAMLCFG_LOADED);
  ck_assert_str_eq(t.str, "null");
}
END_TEST

START_TEST(load_unknown_key_warns_by_default) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "bogus: 1\nname: ok\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(y.warnings, 1u);
  ck_assert_str_eq(t.str, "ok");
}
END_TEST

START_TEST(load_unknown_key_fails_in_strict_mode) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, YAMLCFG_STRICT, "bogus: 1\n", NULL), YAMLCFG_ERROR);
  ck_assert_uint_eq(y.warnings, 1u);
}
END_TEST

START_TEST(load_unknown_key_in_check_mode_is_loaded) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, YAMLCFG_CHECK | YAMLCFG_STRICT, "bogus: 1\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(y.warnings, 1u);
  ck_assert_int_ne(yamlcfg_report(&y), 0);
}
END_TEST

START_TEST(load_invalid_value_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "count: 999\n", NULL), YAMLCFG_ERROR);
  ck_assert_int_eq(load_text(&y, &t, 0, "on: maybe\n", NULL), YAMLCFG_ERROR);
  ck_assert_int_eq(load_text(&y, &t, 0, "name: \"\"\n", NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_invalid_value_is_warning_in_check_mode) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, YAMLCFG_CHECK, "count: 999\non: maybe\nname: ok\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(y.warnings, 2u);
  ck_assert_str_eq(t.str, "ok");
}
END_TEST

START_TEST(load_duplicate_key_warns_and_last_wins) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "count: 1\ncount: 2\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(t.num, 2u);
  ck_assert_uint_eq(y.warnings, 1u);
}
END_TEST

START_TEST(load_strict_duplicate_key_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, YAMLCFG_STRICT, "count: 1\ncount: 2\n", NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_collects_list_values) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "list:\n  - a\n  - b\n  - c\nname: x\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(t.nlist, 3u);
  ck_assert_str_eq(t.list[0], "a");
  ck_assert_str_eq(t.list[1], "b");
  ck_assert_str_eq(t.list[2], "c");
  ck_assert_str_eq(t.str, "x");
  ck_assert_uint_eq(y.warnings, 0u);
}
END_TEST

START_TEST(load_accepts_flow_list_and_scalar_for_list_key) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "list: [a, b]\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(t.nlist, 2u);
  ck_assert_int_eq(load_text(&y, &t, 0, "list: solo\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(t.nlist, 1u);
  ck_assert_str_eq(t.list[0], "solo");
}
END_TEST

START_TEST(load_skips_null_list_items) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "list:\n  - a\n  -\n  - b\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(t.nlist, 2u);
}
END_TEST

START_TEST(load_list_for_scalar_key_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "name:\n  - a\n", NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_list_for_scalar_key_is_warning_in_check_mode) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, YAMLCFG_CHECK, "name:\n  - a\ncount: 3\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_ge(y.warnings, 1u);
  ck_assert_uint_eq(t.num, 3u);
}
END_TEST

START_TEST(load_nested_lists_are_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "list:\n  - [a, b]\n", NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_mapping_in_plain_list_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "list:\n  - k: v\n", NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_keyed_items_call_back_in_pairs) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "items:\n  - first:\n      opt: 5\n  - second\n  - third:\n      opt: 9\n", item_cb), YAMLCFG_LOADED);
  ck_assert_int_eq(t.begins, t.ends);
  ck_assert_int_ge(t.begins, 2);
  ck_assert_uint_ge(t.nlist, 3u);
  ck_assert_str_eq(t.list[0], "first");
  ck_assert_uint_eq(t.num, 9u);
}
END_TEST

START_TEST(load_keyed_item_callback_failure_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  char path[] = "/tmp/yamlcfg_test_XXXXXX";
  int rc;
  memset(&t, 0, sizeof t);
  t.item_fail = 1;
  write_cfg(path, "items:\n  - first:\n      opt: 5\n");
  rc = yamlcfg_load_items(&y, TOOL, 0, path, NULL, KEYS, NKEYS(KEYS), &t, item_cb);
  unlink(path);
  ck_assert_int_eq(rc, YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_top_level_list_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "- a\n- b\n", NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_top_level_scalar_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "just a string\n", NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_multiple_documents_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "count: 1\n---\ncount: 2\n", NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_aliases_are_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "name: &a hello\nfile: *a\n", NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_syntax_error_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "name: [unclosed\n", NULL), YAMLCFG_ERROR);
  ck_assert_int_eq(load_text(&y, &t, 0, "name: a\n\tcount: 1\n", NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_deep_nesting_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  char text[512] = "";
  for (int i = 0; i < 20; i++) {
    size_t n = strlen(text);
    memset(text + n, ' ', (size_t)i * 2);
    snprintf(text + n + (size_t)i * 2, sizeof text - n - (size_t)i * 2, "k%d:\n", i);
  }
  ck_assert_int_eq(load_text(&y, &t, 0, text, NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_overlong_key_path_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  char text[600];
  memset(text, 'k', 400);
  strcpy(text + 400, ": 1\n");
  ck_assert_int_eq(load_text(&y, &t, 0, text, NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_must_exist_checked_only_in_check_mode) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "file: /nonexistent/dir/file\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(y.warnings, 0u);
  ck_assert_int_eq(load_text(&y, &t, YAMLCFG_CHECK, "file: /nonexistent/dir/file\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(y.warnings, 1u);
  ck_assert_int_eq(load_text(&y, &t, YAMLCFG_CHECK, "file: /dev/null\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(y.warnings, 0u);
}
END_TEST

START_TEST(load_strict_with_must_exist_failure_is_error) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, YAMLCFG_STRICT, "file: /nonexistent/dir/file\n", NULL), YAMLCFG_ERROR);
}
END_TEST

START_TEST(load_resets_state_between_calls) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "bogus: 1\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(y.warnings, 1u);
  ck_assert_int_eq(load_text(&y, &t, 0, "count: 1\n", NULL), YAMLCFG_LOADED);
  ck_assert_uint_eq(y.warnings, 0u);
}
END_TEST

START_TEST(load_with_zero_keys_warns_on_everything) {
  yamlcfg_t y;
  tcfg_t t;
  char path[] = "/tmp/yamlcfg_test_XXXXXX";
  int rc;
  write_cfg(path, "a: 1\nb: 2\n");
  rc = yamlcfg_load(&y, TOOL, 0, path, NULL, NULL, 0, &t);
  unlink(path);
  ck_assert_int_eq(rc, YAMLCFG_LOADED);
  ck_assert_uint_eq(y.warnings, 2u);
}
END_TEST

START_TEST(warn_counts_and_report_reflects_mode) {
  yamlcfg_t y;
  tcfg_t t;
  ck_assert_int_eq(load_text(&y, &t, 0, "count: 1\n", NULL), YAMLCFG_LOADED);
  ck_assert_int_eq(yamlcfg_report(&y), 0);
  yamlcfg_warn(&y, "custom %d", 1);
  yamlcfg_warn(&y, "custom %d", 2);
  ck_assert_uint_eq(y.warnings, 2u);
  ck_assert_int_eq(yamlcfg_report(&y), 0);
  y.strict = 1;
  ck_assert_int_ne(yamlcfg_report(&y), 0);
  y.warnings = 0;
  ck_assert_int_eq(yamlcfg_report(&y), 0);
}
END_TEST

static Suite *yamlcfg_suite(void) {
  Suite *s = suite_create("yamlcfg");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, parse_bool_accepts_all_spellings);
  tcase_add_test(tc, parse_bool_rejects_garbage_and_keeps_output);
  tcase_add_test(tc, set_bool_reports_invalid_value);
  tcase_add_test(tc, set_str_copies_value);
  tcase_add_test(tc, set_str_rejects_empty);
  tcase_add_test(tc, set_uint_enforces_inclusive_range);
  tcase_add_test(tc, set_uint_rejects_non_numeric);
  tcase_add_test(tc, set_double_enforces_bounds);
  tcase_add_test(tc, set_double_exclusive_minimum);
  tcase_add_test(tc, set_double_rejects_malformed_and_non_finite);
  tcase_add_test(tc, set_lang_requires_three_letters);
  tcase_add_test(tc, set_addrport_parses_ipv4);
  tcase_add_test(tc, set_addrport_parses_ipv6);
  tcase_add_test(tc, set_addrport_accepts_null_family);
  tcase_add_test(tc, set_addrport_rejects_bad_input);
  tcase_add_test(tc, set_addrport_rejects_small_addr_buffer);
  tcase_add_test(tc, set_color_maps_modes);
  tcase_add_test(tc, set_color_rejects_unknown);
  tcase_add_test(tc, load_without_any_path_is_absent);
  tcase_add_test(tc, load_missing_default_is_absent);
  tcase_add_test(tc, load_missing_default_is_error_in_check_mode);
  tcase_add_test(tc, load_missing_explicit_path_is_error);
  tcase_add_test(tc, load_directory_is_error);
  tcase_add_test(tc, load_explicit_path_wins_over_default);
  tcase_add_test(tc, load_applies_scalars);
  tcase_add_test(tc, load_accepts_quoted_and_empty_document);
  tcase_add_test(tc, load_resolves_nested_key_paths);
  tcase_add_test(tc, load_skips_null_values);
  tcase_add_test(tc, load_quoted_null_is_a_value);
  tcase_add_test(tc, load_unknown_key_warns_by_default);
  tcase_add_test(tc, load_unknown_key_fails_in_strict_mode);
  tcase_add_test(tc, load_unknown_key_in_check_mode_is_loaded);
  tcase_add_test(tc, load_invalid_value_is_error);
  tcase_add_test(tc, load_invalid_value_is_warning_in_check_mode);
  tcase_add_test(tc, load_duplicate_key_warns_and_last_wins);
  tcase_add_test(tc, load_strict_duplicate_key_is_error);
  tcase_add_test(tc, load_collects_list_values);
  tcase_add_test(tc, load_accepts_flow_list_and_scalar_for_list_key);
  tcase_add_test(tc, load_skips_null_list_items);
  tcase_add_test(tc, load_list_for_scalar_key_is_error);
  tcase_add_test(tc, load_list_for_scalar_key_is_warning_in_check_mode);
  tcase_add_test(tc, load_nested_lists_are_error);
  tcase_add_test(tc, load_mapping_in_plain_list_is_error);
  tcase_add_test(tc, load_keyed_items_call_back_in_pairs);
  tcase_add_test(tc, load_keyed_item_callback_failure_is_error);
  tcase_add_test(tc, load_top_level_list_is_error);
  tcase_add_test(tc, load_top_level_scalar_is_error);
  tcase_add_test(tc, load_multiple_documents_is_error);
  tcase_add_test(tc, load_aliases_are_error);
  tcase_add_test(tc, load_syntax_error_is_error);
  tcase_add_test(tc, load_deep_nesting_is_error);
  tcase_add_test(tc, load_overlong_key_path_is_error);
  tcase_add_test(tc, load_must_exist_checked_only_in_check_mode);
  tcase_add_test(tc, load_strict_with_must_exist_failure_is_error);
  tcase_add_test(tc, load_resets_state_between_calls);
  tcase_add_test(tc, load_with_zero_keys_warns_on_everything);
  tcase_add_test(tc, warn_counts_and_report_reflects_mode);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(yamlcfg_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
