/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../alloc_fail.h"
#include "lib/helper/dateutil.h"
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

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

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

START_TEST(dstrbuf_survives_allocation_failures) {
  dstrbuf_t sb;
  char big[20000];
  memset(big, 'y', sizeof big - 1);
  big[sizeof big - 1] = 0;
  alloc_fail_at(0);
  dstrbuf_init(&sb);
  alloc_fail_end();
  ck_assert_ptr_null(sb.buf);
  ck_assert_uint_eq(sb.cap, 0);
  dstrbuf_add_n(&sb, "x", 1);
  dstrbuf_appendf(&sb, "%d", 1);

  dstrbuf_init(&sb);
  ck_assert_ptr_nonnull(sb.buf);
  alloc_fail_at(0);
  dstrbuf_add(&sb, big);
  alloc_fail_end();
  ck_assert_ptr_null(sb.buf);
  ck_assert_uint_eq(sb.len, 0);
  dstrbuf_add(&sb, "z");

  dstrbuf_init(&sb);
  alloc_fail_at(0);
  dstrbuf_appendf(&sb, "%s", big);
  alloc_fail_end();
  ck_assert_ptr_null(sb.buf);
  ck_assert_uint_eq(sb.cap, 0);
  dstrbuf_appendf(&sb, "%d", 5);
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

START_TEST(bufcpy_truncation_forms) {
  static const struct {
    const char *src;
    size_t cap;
    const char *want;
  } t[] = {
      {"hello", 0, NULL},
      {"hello", 1, ""},
      {"hello", 3, "he"},
      {"hello", 6, "hello"},
      {"hello", 100, "hello"},
      {"a\xC3\xA9", 3, "a"},
      {"a\xC3\xA9", 4, "a\xC3\xA9"},
      {"a\xE2\x82\xAC", 4, "a"},
      {"a\xE2\x82\xAC", 3, "a"},
      {"a\xF0\x9F\x98\x80", 5, "a"},
      {"a\xF0\x9F\x98\x80", 4, "a"},
      {"a\xF0\x9F\x98\x80", 3, "a"},
      {"a\x80\x80\x80\x80\x80", 5, "a\x80\x80\x80"},
      {"abc\x80\x80\x80\x80", 5, "ab"},
  };
  char out[32];
  size_t r;
  memset(out, 'X', sizeof out);
  r = bufcpy(out, t[_i].cap, t[_i].src);
  ck_assert_uint_eq(r, strlen(t[_i].src));
  if (t[_i].want) ck_assert_str_eq(out, t[_i].want);
  else ck_assert_int_eq(out[0], 'X');
}
END_TEST

START_TEST(number_to_string_forms) {
  char b[32];
  ck_assert_uint_eq(uint_to_str(b, 0), 1u);
  ck_assert_str_eq(b, "0");
  ck_assert_uint_eq(uint_to_str(b, 4294967295u), 10u);
  ck_assert_str_eq(b, "4294967295");
  ck_assert_uint_eq(uint_to_str_pad(b, 7, 4), 4u);
  ck_assert_str_eq(b, "0007");
  ck_assert_uint_eq(uint_to_str_pad(b, 12345, 3), 5u);
  ck_assert_str_eq(b, "12345");
  ck_assert_uint_eq(uint_to_str_pad(b, 0, 3), 3u);
  ck_assert_str_eq(b, "000");
  ck_assert_uint_eq(u64_to_dec(b, 0), 1u);
  ck_assert_str_eq(b, "0");
  ck_assert_uint_eq(u64_to_dec(b, UINT64_MAX), 20u);
  ck_assert_str_eq(b, "18446744073709551615");
}
END_TEST

START_TEST(array_grow_policy) {
  int cap = 0;
  int *a = NULL;
  void *p;
  a = array_grow(a, &cap, 1, sizeof *a);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(cap, 16);
  p = array_grow(a, &cap, 10, sizeof *a);
  ck_assert_ptr_eq(p, a);
  ck_assert_int_eq(cap, 16);
  a = array_grow(a, &cap, 17, sizeof *a);
  ck_assert_int_eq(cap, 32);
  a = array_grow(a, &cap, 100, sizeof *a);
  ck_assert_int_eq(cap, 100);
  ck_assert_ptr_null(array_grow(a, &cap, INT_MAX, (size_t)1 << 31));
  ck_assert_int_eq(cap, 100);
  free(a);
}
END_TEST

START_TEST(growbuf_reserve_policy) {
  void *buf = NULL;
  size_t cap = 0;
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 4, 0, 8), 0);
  ck_assert_uint_eq(cap, 0u);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 4, 1, 0), 0);
  ck_assert_uint_eq(cap, 1u);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 4, 5, 0), 0);
  ck_assert_uint_eq(cap, 8u);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 4, 5, 0), 0);
  ck_assert_uint_eq(cap, 8u);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 0, 100, 0), -1);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, SIZE_MAX / 2, 100, 0), -1);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 1, SIZE_MAX - 5, 0), -1);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, (size_t)1 << 20, (size_t)1 << 40, 0), -1);
  ck_assert_uint_eq(cap, 8u);
  free(buf);
  buf = NULL;
  cap = 0;
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 2, 3, 64), 0);
  ck_assert_uint_eq(cap, 64u);
  free(buf);
}
END_TEST

START_TEST(digit_and_number_parsing) {
  unsigned long v = 0;
  ck_assert_int_eq(all_digits("123", 3), 1);
  ck_assert_int_eq(all_digits("12a", 3), 0);
  ck_assert_int_eq(all_digits("12a", 2), 1);
  ck_assert_int_eq(all_digits("", 0), 1);
  ck_assert_int_eq(parse_ulong("", 10, &v), -1);
  ck_assert_int_eq(parse_ulong("12x", 100, &v), -1);
  ck_assert_int_eq(parse_ulong("-1", 100, &v), -1);
  ck_assert_int_eq(parse_ulong("101", 100, &v), -1);
  ck_assert_int_eq(parse_ulong("100", 100, &v), 0);
  ck_assert_uint_eq(v, 100u);
  ck_assert_int_eq(parse_ulong("99999999999999999999999", ULONG_MAX, &v), -1);
  ck_assert_int_eq(parse_ulong("18446744073709551615", ULONG_MAX, &v), 0);
  ck_assert_int_eq(parse_ulong("18446744073709551616", ULONG_MAX, &v), -1);
}
END_TEST

START_TEST(chomp_and_csv) {
  char a[] = "line\r\n";
  char b[] = "\n\n";
  char c[] = "";
  char d[] = "x";
  char csv[] = "a,b,,d";
  char one[] = "solo";
  char *f[3];
  chomp(a);
  ck_assert_str_eq(a, "line");
  chomp(b);
  ck_assert_str_eq(b, "");
  chomp(c);
  chomp(d);
  ck_assert_str_eq(d, "x");
  ck_assert_uint_eq(csv_split(csv, f, 3), 3u);
  ck_assert_str_eq(f[0], "a");
  ck_assert_str_eq(f[1], "b");
  ck_assert_str_eq(f[2], "");
  ck_assert_uint_eq(csv_split(one, f, 3), 1u);
  ck_assert_str_eq(f[0], "solo");
  ck_assert_uint_eq(csv_split(one, f, 0), 0u);
}
END_TEST

START_TEST(csv_split_quoting_and_trim) {
  char a[] = " Name, With , \"Quoted, \"\"x\"\"\" ,\"\",  ";
  char b[] = "\"unterminated, still one";
  char c[] = "a,b,c,d";
  char d[] = "mid\"quote,x";
  char *f[5];
  ck_assert_uint_eq(csv_split(a, f, 5), 5u);
  ck_assert_str_eq(f[0], "Name");
  ck_assert_str_eq(f[1], "With");
  ck_assert_str_eq(f[2], "Quoted, \"x\"");
  ck_assert_str_eq(f[3], "");
  ck_assert_str_eq(f[4], "");
  ck_assert_uint_eq(csv_split(b, f, 5), 1u);
  ck_assert_str_eq(f[0], "unterminated, still one");
  ck_assert_uint_eq(csv_split(c, f, 2), 2u);
  ck_assert_str_eq(f[1], "b");
  ck_assert_uint_eq(csv_split(d, f, 5), 2u);
  ck_assert_str_eq(f[0], "mid\"quote");
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

START_TEST(iso8601_forms) {
  static const struct {
    const char *in;
    int rc;
    iso8601_offset_kind_t kind;
    int off;
  } t[] = {
      {"2020-12-15T12:30:45Z", 0, ISO8601_OFF_Z, 0},
      {"2020-12-15T12:30:45", 0, ISO8601_OFF_NONE, 0},
      {"2020-12-15T12:30:45.123Z", 0, ISO8601_OFF_Z, 0},
      {"2020-12-15T12:30:45.5", 0, ISO8601_OFF_NONE, 0},
      {"2020-12-15T12:30:45+01:30", 0, ISO8601_OFF_NUMERIC, 90},
      {"2020-12-15T12:30:45-05:00", 0, ISO8601_OFF_NUMERIC, -300},
      {"2020-12-15T12:30:45.9+00:00", 0, ISO8601_OFF_NUMERIC, 0},
      {"2020-12-15T12:30:45.Z", -1, 0, 0},
      {"2020-12-15T12:30:45z", -1, 0, 0},
      {"2020-12-15T12:30:45ZZ", -1, 0, 0},
      {"2020-12-15T12:30:45+0100", -1, 0, 0},
      {"2020-12-15T12:30:45+24:00", -1, 0, 0},
      {"2020-12-15T12:30:45+01:60", -1, 0, 0},
      {"2020-12-15T12:30:45+0a:00", -1, 0, 0},
      {"2020-12-15T12:30:45 ", -1, 0, 0},
      {"2020-12-15 12:30:45", -1, 0, 0},
      {"2020-12-15T12:30:4", -1, 0, 0},
      {"2020-1-15T12:30:45Z", -1, 0, 0},
      {"2020-12-1ST12:30:45Z", -1, 0, 0},
      {"2020-13-15T12:30:45Z", -1, 0, 0},
      {"2020-00-15T12:30:45Z", -1, 0, 0},
      {"2020-12-00T12:30:45Z", -1, 0, 0},
      {"2020-12-32T12:30:45Z", -1, 0, 0},
      {"2021-02-29T12:30:45Z", -1, 0, 0},
      {"2020-02-29T12:30:45Z", 0, ISO8601_OFF_Z, 0},
      {"1900-02-29T12:30:45Z", -1, 0, 0},
      {"2000-02-29T12:30:45Z", 0, ISO8601_OFF_Z, 0},
      {"2020-04-31T12:30:45Z", -1, 0, 0},
      {"2020-12-15T24:30:45Z", -1, 0, 0},
      {"2020-12-15T12:60:45Z", -1, 0, 0},
      {"2020-12-15T12:30:60Z", -1, 0, 0},
      {"", -1, 0, 0},
      {"2020x12-15T12:30:45Z", -1, 0, 0},
      {"2020-12x15T12:30:45Z", -1, 0, 0},
      {"2020-12-15X12:30:45Z", -1, 0, 0},
      {"2020-12-15T12x30:45Z", -1, 0, 0},
      {"2020-12-15T12:30x45Z", -1, 0, 0},
      {"a020-12-15T12:30:45Z", -1, 0, 0},
      {"2020-a2-15T12:30:45Z", -1, 0, 0},
      {"2020-12-a5T12:30:45Z", -1, 0, 0},
      {"2020-12-15Ta2:30:45Z", -1, 0, 0},
      {"2020-12-15T12:a0:45Z", -1, 0, 0},
      {"2020-12-15T12:30:a5Z", -1, 0, 0},
  };
  iso8601_t o;
  int rc = iso8601_split(t[_i].in, &o);
  ck_assert_int_eq(rc, t[_i].rc);
  if (rc == 0) {
    ck_assert_int_eq(o.offset_kind, t[_i].kind);
    ck_assert_int_eq(o.off_min, t[_i].off);
  }
}
END_TEST

START_TEST(iso8601_edge_cases_and_formatting) {
  static const char *const bad[] = {
    "2024-05-01T10:20:30.", "2024-05-01T10:20:30.Z", "2024-05-01T10:20:30+24:00", "2024-05-01T10:20:30+01:60", "2024-05-01T10:20:30+0100", "2024-05-01T10:20:30+01:0x",
    "2024-05-01T10:20:30Zx", "2024-05-01T10:20:30z", "2024-13-01T10:20:30Z", "2024-00-01T10:20:30Z", "2024-02-30T10:20:30Z", "2023-02-29T10:20:30Z", "1900-02-29T10:20:30Z",
    "2024-05-32T10:20:30Z", "2024-05-00T10:20:30Z", "2024-05-01T24:20:30Z", "2024-05-01T10:60:30Z", "2024-05-01T10:20:60Z", "2024-05-01X10:20:30Z", "2024/05/01T10:20:30Z",
    "20a4-05-01T10:20:30Z", "2024-05-01T10:20", "", "2024-05-01T10-20-30Z",
  };
  static const char *const good[] = {"2024-02-29T10:20:30Z", "2000-02-29T00:00:00", "2024-05-01T10:20:30.123456Z", "2024-05-01T10:20:30.5+05:30", "2024-05-01T10:20:30-23:59", "2024-12-31T23:59:59Z"};
  iso8601_t t;
  char out[32];
  char small[8];
  int y;
  int mo;
  int d;
  for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) ck_assert_msg(iso8601_split(bad[i], &t) == -1, "accepted %s", bad[i]);
  for (size_t i = 0; i < sizeof good / sizeof *good; i++) ck_assert_msg(iso8601_split(good[i], &t) == 0, "rejected %s", good[i]);
  ck_assert_int_eq(iso8601_split("2024-05-01T10:20:30-23:59", &t), 0);
  ck_assert_int_eq(t.off_min, -(23 * 60 + 59));
  ck_assert_int_eq(t.offset_kind, ISO8601_OFF_NUMERIC);
  ck_assert_int_eq(iso8601_split("2024-05-01T10:20:30Z", &t), 0);
  ck_assert_int_eq(t.offset_kind, ISO8601_OFF_Z);
  ck_assert_int_eq(iso8601_split("2024-05-01T10:20:30", &t), 0);
  ck_assert_int_eq(t.offset_kind, ISO8601_OFF_NONE);
  ck_assert_int_eq(iso8601_split("2024-05-01T10:20:30+01:30", &t), 0);
  ck_assert_int_eq(iso8601_mjd_minutes(&t), date_to_mjd(2024, 5, 1) * 1440L + 10 * 60 + 20 - 90);
  ck_assert_int_eq(iso8601_format(out, sizeof out, 0, 3725), 19);
  ck_assert_str_eq(out, "1970-01-01T01:02:05");
  ck_assert_int_eq(iso8601_format(small, sizeof small, 0, 0), -1);
  ck_assert_int_eq(iso8601_format(out, sizeof out, -1, 86399), 19);
  ck_assert_str_eq(out, "1969-12-31T23:59:59");
  mjd_to_date(date_to_mjd(1600, 2, 29), &y, &mo, &d);
  ck_assert_int_eq(y * 10000 + mo * 100 + d, 16000229);
  mjd_to_date(date_to_mjd(1, 1, 1), &y, &mo, &d);
  ck_assert_int_eq(y * 10000 + mo * 100 + d, 10101);
  mjd_to_date(date_to_mjd(-5, 12, 31), &y, &mo, &d);
  ck_assert_int_eq(y, -5);
}
END_TEST

START_TEST(mjd_known_dates) {
  ck_assert_int_eq(date_to_mjd(1993, 10, 13), 49273);
  ck_assert_int_eq(date_to_mjd(2020, 12, 15), 59198);
  ck_assert_int_eq(date_to_mjd(2020, 1, 1), 58849);
  ck_assert_int_eq(date_to_mjd(2020, 2, 29), 58908);
}
END_TEST

START_TEST(sbuf_truncation_and_forms) {
  char buf[8];
  sbuf_t b;
  sbuf_init(&b, buf, sizeof buf);
  sbuf_add(&b, "abc");
  sbuf_add_n(&b, "defgh", 2);
  ck_assert_str_eq(buf, "abcde");
  ck_assert_int_eq(b.truncated, 0);
  sbuf_add(&b, "xyz123");
  ck_assert_str_eq(buf, "abcdexy");
  ck_assert_int_eq(b.truncated, 1);
  sbuf_add(&b, "q");
  ck_assert_str_eq(buf, "abcdexy");
  sbuf_init(&b, buf, sizeof buf);
  sbuf_add_u64(&b, 1234567);
  ck_assert_str_eq(buf, "1234567");
  sbuf_init(&b, buf, sizeof buf);
  sbuf_add_uint(&b, 42);
  sbuf_add_hex2(&b, 0xAB);
  sbuf_add_hex2(&b, 0x105);
  ck_assert_str_eq(buf, "42ab05");
  sbuf_init(&b, buf, sizeof buf);
  sbuf_add_hex(&b, 0);
  sbuf_add_hex(&b, 0xdeadbeef);
  ck_assert_int_eq(b.truncated, 1);
  sbuf_init(&b, buf, sizeof buf);
  sbuf_add_hex(&b, 0x1f);
  ck_assert_str_eq(buf, "1f");
}
END_TEST

START_TEST(sbuf_zero_capacity) {
  sbuf_t b;
  sbuf_init(&b, NULL, 0);
  sbuf_add(&b, "");
  ck_assert_int_eq(b.truncated, 0);
  sbuf_add(&b, "x");
  ck_assert_int_eq(b.truncated, 1);
  sbuf_init(&b, NULL, 0);
  sbuf_add_n(&b, "x", 0);
  ck_assert_int_eq(b.truncated, 0);
}
END_TEST

START_TEST(dstrbuf_growth_and_numbers) {
  dstrbuf_t sb;
  char *big = malloc(20000);
  ck_assert_ptr_nonnull(big);
  memset(big, 'z', 19999);
  big[19999] = '\0';
  dstrbuf_init(&sb);
  ck_assert_ptr_nonnull(sb.buf);
  dstrbuf_add(&sb, "ab");
  dstrbuf_add_n(&sb, "cdef", 2);
  dstrbuf_add_u64(&sb, 7);
  dstrbuf_add_i64(&sb, -9);
  dstrbuf_add_i64(&sb, 5);
  dstrbuf_add_i64(&sb, INT64_MIN);
  ck_assert_str_eq(sb.buf, "abcd7-95-9223372036854775808");
  dstrbuf_add(&sb, big);
  ck_assert_uint_eq(sb.len, strlen("abcd7-95-9223372036854775808") + 19999);
  ck_assert_uint_ge(sb.cap, sb.len + 1);
  free(sb.buf);
  free(big);
}
END_TEST

START_TEST(dstrbuf_appendf_growth) {
  dstrbuf_t sb;
  char *big = malloc(30000);
  ck_assert_ptr_nonnull(big);
  memset(big, 'y', 29999);
  big[29999] = '\0';
  dstrbuf_init(&sb);
  dstrbuf_appendf(&sb, "%s-%d", "x", 5);
  ck_assert_str_eq(sb.buf, "x-5");
  dstrbuf_appendf(&sb, "%s", big);
  ck_assert_uint_eq(sb.len, 3u + 29999u);
  dstrbuf_appendf(&sb, "%s", "tail");
  ck_assert_uint_eq(sb.len, 3u + 29999u + 4u);
  ck_assert_int_eq(sb.buf[sb.len], 0);
  free(sb.buf);
  free(big);
}
END_TEST

START_TEST(dstrbuf_null_buffer_is_inert) {
  dstrbuf_t sb;
  memset(&sb, 0, sizeof sb);
  dstrbuf_add(&sb, "abc");
  dstrbuf_add_u64(&sb, 1);
  dstrbuf_add_i64(&sb, -1);
  dstrbuf_appendf(&sb, "%d", 1);
  ck_assert_ptr_null(sb.buf);
  ck_assert_uint_eq(sb.len, 0u);
}
END_TEST

START_TEST(misc_inline_helpers) {
  ck_assert_uint_eq(next_pow2(0), 1u);
  ck_assert_uint_eq(next_pow2(1), 1u);
  ck_assert_uint_eq(next_pow2(2), 2u);
  ck_assert_uint_eq(next_pow2(3), 4u);
  ck_assert_uint_eq(next_pow2(1000), 1024u);
  ck_assert_int_ge(now_ms(), 0);
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
  tcase_add_loop_test(tc, bufcpy_truncation_forms, 0, 14);
  tcase_add_test(tc, number_to_string_forms);
  tcase_add_test(tc, array_grow_policy);
  tcase_add_test(tc, growbuf_reserve_policy);
  tcase_add_test(tc, digit_and_number_parsing);
  tcase_add_test(tc, chomp_and_csv);
  tcase_add_test(tc, read_line_forms);
  tcase_add_test(tc, csv_split_quoting_and_trim);
  tcase_add_test(tc, read_line_exact_fit_at_eof_and_newline);
  tcase_add_loop_test(tc, iso8601_forms, 0, 43);
  tcase_add_test(tc, mjd_known_dates);
  tcase_add_test(tc, sbuf_truncation_and_forms);
  tcase_add_test(tc, sbuf_zero_capacity);
  tcase_add_test(tc, dstrbuf_growth_and_numbers);
  tcase_add_test(tc, dstrbuf_appendf_growth);
  tcase_add_test(tc, dstrbuf_null_buffer_is_inert);
  tcase_add_test(tc, misc_inline_helpers);
  tcase_add_test(tc, pipe_write_all_plain);
  tcase_add_test(tc, pipe_write_all_stops_on_flag_when_full);
  tcase_add_test(tc, pipe_write_all_waits_for_reader_after_eagain);
  tcase_add_test(tc, pipe_write_all_reports_broken_pipe);
  tcase_add_test(tc, pipe_write_all_retries_after_eintr);
  tcase_add_test(tc, rand_seed_falls_back_without_getrandom);
  tcase_add_test(tc, read_all_survives_allocation_failures);
  tcase_add_test(tc, dstrbuf_survives_allocation_failures);
  tcase_add_test(tc, iso8601_edge_cases_and_formatting);
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
