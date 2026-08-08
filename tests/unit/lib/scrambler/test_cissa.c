/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/scrambler/cissa.h"
#include "lib/scrambler/scrambler.h"

/* control word used by all four ETSI TS 103 127 Annex B test vectors */
static const unsigned char cw[CISSA_CW_LEN] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};

static void hex_decode(const char *hex, unsigned char *out, size_t out_len) {
  size_t i;
  for (i = 0; i < out_len; i++) {
    unsigned hi, lo;
    sscanf(hex + i * 2, "%1x%1x", &hi, &lo);
    out[i] = (unsigned char)((hi << 4) | lo);
  }
}

/* Annex B test cases: full 188-byte TS packets, clear and scrambled, one
 * per adaptation field length (0, 7, 8, 9 bytes) */
typedef struct {
  const char *name;
  const char *clear_hex;
  const char *scrambled_hex;
} cissa_vector_t;

static const cissa_vector_t vectors[] = {
    {"no_adaptation_field",
     "476080115468697320697320746865207061796c6f6164207573656420666f72"
     "206372656174696e6720746865207465737420766563746f727320666f722074"
     "686520445642204950545620736372616d626c65722f6465736372616d626c65"
     "722e205468697320697320746865207061796c6f6164207573656420666f7220"
     "6372656174696e6720746865207465737420766563746f727320666f72207468"
     "6520445642204950545620736372616d626c65722f6465736372616d",
     "4760809115ce67e0cb01b53ce76054e57a4ad120a0dfa4eaaae932c6783f51ae"
     "19faee108bdb78f3113ec2b572cc208500a52ceca114126c58244df563e7a9b4"
     "e041cbc3fbfffbd83c8fbffb10e83ea38204bad702fb01a27b622c4f85aab6aa"
     "75559720d65ab844cea28cf2e1fe5e7ac19d44818919c23249f140757b5d16c0"
     "af45b25f509b9da0619712c59f0b39b06f1fbe90123f212983936a95317fcb62"
     "f4346a1b1e164840303aff838a019bf810a8e0b22f6465736372616d"},
    {"7byte_adaptation_field",
     "476080310600ffffffffff5468697320697320746865207061796c6f61642075"
     "73656420666f72206372656174696e6720746865207465737420766563746f72"
     "7320666f722074686520445642204950545620736372616d626c65722f646573"
     "6372616d626c65722e205468697320697320746865207061796c6f6164207573"
     "656420666f72206372656174696e6720746865207465737420766563746f7273"
     "20666f722074686520445642204950545620736372616d626c65722f",
     "476080b10600ffffffffff15ce67e0cb01b53ce76054e57a4ad120a0dfa4eaaa"
     "e932c6783f51ae19faee108bdb78f3113ec2b572cc208500a52ceca114126c58"
     "244df563e7a9b4e041cbc3fbfffbd83c8fbffb10e83ea38204bad702fb01a27b"
     "622c4f85aab6aa75559720d65ab844cea28cf2e1fe5e7ac19d44818919c23249"
     "f140757b5d16c0af45b25f509b9da0619712c59f0b39b06f1fbe90123f212983"
     "936a95317fcb62f4346a1b1e164840303aff838a019bf810a8e0b22f"},
    {"8byte_adaptation_field",
     "476080310700ffffffffffff5468697320697320746865207061796c6f616420"
     "7573656420666f72206372656174696e6720746865207465737420766563746f"
     "727320666f722074686520445642204950545620736372616d626c65722f6465"
     "736372616d626c65722e205468697320697320746865207061796c6f61642075"
     "73656420666f72206372656174696e6720746865207465737420766563746f72"
     "7320666f722074686520445642204950545620736372616d626c6572",
     "476080b10700ffffffffffff15ce67e0cb01b53ce76054e57a4ad120a0dfa4ea"
     "aae932c6783f51ae19faee108bdb78f3113ec2b572cc208500a52ceca114126c"
     "58244df563e7a9b4e041cbc3fbfffbd83c8fbffb10e83ea38204bad702fb01a2"
     "7b622c4f85aab6aa75559720d65ab844cea28cf2e1fe5e7ac19d44818919c232"
     "49f140757b5d16c0af45b25f509b9da0619712c59f0b39b06f1fbe90123f2129"
     "83936a95317fcb62f4346a1b1e164840303aff838a019bf810a8e0b2"},
    {"9byte_adaptation_field",
     "476080310800ffffffffffffff5468697320697320746865207061796c6f6164"
     "207573656420666f72206372656174696e672074686520746573742076656374"
     "6f727320666f722074686520445642204950545620736372616d626c65722f64"
     "65736372616d626c65722e205468697320697320746865207061796c6f616420"
     "7573656420666f72206372656174696e6720746865207465737420766563746f"
     "727320666f722074686520445642204950545620736372616d626c65",
     "476080b10800ffffffffffffff15ce67e0cb01b53ce76054e57a4ad120a0dfa4"
     "eaaae932c6783f51ae19faee108bdb78f3113ec2b572cc208500a52ceca11412"
     "6c58244df563e7a9b4e041cbc3fbfffbd83c8fbffb10e83ea38204bad702fb01"
     "a27b622c4f85aab6aa75559720d65ab844cea28cf2e1fe5e7ac19d44818919c2"
     "3249f140757b5d16c0af45b25f509b9da0619712c59f0b39b06f1fbe90123f21"
     "2983936a95317fcb62f4346a1b42204950545620736372616d626c65"},
};

START_TEST(cissa_vectors_scramble_full_packet) {
  const cissa_vector_t *v = &vectors[_i];
  unsigned char clear[188], scrambled[188], got[188];
  scrambler_t *s;

  hex_decode(v->clear_hex, clear, 188);
  hex_decode(v->scrambled_hex, scrambled, 188);
  memcpy(got, clear, 188);

  s = scrambler_new(SCRAMBLE_ALGO_CISSA);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  ck_assert_int_eq(scrambler_encrypt_packet(s, got, SCRAMBLE_PARITY_EVEN), 0);
  ck_assert_mem_eq(got, scrambled, 188);
  scrambler_free(s);
}
END_TEST

START_TEST(cissa_encrypt_block_rejects_bad_length) {
  unsigned char buf[15];
  cissa_key_t *k = cissa_key_new(cw);
  ck_assert_ptr_nonnull(k);
  ck_assert_int_eq(cissa_encrypt_block(k, buf, 0), -1);
  ck_assert_int_eq(cissa_encrypt_block(k, buf, 15), -1);
  cissa_key_free(k);
}
END_TEST

START_TEST(cissa_vectors_descramble_full_packet) {
  const cissa_vector_t *v = &vectors[_i];
  unsigned char clear[188], scrambled[188], got[188];
  scrambler_t *s;

  hex_decode(v->clear_hex, clear, 188);
  hex_decode(v->scrambled_hex, scrambled, 188);
  memcpy(got, scrambled, 188);

  s = scrambler_new(SCRAMBLE_ALGO_CISSA);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  ck_assert_int_eq(scrambler_decrypt_packet(s, got), 0);
  ck_assert_mem_eq(got, clear, 188);
  scrambler_free(s);
}
END_TEST

START_TEST(cissa_decrypt_block_rejects_bad_length) {
  unsigned char buf[15];
  cissa_key_t *k = cissa_key_new(cw);
  ck_assert_ptr_nonnull(k);
  ck_assert_int_eq(cissa_decrypt_block(k, buf, 0), -1);
  ck_assert_int_eq(cissa_decrypt_block(k, buf, 15), -1);
  cissa_key_free(k);
}
END_TEST

START_TEST(scrambler_decrypt_packet_passes_through_unscrambled) {
  unsigned char pkt[188], orig[188];
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CISSA);

  memset(pkt, 0xAA, 188);
  pkt[0] = 0x47;
  pkt[3] = 0x10; /* afc=1 payload only, scrambling control 00 (not scrambled) */
  memcpy(orig, pkt, 188);

  ck_assert_int_eq(scrambler_decrypt_packet(s, pkt), 0);
  ck_assert_mem_eq(pkt, orig, 188); /* untouched */
  scrambler_free(s);
}
END_TEST

START_TEST(scrambler_decrypt_packet_rejects_unset_key) {
  unsigned char pkt[188];
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CISSA);
  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[3] = 0x90; /* afc=1, scrambling control 10 (even) - but no key loaded */
  ck_assert_int_eq(scrambler_decrypt_packet(s, pkt), -1);
  scrambler_free(s);
}
END_TEST

START_TEST(scrambler_decrypt_packet_rejects_reserved_control_value) {
  unsigned char pkt[188];
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CISSA);
  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[3] = 0x50; /* afc=1, scrambling control 01 (reserved) */
  ck_assert_int_eq(scrambler_decrypt_packet(s, pkt), -1);
  scrambler_free(s);
}
END_TEST

START_TEST(scrambler_encrypt_packet_rejects_unset_key) {
  unsigned char pkt[188];
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CISSA);
  memset(pkt, 0, sizeof pkt);
  ck_assert_int_eq(scrambler_encrypt_packet(s, pkt, SCRAMBLE_PARITY_EVEN), -1);
  scrambler_free(s);
}
END_TEST

/* residual payload under one 16-byte block (large adaptation field leaves 11 bytes) must stay
 * clear AND report as clear (transport_scrambling_control 00) - not marked scrambled while the
 * bytes underneath never actually got encrypted */
START_TEST(scrambler_encrypt_packet_leaves_control_bits_clear_when_payload_too_small) {
  unsigned char pkt[188], orig[188];
  scrambler_t *s;

  memset(pkt, 0xAA, 188);
  pkt[0] = 0x47;
  pkt[1] = 0x00;
  pkt[2] = 0x00;
  pkt[3] = 0x30; /* afc=3: adaptation field + payload, scrambling control 00 */
  pkt[4] = 172;  /* adaptation_field_length: leaves 188-4-1-172 = 11 payload bytes */
  pkt[5] = 0x00; /* adaptation field flags: no PCR/OPCR/splice/private/ext */
  memcpy(orig, pkt, 188);

  s = scrambler_new(SCRAMBLE_ALGO_CISSA);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  ck_assert_int_eq(scrambler_encrypt_packet(s, pkt, SCRAMBLE_PARITY_EVEN), 0);
  ck_assert_int_eq((pkt[3] >> 6) & 0x3, 0); /* still clear, not marked scrambled */
  ck_assert_mem_eq(pkt, orig, 188);         /* and genuinely untouched */
  scrambler_free(s);
}
END_TEST

/* one shared key across all vectors: proves ctx reuse still resets IV per packet */
START_TEST(cissa_reuses_key_schedule_correctly_across_packets) {
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CISSA);
  size_t i;

  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  for (i = 0; i < sizeof vectors / sizeof vectors[0]; i++) {
    unsigned char clear[188], scrambled[188], got[188];
    hex_decode(vectors[i].clear_hex, clear, 188);
    hex_decode(vectors[i].scrambled_hex, scrambled, 188);
    memcpy(got, clear, 188);
    ck_assert_int_eq(scrambler_encrypt_packet(s, got, SCRAMBLE_PARITY_EVEN), 0);
    ck_assert_mem_eq(got, scrambled, 188);
  }
  scrambler_free(s);
}
END_TEST

/* exercises the enc->dec direction switch path (full re-init, not just IV reset) */
START_TEST(cissa_key_round_trips_after_direction_switch) {
  const cissa_vector_t *v = &vectors[0];
  unsigned char clear[188], scrambled[188], buf[188];
  cissa_key_t *k = cissa_key_new(cw);

  ck_assert_ptr_nonnull(k);
  hex_decode(v->clear_hex, clear, 188);
  hex_decode(v->scrambled_hex, scrambled, 188);

  memcpy(buf, clear + 4, 184);
  ck_assert_int_eq(cissa_encrypt_block(k, buf, 176), 0);
  ck_assert_mem_eq(buf, scrambled + 4, 176);

  memcpy(buf, scrambled + 4, 184);
  ck_assert_int_eq(cissa_decrypt_block(k, buf, 176), 0);
  ck_assert_mem_eq(buf, clear + 4, 176);

  cissa_key_free(k);
}
END_TEST

static unsigned char captured[188];
static unsigned captured_n;

static void capture_emit(void *ctx, const unsigned char pkt[188]) {
  (void)ctx;
  memcpy(captured, pkt, 188);
  captured_n++;
}

/* CISSA has no batching backend: *_queued must behave exactly like the
 * non-queued calls, emitting inline before returning */
START_TEST(cissa_encrypt_packet_queued_emits_immediately) {
  const cissa_vector_t *v = &vectors[0];
  unsigned char clear[188], scrambled[188], pkt[188];
  scrambler_t *s;

  hex_decode(v->clear_hex, clear, 188);
  hex_decode(v->scrambled_hex, scrambled, 188);
  memcpy(pkt, clear, 188);

  s = scrambler_new(SCRAMBLE_ALGO_CISSA);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  captured_n = 0;
  ck_assert_int_eq(scrambler_encrypt_packet_queued(s, pkt, SCRAMBLE_PARITY_EVEN, capture_emit, NULL), 0);
  ck_assert_uint_eq(captured_n, 1);
  ck_assert_mem_eq(captured, scrambled, 188);
  scrambler_free(s);
}
END_TEST

START_TEST(cissa_decrypt_packet_queued_emits_immediately) {
  const cissa_vector_t *v = &vectors[0];
  unsigned char clear[188], scrambled[188], pkt[188];
  scrambler_t *s;

  hex_decode(v->clear_hex, clear, 188);
  hex_decode(v->scrambled_hex, scrambled, 188);
  memcpy(pkt, scrambled, 188);

  s = scrambler_new(SCRAMBLE_ALGO_CISSA);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  captured_n = 0;
  ck_assert_int_eq(scrambler_decrypt_packet_queued(s, pkt, capture_emit, NULL), 0);
  ck_assert_uint_eq(captured_n, 1);
  ck_assert_mem_eq(captured, clear, 188);
  scrambler_free(s);
}
END_TEST

static Suite *cissa_suite(void) {
  Suite *s = suite_create("cissa");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, cissa_vectors_scramble_full_packet, 0, sizeof vectors / sizeof vectors[0]);
  tcase_add_loop_test(tc, cissa_vectors_descramble_full_packet, 0, sizeof vectors / sizeof vectors[0]);
  tcase_add_test(tc, cissa_encrypt_block_rejects_bad_length);
  tcase_add_test(tc, cissa_decrypt_block_rejects_bad_length);
  tcase_add_test(tc, cissa_reuses_key_schedule_correctly_across_packets);
  tcase_add_test(tc, cissa_key_round_trips_after_direction_switch);
  tcase_add_test(tc, scrambler_encrypt_packet_rejects_unset_key);
  tcase_add_test(tc, scrambler_encrypt_packet_leaves_control_bits_clear_when_payload_too_small);
  tcase_add_test(tc, scrambler_decrypt_packet_passes_through_unscrambled);
  tcase_add_test(tc, scrambler_decrypt_packet_rejects_unset_key);
  tcase_add_test(tc, scrambler_decrypt_packet_rejects_reserved_control_value);
  tcase_add_test(tc, cissa_encrypt_packet_queued_emits_immediately);
  tcase_add_test(tc, cissa_decrypt_packet_queued_emits_immediately);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(cissa_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
