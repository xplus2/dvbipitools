/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/ws/ws_frame.h"

/* builds a masked client->server frame, mirrors what a real client sends */
static uint8_t *build_client_frame(int fin, int opcode, const void *payload, size_t payload_len, uint32_t mask_key,
                                    size_t *out_len) {
  size_t hdr, i;
  uint8_t *out;
  const uint8_t *p = payload;
  uint8_t mask[4];

  mask[0] = (uint8_t)(mask_key >> 24);
  mask[1] = (uint8_t)(mask_key >> 16);
  mask[2] = (uint8_t)(mask_key >> 8);
  mask[3] = (uint8_t)mask_key;

  if (payload_len <= 125)
    hdr = 2;
  else if (payload_len <= 0xffff)
    hdr = 4;
  else
    hdr = 10;

  out = malloc(hdr + 4 + payload_len);
  ck_assert_ptr_nonnull(out);

  out[0] = (uint8_t)((fin ? 0x80 : 0x00) | (opcode & 0x0f));
  if (payload_len <= 125) {
    out[1] = (uint8_t)(0x80 | payload_len);
  } else if (payload_len <= 0xffff) {
    out[1] = 0x80 | 126;
    out[2] = (uint8_t)(payload_len >> 8);
    out[3] = (uint8_t)payload_len;
  } else {
    out[1] = 0x80 | 127;
    for (i = 0; i < 8; i++)
      out[2 + i] = (uint8_t)((uint64_t)payload_len >> (56 - 8 * i));
  }
  memcpy(out + hdr, mask, 4);
  for (i = 0; i < payload_len; i++)
    out[hdr + 4 + i] = p[i] ^ mask[i % 4];

  *out_len = hdr + 4 + payload_len;
  return out;
}

START_TEST(build_small_payload_has_two_byte_header) {
  size_t out_len;
  uint8_t *out = ws_build_frame(WS_OP_TEXT, "hi", 2, &out_len);
  ck_assert_ptr_nonnull(out);
  ck_assert_uint_eq(out_len, 4);
  ck_assert_uint_eq(out[0], 0x80 | WS_OP_TEXT);
  ck_assert_uint_eq(out[1], 2);
  ck_assert_mem_eq(out + 2, "hi", 2);
  free(out);
}
END_TEST

START_TEST(build_zero_length_payload) {
  size_t out_len;
  uint8_t *out = ws_build_frame(WS_OP_PING, NULL, 0, &out_len);
  ck_assert_ptr_nonnull(out);
  ck_assert_uint_eq(out_len, 2);
  ck_assert_uint_eq(out[1], 0);
  free(out);
}
END_TEST

START_TEST(build_medium_payload_uses_16bit_length) {
  size_t out_len;
  size_t plen = 200;
  char *payload = malloc(plen);
  memset(payload, 'a', plen);
  uint8_t *out = ws_build_frame(WS_OP_BINARY, payload, plen, &out_len);
  ck_assert_ptr_nonnull(out);
  ck_assert_uint_eq(out_len, 4 + plen);
  ck_assert_uint_eq(out[1], 126);
  ck_assert_uint_eq((unsigned)((out[2] << 8) | out[3]), plen);
  ck_assert_mem_eq(out + 4, payload, plen);
  free(out);
  free(payload);
}
END_TEST

START_TEST(build_large_payload_uses_64bit_length) {
  size_t out_len;
  size_t plen = 70000;
  char *payload = malloc(plen);
  memset(payload, 'z', plen);
  uint8_t *out = ws_build_frame(WS_OP_BINARY, payload, plen, &out_len);
  ck_assert_ptr_nonnull(out);
  ck_assert_uint_eq(out_len, 10 + plen);
  ck_assert_uint_eq(out[1], 127);
  {
    uint64_t len = 0;
    size_t i;
    for (i = 0; i < 8; i++)
      len = (len << 8) | out[2 + i];
    ck_assert_uint_eq(len, plen);
  }
  ck_assert_mem_eq(out + 10, payload, plen);
  free(out);
  free(payload);
}
END_TEST

START_TEST(unfragmented_text_round_trips) {
  ws_parser_t p;
  size_t flen;
  uint8_t *frame = build_client_frame(1, WS_OP_TEXT, "hello", 5, 0xdeadbeef, &flen);
  int opcode;
  const uint8_t *payload;
  size_t payload_len;

  ws_parser_init(&p);
  ck_assert_int_eq(ws_parser_feed(&p, frame, flen), 0);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 1);
  ck_assert_int_eq(opcode, WS_OP_TEXT);
  ck_assert_uint_eq(payload_len, 5);
  ck_assert_mem_eq(payload, "hello", 5);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 0);

  free(frame);
  ws_parser_free(&p);
}
END_TEST

START_TEST(unfragmented_binary_round_trips) {
  ws_parser_t p;
  size_t flen;
  uint8_t payload_in[4] = {0x00, 0xff, 0x10, 0x20};
  uint8_t *frame = build_client_frame(1, WS_OP_BINARY, payload_in, sizeof payload_in, 0x1234, &flen);
  int opcode;
  const uint8_t *payload;
  size_t payload_len;

  ws_parser_init(&p);
  ws_parser_feed(&p, frame, flen);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 1);
  ck_assert_int_eq(opcode, WS_OP_BINARY);
  ck_assert_mem_eq(payload, payload_in, sizeof payload_in);

  free(frame);
  ws_parser_free(&p);
}
END_TEST

START_TEST(medium_length_payload_round_trips) {
  ws_parser_t p;
  size_t flen, plen = 300;
  char *payload_in = malloc(plen);
  uint8_t *frame;
  int opcode;
  const uint8_t *payload;
  size_t payload_len;

  memset(payload_in, 'x', plen);
  frame = build_client_frame(1, WS_OP_BINARY, payload_in, plen, 0x99887766, &flen);

  ws_parser_init(&p);
  ws_parser_feed(&p, frame, flen);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 1);
  ck_assert_uint_eq(payload_len, plen);
  ck_assert_mem_eq(payload, payload_in, plen);

  free(frame);
  free(payload_in);
  ws_parser_free(&p);
}
END_TEST

START_TEST(fragmented_message_reassembles) {
  ws_parser_t p;
  size_t f1len, f2len, f3len;
  uint8_t *f1 = build_client_frame(0, WS_OP_TEXT, "foo", 3, 0x1, &f1len);
  uint8_t *f2 = build_client_frame(0, WS_OP_CONTINUATION, "bar", 3, 0x2, &f2len);
  uint8_t *f3 = build_client_frame(1, WS_OP_CONTINUATION, "baz", 3, 0x3, &f3len);
  int opcode;
  const uint8_t *payload;
  size_t payload_len;

  ws_parser_init(&p);
  ws_parser_feed(&p, f1, f1len);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 0);
  ws_parser_feed(&p, f2, f2len);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 0);
  ws_parser_feed(&p, f3, f3len);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 1);
  ck_assert_int_eq(opcode, WS_OP_TEXT);
  ck_assert_uint_eq(payload_len, 9);
  ck_assert_mem_eq(payload, "foobarbaz", 9);

  free(f1);
  free(f2);
  free(f3);
  ws_parser_free(&p);
}
END_TEST

START_TEST(two_frames_in_one_feed_drain_in_order) {
  ws_parser_t p;
  size_t f1len, f2len;
  uint8_t *f1 = build_client_frame(1, WS_OP_TEXT, "one", 3, 0x11, &f1len);
  uint8_t *f2 = build_client_frame(1, WS_OP_TEXT, "two", 3, 0x22, &f2len);
  uint8_t *both = malloc(f1len + f2len);
  int opcode;
  const uint8_t *payload;
  size_t payload_len;

  memcpy(both, f1, f1len);
  memcpy(both + f1len, f2, f2len);

  ws_parser_init(&p);
  ws_parser_feed(&p, both, f1len + f2len);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 1);
  ck_assert_mem_eq(payload, "one", 3);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 1);
  ck_assert_mem_eq(payload, "two", 3);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 0);

  free(f1);
  free(f2);
  free(both);
  ws_parser_free(&p);
}
END_TEST

START_TEST(partial_frame_waits_for_rest) {
  ws_parser_t p;
  size_t flen;
  uint8_t *frame = build_client_frame(1, WS_OP_TEXT, "hello world", 11, 0x42, &flen);
  int opcode;
  const uint8_t *payload;
  size_t payload_len;

  ws_parser_init(&p);
  ws_parser_feed(&p, frame, flen - 3);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 0);
  ws_parser_feed(&p, frame + flen - 3, 3);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 1);
  ck_assert_mem_eq(payload, "hello world", 11);

  free(frame);
  ws_parser_free(&p);
}
END_TEST

START_TEST(unmasked_frame_is_protocol_error) {
  ws_parser_t p;
  uint8_t frame[2] = {0x81, 0x02}; /* FIN|TEXT, len=2, mask bit clear */
  int opcode;
  const uint8_t *payload;
  size_t payload_len;

  ws_parser_init(&p);
  ws_parser_feed(&p, frame, sizeof frame);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), -1);
  ws_parser_free(&p);
}
END_TEST

START_TEST(continuation_without_start_is_protocol_error) {
  ws_parser_t p;
  size_t flen;
  uint8_t *frame = build_client_frame(1, WS_OP_CONTINUATION, "x", 1, 0x7, &flen);
  int opcode;
  const uint8_t *payload;
  size_t payload_len;

  ws_parser_init(&p);
  ws_parser_feed(&p, frame, flen);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), -1);

  free(frame);
  ws_parser_free(&p);
}
END_TEST

START_TEST(unknown_opcode_is_protocol_error) {
  ws_parser_t p;
  size_t flen;
  uint8_t *frame = build_client_frame(1, 0x3, "x", 1, 0x7, &flen); /* 0x3 reserved */
  int opcode;
  const uint8_t *payload;
  size_t payload_len;

  ws_parser_init(&p);
  ws_parser_feed(&p, frame, flen);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), -1);

  free(frame);
  ws_parser_free(&p);
}
END_TEST

START_TEST(control_frame_at_125_bytes_ok) {
  ws_parser_t p;
  size_t flen;
  char payload[125];
  uint8_t *frame;
  int opcode;
  const uint8_t *payload_out;
  size_t payload_len;

  memset(payload, 'q', sizeof payload);
  frame = build_client_frame(1, WS_OP_PING, payload, sizeof payload, 0x55, &flen);

  ws_parser_init(&p);
  ws_parser_feed(&p, frame, flen);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload_out, &payload_len), 1);
  ck_assert_int_eq(opcode, WS_OP_PING);
  ck_assert_uint_eq(payload_len, 125);

  free(frame);
  ws_parser_free(&p);
}
END_TEST

START_TEST(control_frame_over_125_bytes_is_protocol_error) {
  ws_parser_t p;
  size_t flen;
  char payload[126];
  uint8_t *frame;
  int opcode;
  const uint8_t *payload_out;
  size_t payload_len;

  memset(payload, 'q', sizeof payload);
  frame = build_client_frame(1, WS_OP_PING, payload, sizeof payload, 0x55, &flen);

  ws_parser_init(&p);
  ws_parser_feed(&p, frame, flen);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload_out, &payload_len), -1);

  free(frame);
  ws_parser_free(&p);
}
END_TEST

START_TEST(fragmented_control_frame_is_protocol_error) {
  ws_parser_t p;
  size_t flen;
  uint8_t *frame = build_client_frame(0, WS_OP_PING, "x", 1, 0x9, &flen); /* fin=0 on a control frame */
  int opcode;
  const uint8_t *payload;
  size_t payload_len;

  ws_parser_init(&p);
  ws_parser_feed(&p, frame, flen);
  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), -1);

  free(frame);
  ws_parser_free(&p);
}
END_TEST

START_TEST(ping_interleaved_between_fragments) {
  ws_parser_t p;
  size_t f1len, pingflen, f2len;
  uint8_t *f1 = build_client_frame(0, WS_OP_TEXT, "AB", 2, 0xa1, &f1len);
  uint8_t *pingf = build_client_frame(1, WS_OP_PING, "hi", 2, 0xa2, &pingflen);
  uint8_t *f2 = build_client_frame(1, WS_OP_CONTINUATION, "CD", 2, 0xa3, &f2len);
  int opcode;
  const uint8_t *payload;
  size_t payload_len;

  ws_parser_init(&p);
  ws_parser_feed(&p, f1, f1len);
  ws_parser_feed(&p, pingf, pingflen);
  ws_parser_feed(&p, f2, f2len);

  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 1);
  ck_assert_int_eq(opcode, WS_OP_PING);
  ck_assert_mem_eq(payload, "hi", 2);

  ck_assert_int_eq(ws_parser_next(&p, &opcode, &payload, &payload_len), 1);
  ck_assert_int_eq(opcode, WS_OP_TEXT);
  ck_assert_uint_eq(payload_len, 4);
  ck_assert_mem_eq(payload, "ABCD", 4);

  free(f1);
  free(pingf);
  free(f2);
  ws_parser_free(&p);
}
END_TEST

static Suite *ws_frame_suite(void) {
  Suite *s = suite_create("dipixy_ws_frame");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, build_small_payload_has_two_byte_header);
  tcase_add_test(tc, build_zero_length_payload);
  tcase_add_test(tc, build_medium_payload_uses_16bit_length);
  tcase_add_test(tc, build_large_payload_uses_64bit_length);
  tcase_add_test(tc, unfragmented_text_round_trips);
  tcase_add_test(tc, unfragmented_binary_round_trips);
  tcase_add_test(tc, medium_length_payload_round_trips);
  tcase_add_test(tc, fragmented_message_reassembles);
  tcase_add_test(tc, two_frames_in_one_feed_drain_in_order);
  tcase_add_test(tc, partial_frame_waits_for_rest);
  tcase_add_test(tc, unmasked_frame_is_protocol_error);
  tcase_add_test(tc, continuation_without_start_is_protocol_error);
  tcase_add_test(tc, unknown_opcode_is_protocol_error);
  tcase_add_test(tc, control_frame_at_125_bytes_ok);
  tcase_add_test(tc, control_frame_over_125_bytes_is_protocol_error);
  tcase_add_test(tc, fragmented_control_frame_is_protocol_error);
  tcase_add_test(tc, ping_interleaved_between_fragments);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ws_frame_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
