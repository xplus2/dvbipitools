/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/net/tssource.h"

static char *write_temp(const unsigned char *data, size_t len) {
  static char path[] = "/tmp/dvbipitools_tssrc_test_XXXXXX";
  char *p = strdup(path);
  int fd = mkstemp(p);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq(write(fd, data, len), (ssize_t)len);
  close(fd);
  return p;
}

static void fill_ts_packet(unsigned char *pkt, unsigned char tag) {
  size_t i;
  pkt[0] = 0x47;
  pkt[1] = 0x00;
  pkt[2] = 0x00;
  pkt[3] = 0x10;
  for (i = 4; i < 188; i++)
    pkt[i] = tag;
}

/* v2, no CSRC/extension, marker+PT arbitrary, sequence arbitrary */
static void fill_rtp_header(unsigned char *h, uint32_t ts, uint32_t seq) {
  h[0] = 0x80;
  h[1] = 33;
  h[2] = (unsigned char)(seq >> 8);
  h[3] = (unsigned char)seq;
  h[4] = (unsigned char)(ts >> 24);
  h[5] = (unsigned char)(ts >> 16);
  h[6] = (unsigned char)(ts >> 8);
  h[7] = (unsigned char)ts;
  h[8] = 0;
  h[9] = 0;
  h[10] = 0;
  h[11] = 1; /* ssrc */
}

static ssize_t read_all(tssrc_t *s, unsigned char *buf, size_t cap) {
  size_t got = 0;
  int tries;
  for (tries = 0; got < cap && tries < 100000; tries++) {
    ssize_t n = tssrc_read(s, buf + got, cap - got, NULL);
    if (n < 0)
      break;
    got += (size_t)n;
  }
  return (ssize_t)got;
}

START_TEST(tssrc_file_raw_ts_passthrough) {
  unsigned char content[20 * 188], readback[20 * 188];
  unsigned i;
  char *path;
  tssrc_cfg_t cfg;
  tssrc_t *s;

  for (i = 0; i < 20; i++)
    fill_ts_packet(content + i * 188, (unsigned char)i);

  path = write_temp(content, sizeof content);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_FILE;
  cfg.file_path = path;
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);

  ck_assert_int_eq(read_all(s, readback, sizeof readback), (ssize_t)sizeof readback);
  ck_assert_int_eq(memcmp(content, readback, sizeof content), 0);
  ck_assert_int_eq(tssrc_is_rtp_framed(s), 0);

  tssrc_close(s);
  unlink(path);
  free(path);
}
END_TEST

START_TEST(tssrc_file_rtp_framed_deframes) {
  unsigned char content[6 * (12 + 7 * 188)], expect_ts[6 * 7 * 188], readback[6 * 7 * 188];
  unsigned rec, k, off = 0, eoff = 0;
  char *path;
  tssrc_cfg_t cfg;
  tssrc_t *s;

  for (rec = 0; rec < 6; rec++) {
    fill_rtp_header(content + off, 90000 * rec, rec);
    off += 12;
    for (k = 0; k < 7; k++) {
      unsigned char tag = (unsigned char)(rec * 7 + k);
      fill_ts_packet(content + off, tag);
      fill_ts_packet(expect_ts + eoff, tag);
      off += 188;
      eoff += 188;
    }
  }
  ck_assert_uint_eq(off, sizeof content);
  ck_assert_uint_eq(eoff, sizeof expect_ts);

  path = write_temp(content, sizeof content);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_FILE;
  cfg.file_path = path;
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);

  ck_assert_int_eq(read_all(s, readback, sizeof readback), (ssize_t)sizeof readback);
  ck_assert_int_eq(memcmp(expect_ts, readback, sizeof expect_ts), 0);
  ck_assert_int_eq(tssrc_is_rtp_framed(s), 1);
  ck_assert_uint_eq(tssrc_last_rtp_ts(s), 90000u * 5u); /* last record's timestamp */

  tssrc_close(s);
  unlink(path);
  free(path);
}
END_TEST

START_TEST(tssrc_rewind_restarts_raw_file_from_zero) {
  unsigned char content[10 * 188], first_pass[188], second_pass[3 * 188];
  unsigned i;
  char *path;
  tssrc_cfg_t cfg;
  tssrc_t *s;

  for (i = 0; i < 10; i++)
    fill_ts_packet(content + i * 188, (unsigned char)i);

  path = write_temp(content, sizeof content);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_FILE;
  cfg.file_path = path;
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);

  ck_assert_int_eq(read_all(s, first_pass, sizeof first_pass), (ssize_t)sizeof first_pass);
  ck_assert_int_eq(memcmp(content, first_pass, sizeof first_pass), 0);

  tssrc_rewind(s);
  ck_assert_int_eq(read_all(s, second_pass, sizeof second_pass), (ssize_t)sizeof second_pass);
  ck_assert_int_eq(memcmp(content, second_pass, sizeof second_pass), 0); /* back at byte 0, not byte 188 */

  tssrc_close(s);
  unlink(path);
  free(path);
}
END_TEST

static Suite *tssource_file_suite(void) {
  Suite *s = suite_create("tssource_file");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, tssrc_file_raw_ts_passthrough);
  tcase_add_test(tc, tssrc_file_rtp_framed_deframes);
  tcase_add_test(tc, tssrc_rewind_restarts_raw_file_from_zero);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tssource_file_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
