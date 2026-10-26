/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../../alloc_fail.h"
#include "lib/helper/uriparse.h"
#include "lib/sys/ioutil.h"

typedef struct {
  const char *in;
  int rc;
  int family;
  const char *group;
  unsigned port;
} mcast_case_t;

static const mcast_case_t mcast_cases[] = {
    {"239.1.2.3:5000", 0, AF_INET, "239.1.2.3", 5000},
    {"224.0.0.0:1", 0, AF_INET, "224.0.0.0", 1},
    {"239.255.255.255:65535", 0, AF_INET, "239.255.255.255", 65535},
    {"[ff02::1]:5000", 0, AF_INET6, "ff02::1", 5000},
    {"[ff3e::8000:1234]:1234", 0, AF_INET6, "ff3e::8000:1234", 1234},
    {"223.255.255.255:5000", -1, 0, "", 0},
    {"240.0.0.0:5000", -1, 0, "", 0},
    {"192.168.1.1:5000", -1, 0, "", 0},
    {"0.0.0.0:5000", -1, 0, "", 0},
    {"[fe80::1]:5000", -1, 0, "", 0},
    {"[::1]:5000", -1, 0, "", 0},
    {"239.1.2.3", -1, 0, "", 0},
    {"239.1.2.3:", -1, 0, "", 0},
    {"239.1.2.3:0", -1, 0, "", 0},
    {"239.1.2.3:65536", -1, 0, "", 0},
    {"239.1.2.3:abc", -1, 0, "", 0},
    {"239.1.2.3:5000x", -1, 0, "", 0},
    {":5000", -1, 0, "", 0},
    {"host:5000", -1, 0, "", 0},
    {"239.1.2:5000", -1, 0, "", 0},
    {"[ff02::1]", -1, 0, "", 0},
    {"[ff02::1:5000", -1, 0, "", 0},
    {"[ff02::1]5000", -1, 0, "", 0},
    {"[]:5000", -1, 0, "", 0},
    {"[ff02::zz]:5000", -1, 0, "", 0},
    {"", -1, 0, "", 0},
};

START_TEST(mcast_addrport_table) {
  const mcast_case_t *c = &mcast_cases[_i];
  char group[64];
  int family = -1;
  unsigned port = 0;
  int rc = uriparse_mcast_addrport(c->in, &family, group, sizeof group, &port);

  ck_assert_msg(rc == c->rc, "'%s': rc %d, want %d", c->in, rc, c->rc);
  if (rc == 0) {
    ck_assert_msg(family == c->family, "'%s': family %d", c->in, family);
    ck_assert_msg(strcmp(group, c->group) == 0, "'%s': group '%s'", c->in, group);
    ck_assert_msg(port == c->port, "'%s': port %u", c->in, port);
  }
}
END_TEST

START_TEST(mcast_addrport_respects_group_buffer_size) {
  char group[16];
  int family;
  unsigned port;

  ck_assert_int_eq(uriparse_mcast_addrport("239.1.2.3:5000", &family, group, 9, &port), -1);
  ck_assert_int_eq(uriparse_mcast_addrport("239.1.2.3:5000", &family, group, 10, &port), 0);
  ck_assert_str_eq(group, "239.1.2.3");
}
END_TEST

typedef struct {
  int family;
  const char *group;
  unsigned port;
  size_t cap;
  const char *expect;
} describe_case_t;

static const describe_case_t describe_cases[] = {
    {AF_INET, "239.1.2.3", 5000, 64, "239.1.2.3:5000"},
    {AF_INET6, "ff02::1", 5000, 64, "[ff02::1]:5000"},
    {AF_INET, "239.1.2.3", 5000, 8, "239.1.2"},
    {AF_INET, "239.1.2.3", 5000, 15, "239.1.2.3:5000"},
    {AF_INET, "239.1.2.3", 5000, 14, "239.1.2.3:500"},
    {AF_INET6, "ff02::1", 1, 5, "[ff0"},
    {AF_INET, "x", 1, 1, ""},
};

START_TEST(mcast_describe_formats_and_truncates) {
  const describe_case_t *c = &describe_cases[_i];
  char *buf = malloc(c->cap);

  ck_assert_ptr_nonnull(buf);
  uriparse_mcast_describe(c->family, c->group, c->port, buf, c->cap);
  ck_assert_str_eq(buf, c->expect);
  free(buf);
}
END_TEST

START_TEST(mcast_describe_leaves_zero_capacity_buffer_alone) {
  char buf[4] = {'X', 'X', 'X', 'X'};

  uriparse_mcast_describe(AF_INET, "239.1.2.3", 5000, buf, 0);
  ck_assert_mem_eq(buf, "XXXX", 4);
}
END_TEST

typedef struct {
  const char *uri;
  size_t rtmp_cap;
  size_t file_cap;
  int rc;
  const char *rtmp;
  const char *file;
} target_case_t;

static const target_case_t target_cases[] = {
    {"rtmp://host/app/key", 64, 64, 1, "rtmp://host/app/key", ""},
    {"rtmps://host/app/key", 64, 64, 2, "rtmps://host/app/key", ""},
    {"/var/tmp/out.ts", 64, 64, 0, "", "/var/tmp/out.ts"},
    {"out.ts", 64, 64, 0, "", "out.ts"},
    {"", 64, 64, 0, "", ""},
    {"RTMP://host/app", 64, 64, 0, "", "RTMP://host/app"},
    {"rtmp:/host", 64, 64, 0, "", "rtmp:/host"},
    {"rtmp://h", 9, 64, 1, "rtmp://h", ""},
    {"rtmp://h", 8, 64, -1, "", ""},
    {"rtmps://h", 9, 64, -1, "", ""},
    {"abcd", 64, 5, 0, "", "abcd"},
    {"abcde", 64, 5, -1, "", ""},
};

START_TEST(rtmp_or_file_classifies_and_bounds_targets) {
  const target_case_t *c = &target_cases[_i];
  char rtmp[64];
  char file[64];
  int rc;

  rtmp[0] = '\0';
  file[0] = '\0';
  rc = uriparse_rtmp_or_file(c->uri, rtmp, c->rtmp_cap, file, c->file_cap);
  ck_assert_msg(rc == c->rc, "'%s': rc %d, want %d", c->uri, rc, c->rc);
  ck_assert_msg(strcmp(rtmp, c->rtmp) == 0, "'%s': rtmp '%s'", c->uri, rtmp);
  ck_assert_msg(strcmp(file, c->file) == 0, "'%s': file '%s'", c->uri, file);
}
END_TEST

typedef struct {
  const char *in;
  int rc;
  int family;
  const char *src;
  const char *group;
  unsigned port;
} ssm_case_t;

static const ssm_case_t ssm_cases[] = {
    {"232.1.2.3:5000", 0, AF_INET, "", "232.1.2.3", 5000},
    {"@232.1.2.3:5000", 0, AF_INET, "", "232.1.2.3", 5000},
    {"10.0.0.1@232.1.2.3:5000", 0, AF_INET, "10.0.0.1", "232.1.2.3", 5000},
    {"2001:db8::1@[ff3e::8000:1]:5000", 0, AF_INET6, "2001:db8::1", "ff3e::8000:1", 5000},
    {"[2001:db8::1]@[ff3e::8000:1]:5000", 0, AF_INET6, "2001:db8::1", "ff3e::8000:1", 5000},
    {"2001:db8::1@232.1.2.3:5000", -1, 0, "", "", 0},
    {"10.0.0.1@[ff3e::1]:5000", -1, 0, "", "", 0},
    {"239.1.1.1@232.1.2.3:5000", -1, 0, "", "", 0},
    {"0.0.0.0@232.1.2.3:5000", -1, 0, "", "", 0},
    {"host@232.1.2.3:5000", -1, 0, "", "", 0},
    {"10.0.0.1@192.168.1.1:5000", -1, 0, "", "", 0},
};

START_TEST(mcast_src_addrport_table) {
  const ssm_case_t *c = &ssm_cases[_i];
  int family = 0;
  unsigned port = 0;
  char group[64] = "", src[64] = "";
  int rc = uriparse_mcast_src_addrport(c->in, &family, group, sizeof group, &port, src, sizeof src);
  ck_assert_msg(rc == c->rc, "'%s': rc %d, want %d", c->in, rc, c->rc);
  if (rc) return;
  ck_assert_int_eq(family, c->family);
  ck_assert_str_eq(src, c->src);
  ck_assert_str_eq(group, c->group);
  ck_assert_uint_eq(port, c->port);
}
END_TEST

START_TEST(mcast_src_uri_formats) {
  char buf[128];
  uriparse_mcast_src_uri(buf, sizeof buf, "udp", AF_INET, "10.0.0.1", "232.1.2.3", 5000);
  ck_assert_str_eq(buf, "udp://10.0.0.1@232.1.2.3:5000");
  uriparse_mcast_src_uri(buf, sizeof buf, "rtp", AF_INET, "", "239.1.2.3", 5000);
  ck_assert_str_eq(buf, "rtp://@239.1.2.3:5000");
  uriparse_mcast_src_uri(buf, sizeof buf, "rtp", AF_INET6, "2001:db8::1", "ff3e::1", 5000);
  ck_assert_str_eq(buf, "rtp://2001:db8::1@[ff3e::1]:5000");
}
END_TEST

START_TEST(pct_encode_seg_forms) {
  static const struct {
    const char *in;
    size_t cap;
    const char *want;
  } t[] = {
      {"abc-._~XYZ09", 64, "abc-._~XYZ09"},
      {"Das Erste HD/ZDF-1._~", 64, "Das%20Erste%20HD%2FZDF-1._~"},
      {"a b/c", 64, "a%20b%2Fc"},
      {"\xC3\xA9", 64, "%C3%A9"},
      {"a b", 4, "a"},
      {"a b", 5, "a%20"},
      {"abcdef", 4, "abc"},
      {"", 4, ""},
  };
  char out[64];
  memset(out, 'X', sizeof out);
  uriparse_pct_encode_seg(t[_i].in, out, t[_i].cap);
  ck_assert_str_eq(out, t[_i].want);
  memset(out, 'X', sizeof out);
  uriparse_pct_encode_seg("abc", out, 0);
  ck_assert_int_eq(out[0], 'X');
}
END_TEST

START_TEST(pct_decode_forms) {
  static const struct {
    const char *in;
    int rc;
    const char *want;
  } t[] = {
      {"a%20b%2Fc", 0, "a b/c"},
      {"%C3%a9", 0, "\xC3\xA9"},
      {"plain", 0, "plain"},
      {"", 0, ""},
      {"a%2", -1, NULL},
      {"a%zz", -1, NULL},
  };
  char buf[32];
  bufcpy(buf, sizeof buf, t[_i].in);
  ck_assert_int_eq(uriparse_pct_decode(buf), t[_i].rc);
  if (!t[_i].rc) ck_assert_str_eq(buf, t[_i].want);
}
END_TEST

static const struct {
  const char *in;
  const char *out;
} redact_cases[] = {
    {"rtp://user:pw@host:1234/p", "rtp://host:1234/p"},
    {"rtp://user:p@ss@host:1234/p", "rtp://host:1234/p"},
    {"rtp://host/p@th?x=1#f", "rtp://host/p@th"},
    {"rtp://@host", "rtp://@host"},
    {"host:1234", "host:1234"},
};

START_TEST(redact_drops_userinfo_up_to_last_at) {
  char *r = uriparse_redact_dup(redact_cases[_i].in);
  ck_assert_ptr_nonnull(r);
  ck_assert_str_eq(r, redact_cases[_i].out);
  free(r);
}
END_TEST

START_TEST(mcast_cfg_src_edge_cases_and_redact_oom) {
  static const char *const bad_src[] = {
    "[2001:db8::1@[ff3e::1]:5000", "@", "@232.1.2.3", "[::]@[ff3e::1]:5000", "[ff05::1]@[ff3e::1]:5000", "2001:db8::1@232.1.2.3:5000",
    "10.0.0.1@[ff3e::1]:5000", "10.0.0.1@232.1.2.3", "10.0.0.1@10.0.0.2:5000",
  };
  static const char *const good_src[] = {"[2001:db8::1]@[ff3e::1]:5000", "@232.1.2.3:5000", "10.0.0.1@232.1.2.3:5000", "232.1.2.3:5000"};
  int family = 0;
  unsigned port = 0;
  char group[64];
  char src[64];
  char err[96];
  char small[4];
  char *r;
  int rc;
  ck_assert_int_eq(uriparse_mcast_cfg("239.1.2.3:5000", &family, group, sizeof group, &port, err, sizeof err), 0);
  ck_assert_int_eq(uriparse_mcast_cfg("10.0.0.1:5000", &family, group, sizeof group, &port, err, sizeof err), -1);
  ck_assert_ptr_nonnull(strstr(err, "multicast"));
  for (size_t k = 0; k < sizeof bad_src / sizeof *bad_src; k++)
    ck_assert_msg(uriparse_mcast_src_addrport(bad_src[k], &family, group, sizeof group, &port, src, sizeof src) == -1, "accepted %s", bad_src[k]);
  for (size_t k = 0; k < sizeof good_src / sizeof *good_src; k++)
    ck_assert_msg(uriparse_mcast_src_addrport(good_src[k], &family, group, sizeof group, &port, src, sizeof src) == 0, "rejected %s", good_src[k]);
  ck_assert_int_eq(uriparse_mcast_src_addrport("10.0.0.1@232.1.2.3:5000", &family, group, sizeof group, &port, small, sizeof small), -1);
  alloc_fail_at(0);
  r = uriparse_redact_dup("rtp://user:pw@host:80/x?q#f");
  alloc_fail_end();
  ck_assert_ptr_null(r);
  r = uriparse_redact_dup("no-scheme?q");
  ck_assert_str_eq(r, "no-scheme?q");
  free(r);
  rc = uriparse_pct_decode((char[]){"%4"});
  ck_assert_int_eq(rc, -1);
  rc = uriparse_pct_decode((char[]){"%zz"});
  ck_assert_int_eq(rc, -1);
  uriparse_pct_encode_seg("a b", small, sizeof small);
  ck_assert_str_eq(small, "a");
  uriparse_pct_encode_seg("abcdef", small, 0);
}
END_TEST

static Suite *uriparse_suite(void) {
  Suite *s = suite_create("uriparse");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, mcast_addrport_table, 0, (int)(sizeof mcast_cases / sizeof mcast_cases[0]));
  tcase_add_test(tc, mcast_addrport_respects_group_buffer_size);
  tcase_add_loop_test(tc, mcast_src_addrport_table, 0, (int)(sizeof ssm_cases / sizeof ssm_cases[0]));
  tcase_add_test(tc, mcast_src_uri_formats);
  tcase_add_loop_test(tc, mcast_describe_formats_and_truncates, 0, (int)(sizeof describe_cases / sizeof describe_cases[0]));
  tcase_add_test(tc, mcast_describe_leaves_zero_capacity_buffer_alone);
  tcase_add_loop_test(tc, rtmp_or_file_classifies_and_bounds_targets, 0, (int)(sizeof target_cases / sizeof target_cases[0]));
  tcase_add_loop_test(tc, pct_encode_seg_forms, 0, 8);
  tcase_add_loop_test(tc, pct_decode_forms, 0, 6);
  tcase_add_loop_test(tc, redact_drops_userinfo_up_to_last_at, 0, (int)(sizeof redact_cases / sizeof redact_cases[0]));
  tcase_add_test(tc, mcast_cfg_src_edge_cases_and_redact_oom);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(uriparse_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
