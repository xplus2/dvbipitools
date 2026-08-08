/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/cas/simulcrypt_msg.h"

START_TEST(hdr_write_then_parse_round_trip) {
  unsigned char buf[SIMULCRYPT_HDR_LEN];
  simulcrypt_hdr_t hdr;
  size_t n = simulcrypt_hdr_write(3, 0x0201, 42, buf, sizeof buf);
  ck_assert_uint_eq(n, SIMULCRYPT_HDR_LEN);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, sizeof buf, &hdr), 0);
  ck_assert_uint_eq(hdr.version, 3);
  ck_assert_uint_eq(hdr.type, 0x0201);
  ck_assert_uint_eq(hdr.payload_len, 42);
}
END_TEST

START_TEST(hdr_write_rejects_small_cap) {
  unsigned char buf[SIMULCRYPT_HDR_LEN - 1];
  ck_assert_uint_eq(simulcrypt_hdr_write(2, 1, 1, buf, sizeof buf), 0u);
}
END_TEST

START_TEST(hdr_parse_rejects_short_buffer) {
  unsigned char buf[SIMULCRYPT_HDR_LEN - 1] = {0};
  simulcrypt_hdr_t hdr;
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, sizeof buf, &hdr), -1);
}
END_TEST

START_TEST(tlv_round_trip_multiple_elements) {
  unsigned char frame[64];
  simulcrypt_writer_t w;
  simulcrypt_tlv_reader_t r;
  unsigned short tag;
  const unsigned char *value;
  unsigned short vlen;
  size_t total;
  simulcrypt_hdr_t hdr;

  static const unsigned char v1[] = {0xAA, 0xBB};
  static const unsigned char v3[] = {1, 2, 3, 4};

  ck_assert_int_eq(simulcrypt_writer_begin(&w, frame, sizeof frame, 2, 0x0111), 0);
  ck_assert_int_eq(simulcrypt_writer_put_tlv(&w, 0x0001, v1, sizeof v1), 0);
  ck_assert_int_eq(simulcrypt_writer_put_tlv(&w, 0x0002, NULL, 0), 0);
  ck_assert_int_eq(simulcrypt_writer_put_tlv(&w, 0x0003, v3, sizeof v3), 0);
  total = simulcrypt_writer_finish(&w);
  ck_assert_uint_eq(total, SIMULCRYPT_HDR_LEN + (4 + 2) + (4 + 0) + (4 + 4));

  ck_assert_int_eq(simulcrypt_hdr_parse(frame, total, &hdr), 0);
  ck_assert_uint_eq(hdr.version, 2);
  ck_assert_uint_eq(hdr.type, 0x0111);
  ck_assert_uint_eq(hdr.payload_len, total - SIMULCRYPT_HDR_LEN);

  simulcrypt_tlv_reader_init(&r, frame + SIMULCRYPT_HDR_LEN, hdr.payload_len);

  ck_assert_int_eq(simulcrypt_tlv_reader_next(&r, &tag, &value, &vlen), 1);
  ck_assert_uint_eq(tag, 0x0001);
  ck_assert_uint_eq(vlen, sizeof v1);
  ck_assert_mem_eq(value, v1, sizeof v1);

  ck_assert_int_eq(simulcrypt_tlv_reader_next(&r, &tag, &value, &vlen), 1);
  ck_assert_uint_eq(tag, 0x0002);
  ck_assert_uint_eq(vlen, 0);

  ck_assert_int_eq(simulcrypt_tlv_reader_next(&r, &tag, &value, &vlen), 1);
  ck_assert_uint_eq(tag, 0x0003);
  ck_assert_uint_eq(vlen, sizeof v3);
  ck_assert_mem_eq(value, v3, sizeof v3);

  ck_assert_int_eq(simulcrypt_tlv_reader_next(&r, &tag, &value, &vlen), 0);
}
END_TEST

START_TEST(tlv_reader_clean_end_on_empty_payload) {
  simulcrypt_tlv_reader_t r;
  unsigned short tag, vlen;
  const unsigned char *value;
  simulcrypt_tlv_reader_init(&r, NULL, 0);
  ck_assert_int_eq(simulcrypt_tlv_reader_next(&r, &tag, &value, &vlen), 0);
}
END_TEST

START_TEST(tlv_reader_rejects_truncated_tlv_header) {
  static const unsigned char buf[] = {0x00, 0x01, 0x00}; /* 3 bytes, need 4 for tag+length */
  simulcrypt_tlv_reader_t r;
  unsigned short tag, vlen;
  const unsigned char *value;
  simulcrypt_tlv_reader_init(&r, buf, sizeof buf);
  ck_assert_int_eq(simulcrypt_tlv_reader_next(&r, &tag, &value, &vlen), -1);
}
END_TEST

START_TEST(tlv_reader_rejects_truncated_value) {
  /* tag 0x0001, declared length 10, but only 2 bytes of value follow */
  static const unsigned char buf[] = {0x00, 0x01, 0x00, 0x0A, 0xAA, 0xBB};
  simulcrypt_tlv_reader_t r;
  unsigned short tag, vlen;
  const unsigned char *value;
  simulcrypt_tlv_reader_init(&r, buf, sizeof buf);
  ck_assert_int_eq(simulcrypt_tlv_reader_next(&r, &tag, &value, &vlen), -1);
}
END_TEST

START_TEST(writer_begin_rejects_small_cap) {
  unsigned char buf[SIMULCRYPT_HDR_LEN - 1];
  simulcrypt_writer_t w;
  ck_assert_int_eq(simulcrypt_writer_begin(&w, buf, sizeof buf, 2, 1), -1);
}
END_TEST

START_TEST(writer_put_tlv_rejects_overflow) {
  unsigned char buf[SIMULCRYPT_HDR_LEN + 3]; /* room for header + 3 bytes, TLV needs 4 + value */
  simulcrypt_writer_t w;
  static const unsigned char v[] = {1, 2};
  ck_assert_int_eq(simulcrypt_writer_begin(&w, buf, sizeof buf, 2, 1), 0);
  ck_assert_int_eq(simulcrypt_writer_put_tlv(&w, 0x0001, v, sizeof v), -1);
  ck_assert_uint_eq(simulcrypt_writer_finish(&w), 0u);
}
END_TEST

START_TEST(writer_put_tlv_fits_exact_cap) {
  unsigned char buf[SIMULCRYPT_HDR_LEN + 4 + 2];
  simulcrypt_writer_t w;
  static const unsigned char v[] = {1, 2};
  ck_assert_int_eq(simulcrypt_writer_begin(&w, buf, sizeof buf, 2, 1), 0);
  ck_assert_int_eq(simulcrypt_writer_put_tlv(&w, 0x0001, v, sizeof v), 0);
  ck_assert_uint_eq(simulcrypt_writer_finish(&w), sizeof buf);
}
END_TEST

static void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

START_TEST(reader_poll_full_frame_one_write) {
  int fds[2];
  unsigned char frame[32];
  simulcrypt_writer_t w;
  static const unsigned char v[] = {0x11, 0x22, 0x33};
  size_t total;
  simulcrypt_reader_t r;
  simulcrypt_hdr_t hdr;
  const unsigned char *payload;

  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  set_nonblocking(fds[0]);

  ck_assert_int_eq(simulcrypt_writer_begin(&w, frame, sizeof frame, 3, 0x0201), 0);
  ck_assert_int_eq(simulcrypt_writer_put_tlv(&w, 0x7000, v, sizeof v), 0);
  total = simulcrypt_writer_finish(&w);

  ck_assert_int_eq((int)write(fds[1], frame, total), (int)total);

  simulcrypt_reader_init(&r);
  ck_assert_int_eq(simulcrypt_reader_poll(&r, fds[0], 1000, &hdr, &payload), 1);
  ck_assert_uint_eq(hdr.version, 3);
  ck_assert_uint_eq(hdr.type, 0x0201);
  ck_assert_uint_eq(hdr.payload_len, 4 + sizeof v);
  ck_assert_mem_eq(payload, frame + SIMULCRYPT_HDR_LEN, hdr.payload_len);

  close(fds[0]);
  close(fds[1]);
}
END_TEST

START_TEST(reader_poll_frame_split_across_writes) {
  int fds[2];
  unsigned char frame[32];
  simulcrypt_writer_t w;
  static const unsigned char v[] = {0xDE, 0xAD};
  size_t total;
  simulcrypt_reader_t r;
  simulcrypt_hdr_t hdr;
  const unsigned char *payload;

  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  set_nonblocking(fds[0]);

  ck_assert_int_eq(simulcrypt_writer_begin(&w, frame, sizeof frame, 2, 0x0011), 0);
  ck_assert_int_eq(simulcrypt_writer_put_tlv(&w, 0x8000, v, sizeof v), 0);
  total = simulcrypt_writer_finish(&w);

  simulcrypt_reader_init(&r);

  /* header only */
  ck_assert_int_eq((int)write(fds[1], frame, SIMULCRYPT_HDR_LEN), SIMULCRYPT_HDR_LEN);
  ck_assert_int_eq(simulcrypt_reader_poll(&r, fds[0], 200, &hdr, &payload), 0);

  /* remaining payload */
  ck_assert_int_eq((int)write(fds[1], frame + SIMULCRYPT_HDR_LEN, total - SIMULCRYPT_HDR_LEN), (int)(total - SIMULCRYPT_HDR_LEN));
  ck_assert_int_eq(simulcrypt_reader_poll(&r, fds[0], 1000, &hdr, &payload), 1);
  ck_assert_uint_eq(hdr.type, 0x0011);
  ck_assert_uint_eq(hdr.payload_len, 4 + sizeof v);
  ck_assert_mem_eq(payload, frame + SIMULCRYPT_HDR_LEN, hdr.payload_len);

  close(fds[0]);
  close(fds[1]);
}
END_TEST

START_TEST(reader_poll_returns_zero_on_timeout) {
  int fds[2];
  simulcrypt_reader_t r;
  simulcrypt_hdr_t hdr;
  const unsigned char *payload;

  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  set_nonblocking(fds[0]);
  simulcrypt_reader_init(&r);

  ck_assert_int_eq(simulcrypt_reader_poll(&r, fds[0], 50, &hdr, &payload), 0);

  close(fds[0]);
  close(fds[1]);
}
END_TEST

START_TEST(reader_poll_returns_error_on_peer_close) {
  int fds[2];
  simulcrypt_reader_t r;
  simulcrypt_hdr_t hdr;
  const unsigned char *payload;

  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  set_nonblocking(fds[0]);
  simulcrypt_reader_init(&r);

  close(fds[1]);
  ck_assert_int_eq(simulcrypt_reader_poll(&r, fds[0], 1000, &hdr, &payload), -1);

  close(fds[0]);
}
END_TEST

static Suite *simulcrypt_msg_suite(void) {
  Suite *s = suite_create("simulcrypt_msg");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, hdr_write_then_parse_round_trip);
  tcase_add_test(tc, hdr_write_rejects_small_cap);
  tcase_add_test(tc, hdr_parse_rejects_short_buffer);
  tcase_add_test(tc, tlv_round_trip_multiple_elements);
  tcase_add_test(tc, tlv_reader_clean_end_on_empty_payload);
  tcase_add_test(tc, tlv_reader_rejects_truncated_tlv_header);
  tcase_add_test(tc, tlv_reader_rejects_truncated_value);
  tcase_add_test(tc, writer_begin_rejects_small_cap);
  tcase_add_test(tc, writer_put_tlv_rejects_overflow);
  tcase_add_test(tc, writer_put_tlv_fits_exact_cap);
  tcase_add_test(tc, reader_poll_full_frame_one_write);
  tcase_add_test(tc, reader_poll_frame_split_across_writes);
  tcase_add_test(tc, reader_poll_returns_zero_on_timeout);
  tcase_add_test(tc, reader_poll_returns_error_on_peer_close);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(simulcrypt_msg_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
