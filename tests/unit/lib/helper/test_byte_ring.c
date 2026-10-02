/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/byte_ring.h"

#define RING_CAP 8u

static void ring_open(byte_ring_t *r, uint32_t cap) {
  memset(r, 0, sizeof *r);
  byte_ring_reset(r, cap);
  ck_assert_ptr_nonnull(r->buf);
}

START_TEST(write_then_read_round_trips) {
  byte_ring_t r;
  static const uint8_t in[] = {1, 2, 3, 4, 5};
  uint8_t out[8];

  ring_open(&r, RING_CAP);
  ck_assert_int_eq(byte_ring_write(&r, in, sizeof in), 1);
  ck_assert_uint_eq(byte_ring_read(&r, out, sizeof out), sizeof in);
  ck_assert_mem_eq(out, in, sizeof in);
  ck_assert_uint_eq(byte_ring_read(&r, out, sizeof out), 0u);
  byte_ring_free(&r);
}
END_TEST

START_TEST(empty_write_is_a_noop_success_even_without_buffer) {
  byte_ring_t r;

  memset(&r, 0, sizeof r);
  ck_assert_int_eq(byte_ring_write(&r, (const uint8_t *)"", 0), 1);
  ring_open(&r, RING_CAP);
  ck_assert_int_eq(byte_ring_write(&r, (const uint8_t *)"", 0), 1);
  byte_ring_free(&r);
}
END_TEST

START_TEST(uninitialized_ring_rejects_everything) {
  byte_ring_t r;
  uint8_t out[4];
  size_t len = 99;

  memset(&r, 0, sizeof r);
  ck_assert_int_eq(byte_ring_write(&r, (const uint8_t *)"abcd", 4), 0);
  ck_assert_uint_eq(byte_ring_read(&r, out, sizeof out), 0u);
  ck_assert_ptr_null(byte_ring_peek(&r, &len));
  ck_assert_uint_eq(len, 0u);
}
END_TEST

START_TEST(write_beyond_free_space_is_rejected_without_side_effects) {
  byte_ring_t r;
  static const uint8_t full[RING_CAP] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint8_t out[RING_CAP + 4];

  ring_open(&r, RING_CAP);
  ck_assert_int_eq(byte_ring_write(&r, full, RING_CAP + 1), 0);
  ck_assert_uint_eq(byte_ring_read(&r, out, sizeof out), 0u);

  ck_assert_int_eq(byte_ring_write(&r, full, RING_CAP), 1);
  ck_assert_int_eq(byte_ring_write(&r, full, 1), 0);
  ck_assert_uint_eq(byte_ring_read(&r, out, sizeof out), RING_CAP);
  ck_assert_mem_eq(out, full, RING_CAP);
  byte_ring_free(&r);
}
END_TEST

START_TEST(wraparound_splits_reads_into_contiguous_runs) {
  byte_ring_t r;
  static const uint8_t first[6] = {1, 2, 3, 4, 5, 6};
  static const uint8_t second[5] = {7, 8, 9, 10, 11};
  uint8_t out[RING_CAP];

  ring_open(&r, RING_CAP);
  ck_assert_int_eq(byte_ring_write(&r, first, sizeof first), 1);
  ck_assert_uint_eq(byte_ring_read(&r, out, sizeof first), sizeof first);
  ck_assert_int_eq(byte_ring_write(&r, second, sizeof second), 1);

  ck_assert_uint_eq(byte_ring_read(&r, out, sizeof out), 2u);
  ck_assert_mem_eq(out, second, 2);
  ck_assert_uint_eq(byte_ring_read(&r, out, sizeof out), 3u);
  ck_assert_mem_eq(out, second + 2, 3);
  ck_assert_uint_eq(byte_ring_read(&r, out, sizeof out), 0u);
  byte_ring_free(&r);
}
END_TEST

START_TEST(read_honours_maxlen) {
  byte_ring_t r;
  static const uint8_t in[] = {1, 2, 3, 4};
  uint8_t out[4];

  ring_open(&r, RING_CAP);
  ck_assert_int_eq(byte_ring_write(&r, in, sizeof in), 1);
  ck_assert_uint_eq(byte_ring_read(&r, out, 3), 3u);
  ck_assert_mem_eq(out, in, 3);
  ck_assert_uint_eq(byte_ring_read(&r, out, 3), 1u);
  ck_assert_uint_eq(out[0], 4u);
  byte_ring_free(&r);
}
END_TEST

START_TEST(peek_and_advance_walk_the_same_data_zero_copy) {
  byte_ring_t r;
  static const uint8_t first[6] = {1, 2, 3, 4, 5, 6};
  static const uint8_t second[4] = {7, 8, 9, 10};
  uint8_t skip[6];
  const uint8_t *p;
  size_t len = 0;

  ring_open(&r, RING_CAP);
  ck_assert_ptr_null(byte_ring_peek(&r, &len));
  ck_assert_uint_eq(len, 0u);

  ck_assert_int_eq(byte_ring_write(&r, first, sizeof first), 1);
  ck_assert_uint_eq(byte_ring_read(&r, skip, sizeof skip), sizeof skip);
  ck_assert_int_eq(byte_ring_write(&r, second, sizeof second), 1);

  p = byte_ring_peek(&r, &len);
  ck_assert_ptr_nonnull(p);
  ck_assert_uint_eq(len, 2u);
  ck_assert_mem_eq(p, second, 2);
  byte_ring_advance(&r, len);

  p = byte_ring_peek(&r, &len);
  ck_assert_ptr_nonnull(p);
  ck_assert_uint_eq(len, 2u);
  ck_assert_mem_eq(p, second + 2, 2);
  byte_ring_advance(&r, len);

  ck_assert_ptr_null(byte_ring_peek(&r, &len));
  ck_assert_uint_eq(len, 0u);
  byte_ring_free(&r);
}
END_TEST

START_TEST(positions_survive_32_bit_counter_wrap) {
  byte_ring_t r;
  static const uint8_t in[RING_CAP] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint8_t out[RING_CAP];

  ring_open(&r, RING_CAP);
  atomic_store(&r.wpos, 0xFFFFFFFDu);
  atomic_store(&r.rpos, 0xFFFFFFFDu);
  ck_assert_int_eq(byte_ring_write(&r, in, 5), 1);
  ck_assert_uint_eq(atomic_load(&r.wpos), 2u);
  ck_assert_uint_eq(byte_ring_read(&r, out, sizeof out), 3u);
  ck_assert_mem_eq(out, in, 3);
  ck_assert_uint_eq(byte_ring_read(&r, out, sizeof out), 2u);
  ck_assert_mem_eq(out, in + 3, 2);
  ck_assert_int_eq(byte_ring_write(&r, in, RING_CAP), 1);
  ck_assert_int_eq(byte_ring_write(&r, in, 1), 0);
  byte_ring_free(&r);
}
END_TEST

START_TEST(reset_keeps_the_buffer_and_clears_positions) {
  byte_ring_t r;
  uint8_t *before;
  uint8_t out[4];

  ring_open(&r, RING_CAP);
  before = r.buf;
  ck_assert_int_eq(byte_ring_write(&r, (const uint8_t *)"abcd", 4), 1);
  byte_ring_reset(&r, RING_CAP);
  ck_assert_ptr_eq(r.buf, before);
  ck_assert_uint_eq(byte_ring_read(&r, out, sizeof out), 0u);
  byte_ring_free(&r);
}
END_TEST

START_TEST(free_is_idempotent) {
  byte_ring_t r;

  ring_open(&r, RING_CAP);
  byte_ring_free(&r);
  ck_assert_ptr_null(r.buf);
  byte_ring_free(&r);
  ck_assert_ptr_null(r.buf);
}
END_TEST

#define STREAM_BYTES 300000u
#define STREAM_RING_CAP 1024u

typedef struct {
  byte_ring_t ring;
  unsigned errors;
} stream_t;

static void *producer_main(void *arg) {
  stream_t *s = arg;
  uint8_t chunk[37];
  uint32_t sent = 0;

  while (sent < STREAM_BYTES) {
    size_t n = sizeof chunk;

    if (n > STREAM_BYTES - sent) n = STREAM_BYTES - sent;
    for (size_t i = 0; i < n; i++) chunk[i] = (uint8_t)((sent + i) * 31u);
    if (byte_ring_write(&s->ring, chunk, n)) sent += (uint32_t)n;
    else sched_yield();
  }
  return NULL;
}

START_TEST(single_producer_single_consumer_stream_is_ordered) {
  stream_t s;
  pthread_t th;
  uint8_t out[97];
  uint32_t got = 0;

  memset(&s, 0, sizeof s);
  byte_ring_reset(&s.ring, STREAM_RING_CAP);
  ck_assert_ptr_nonnull(s.ring.buf);
  ck_assert_int_eq(pthread_create(&th, NULL, producer_main, &s), 0);
  while (got < STREAM_BYTES) {
    size_t n = byte_ring_read(&s.ring, out, sizeof out);

    if (!n) {
      sched_yield();
      continue;
    }
    for (size_t i = 0; i < n; i++)
      if (out[i] != (uint8_t)((got + i) * 31u)) s.errors++;
    got += (uint32_t)n;
  }
  pthread_join(th, NULL);
  ck_assert_uint_eq(s.errors, 0u);
  byte_ring_free(&s.ring);
}
END_TEST

static Suite *byte_ring_suite(void) {
  Suite *s = suite_create("byte_ring");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, write_then_read_round_trips);
  tcase_add_test(tc, empty_write_is_a_noop_success_even_without_buffer);
  tcase_add_test(tc, uninitialized_ring_rejects_everything);
  tcase_add_test(tc, write_beyond_free_space_is_rejected_without_side_effects);
  tcase_add_test(tc, wraparound_splits_reads_into_contiguous_runs);
  tcase_add_test(tc, read_honours_maxlen);
  tcase_add_test(tc, peek_and_advance_walk_the_same_data_zero_copy);
  tcase_add_test(tc, positions_survive_32_bit_counter_wrap);
  tcase_add_test(tc, reset_keeps_the_buffer_and_clears_positions);
  tcase_add_test(tc, free_is_idempotent);
  tcase_add_test(tc, single_producer_single_consumer_stream_is_ordered);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(byte_ring_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
