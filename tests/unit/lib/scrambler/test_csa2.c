/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include <dvbcsa/dvbcsa.h>

#include "lib/scrambler/csa2.h"
#include "lib/scrambler/scrambler.h"

/* round-trip CSA2 correctness against libdvbcsa = strongest check available */
static const unsigned char cw[CSA2_CW_LEN] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

START_TEST(csa2_encrypt_block_round_trips_with_library_decrypt) {
  unsigned char plain[64], buf[64];
  struct dvbcsa_key_s *dk;
  csa2_key_t *k;

  for (size_t i = 0; i < sizeof plain; i++)
    plain[i] = (unsigned char)i;
  memcpy(buf, plain, sizeof buf);

  k = csa2_key_new(cw);
  ck_assert_ptr_nonnull(k);
  csa2_encrypt_block(k, buf, sizeof buf);
  ck_assert_mem_ne(buf, plain, sizeof buf);
  csa2_key_free(k);

  dk = dvbcsa_key_alloc();
  ck_assert_ptr_nonnull(dk);
  dvbcsa_key_set(cw, dk);
  dvbcsa_decrypt(dk, buf, sizeof buf);
  ck_assert_mem_eq(buf, plain, sizeof buf);
  dvbcsa_key_free(dk);
}
END_TEST

START_TEST(scrambler_csa2_packet_rounds_to_8byte_blocks_and_round_trips) {
  unsigned char clear[188], pkt[188];
  struct dvbcsa_key_s *dk;
  scrambler_t *s;

  /* no adaptation field: header 4 bytes, 184-byte payload (multiple of 8 already, no residual) */
  clear[0] = 0x47;
  clear[1] = 0x60;
  clear[2] = 0x80;
  clear[3] = 0x11; /* AFC=01 payload only, scrambling_control=00 */
  for (size_t i = 4; i < 188; i++)
    clear[i] = (unsigned char)(i * 7);
  memcpy(pkt, clear, sizeof pkt);

  s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_ODD, cw, sizeof cw, NULL, NULL), 0);
  ck_assert_int_eq(scrambler_encrypt_packet(s, pkt, SCRAMBLE_PARITY_ODD), 0);
  scrambler_free(s);

  /* header untouched except scrambling_control bits (top 2 of byte 3, now '11' odd) */
  ck_assert_uint_eq(pkt[0], clear[0]);
  ck_assert_uint_eq(pkt[1], clear[1]);
  ck_assert_uint_eq(pkt[2], clear[2]);
  ck_assert_uint_eq(pkt[3], (unsigned char)((clear[3] & 0x3F) | 0xC0));
  ck_assert_mem_ne(pkt + 4, clear + 4, 184); /* fully scrambled, 184 % 8 == 0 */

  dk = dvbcsa_key_alloc();
  ck_assert_ptr_nonnull(dk);
  dvbcsa_key_set(cw, dk);
  dvbcsa_decrypt(dk, pkt + 4, 184);
  dvbcsa_key_free(dk);
  ck_assert_mem_eq(pkt + 4, clear + 4, 184);
}
END_TEST

START_TEST(scrambler_csa2_packet_encrypts_full_payload_including_residual) {
  unsigned char clear[188], pkt[188];
  struct dvbcsa_key_s *dk;
  scrambler_t *s;
  size_t i;

  /* 5-byte adaptation field (1 length byte + 4): payload_off = 4+5 = 9, payload_size = 179,
     179 % 8 == 3 - libdvbcsa's residue termination stage still scrambles the full length,
     no trailing bytes left in clear (unlike CISSA's whole-block-only encrypt) */
  clear[0] = 0x47;
  clear[1] = 0x60;
  clear[2] = 0x80;
  clear[3] = 0x31; /* AFC=11 AF+payload */
  clear[4] = 0x04; /* AF length byte: 4 bytes follow */
  clear[5] = clear[6] = clear[7] = clear[8] = 0xFF;
  for (i = 9; i < 188; i++)
    clear[i] = (unsigned char)(i * 3);
  memcpy(pkt, clear, sizeof pkt);

  s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  ck_assert_int_eq(scrambler_encrypt_packet(s, pkt, SCRAMBLE_PARITY_EVEN), 0);
  scrambler_free(s);

  ck_assert_mem_eq(pkt, clear, 3);
  ck_assert_uint_eq(pkt[3], (unsigned char)((clear[3] & 0x3F) | 0x80));
  ck_assert_mem_eq(pkt + 4, clear + 4, 5);
  ck_assert_mem_ne(pkt + 9, clear + 9, 179);

  dk = dvbcsa_key_alloc();
  ck_assert_ptr_nonnull(dk);
  dvbcsa_key_set(cw, dk);
  dvbcsa_decrypt(dk, pkt + 9, 179);
  dvbcsa_key_free(dk);
  ck_assert_mem_eq(pkt + 9, clear + 9, 179);
}
END_TEST

START_TEST(scrambler_set_key_rejects_wrong_cw_len_for_csa2) {
  unsigned char cw16[16] = {0};
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw16, sizeof cw16, NULL, NULL), -1);
  scrambler_free(s);
}
END_TEST

START_TEST(csa2_decrypt_block_round_trips_with_library_encrypt) {
  unsigned char plain[64], buf[64];
  struct dvbcsa_key_s *dk;
  csa2_key_t *k;
  size_t i;

  for (i = 0; i < sizeof plain; i++)
    plain[i] = (unsigned char)(i * 5);
  memcpy(buf, plain, sizeof buf);

  dk = dvbcsa_key_alloc();
  ck_assert_ptr_nonnull(dk);
  dvbcsa_key_set(cw, dk);
  dvbcsa_encrypt(dk, buf, sizeof buf);
  dvbcsa_key_free(dk);
  ck_assert_mem_ne(buf, plain, sizeof buf);

  k = csa2_key_new(cw);
  ck_assert_ptr_nonnull(k);
  csa2_decrypt_block(k, buf, sizeof buf);
  csa2_key_free(k);
  ck_assert_mem_eq(buf, plain, sizeof buf);
}
END_TEST

START_TEST(scrambler_csa2_packet_descrambles_full_payload_round_trip) {
  unsigned char clear[188], pkt[188];
  scrambler_t *senc, *sdec;
  size_t i;

  /* same 5-byte adaptation field / non-8-aligned tail shape as the encrypt-side test */
  clear[0] = 0x47;
  clear[1] = 0x60;
  clear[2] = 0x80;
  clear[3] = 0x31;
  clear[4] = 0x04;
  clear[5] = clear[6] = clear[7] = clear[8] = 0xFF;
  for (i = 9; i < 188; i++)
    clear[i] = (unsigned char)(i * 3);
  memcpy(pkt, clear, sizeof pkt);

  senc = scrambler_new(SCRAMBLE_ALGO_CSA2);
  ck_assert_ptr_nonnull(senc);
  ck_assert_int_eq(scrambler_set_key(senc, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  ck_assert_int_eq(scrambler_encrypt_packet(senc, pkt, SCRAMBLE_PARITY_EVEN), 0);
  scrambler_free(senc);
  ck_assert_uint_eq((pkt[3] >> 6) & 0x3, 2); /* even */

  sdec = scrambler_new(SCRAMBLE_ALGO_CSA2);
  ck_assert_ptr_nonnull(sdec);
  ck_assert_int_eq(scrambler_set_key(sdec, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  ck_assert_int_eq(scrambler_decrypt_packet(sdec, pkt), 0);
  scrambler_free(sdec);

  ck_assert_uint_eq((pkt[3] >> 6) & 0x3, 0); /* cleared */
  ck_assert_mem_eq(pkt, clear, 188);
}
END_TEST

START_TEST(scrambler_decrypt_packet_passes_through_unscrambled_csa2) {
  unsigned char pkt[188], orig[188];
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CSA2);

  memset(pkt, 0x55, 188);
  pkt[0] = 0x47;
  pkt[3] = 0x10; /* afc=1 payload only, scrambling control 00 (not scrambled) */
  memcpy(orig, pkt, 188);

  ck_assert_int_eq(scrambler_decrypt_packet(s, pkt), 0);
  ck_assert_mem_eq(pkt, orig, 188);
  scrambler_free(s);
}
END_TEST

START_TEST(scrambler_decrypt_packet_rejects_unset_key_csa2) {
  unsigned char pkt[188];
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[3] = 0xD0; /* afc=1, scrambling control 11 (odd) - no key loaded */
  ck_assert_int_eq(scrambler_decrypt_packet(s, pkt), -1);
  scrambler_free(s);
}
END_TEST

START_TEST(csa2_batch_matches_single_packet_api_mixed_lengths) {
  /* mixed non-8-aligned lengths, like real TS payloads with varying
     adaptation field sizes - single-packet API is the trusted oracle */
  size_t lens[5] = {184, 179, 171, 8, 184};
  unsigned char single[5][188], batch_bufs[5][188];
  csa2_batch_entry_t entries[5];
  csa2_key_t *k;
  unsigned bs, n, i;
  int trial;

  k = csa2_key_new(cw);
  ck_assert_ptr_nonnull(k);
  bs = csa2_batch_size();
  ck_assert_uint_gt(bs, 0u);
  n = bs < 5u ? bs : 5u;

  srand(2026);
  for (trial = 0; trial < 500; trial++) {
    for (i = 0; i < n; i++) {
      size_t j;
      for (j = 0; j < lens[i]; j++) {
        unsigned char v = (unsigned char)rand();
        single[i][j] = v;
        batch_bufs[i][j] = v;
      }
      entries[i].data = batch_bufs[i];
      entries[i].len = lens[i];
    }
    for (i = 0; i < n; i++)
      csa2_encrypt_block(k, single[i], lens[i]);
    csa2_encrypt_batch(k, entries, n);
    for (i = 0; i < n; i++)
      ck_assert_mem_eq(single[i], batch_bufs[i], lens[i]);

    for (i = 0; i < n; i++)
      csa2_decrypt_block(k, single[i], lens[i]);
    csa2_decrypt_batch(k, entries, n);
    for (i = 0; i < n; i++)
      ck_assert_mem_eq(single[i], batch_bufs[i], lens[i]);
  }
  csa2_key_free(k);
}
END_TEST

#define CAPTURE_MAX 2200
static unsigned char captured[CAPTURE_MAX][188];
static unsigned captured_n;

static void capture_emit(void *ctx, const unsigned char pkt[188]) {
  (void)ctx;
  ck_assert_uint_lt(captured_n, CAPTURE_MAX);
  memcpy(captured[captured_n], pkt, 188);
  captured_n++;
}

START_TEST(scrambler_encrypt_queued_flushes_on_batch_full) {
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  unsigned bs = csa2_batch_size();
  unsigned char pkt[188], ref[188];
  unsigned i;

  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  captured_n = 0;

  for (i = 0; i < bs; i++) {
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[3] = 0x10; /* afc=1 payload only, ctrl=00 */
    pkt[4] = (unsigned char)i; /* vary payload so packets differ */
    ck_assert_int_eq(scrambler_encrypt_packet_queued(s, pkt, SCRAMBLE_PARITY_EVEN, capture_emit, NULL), 0);
    if (i < bs - 1)
      ck_assert_uint_eq(captured_n, 0); /* held, batch not full yet */
  }
  ck_assert_uint_eq(captured_n, bs); /* flushed automatically once full */

  /* cross-check batched output against the non-queued single-packet API */
  for (i = 0; i < bs; i++) {
    scrambler_t *sref = scrambler_new(SCRAMBLE_ALGO_CSA2);
    ck_assert_ptr_nonnull(sref);
    memset(ref, 0, 188);
    ref[0] = 0x47;
    ref[3] = 0x10;
    ref[4] = (unsigned char)i;
    ck_assert_int_eq(scrambler_set_key(sref, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
    ck_assert_int_eq(scrambler_encrypt_packet(sref, ref, SCRAMBLE_PARITY_EVEN), 0);
    ck_assert_mem_eq(ref, captured[i], 188);
    scrambler_free(sref);
  }
  scrambler_free(s);
}
END_TEST

START_TEST(scrambler_encrypt_queued_flushes_on_parity_change) {
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  unsigned char pkt[188];

  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_ODD, cw, sizeof cw, NULL, NULL), 0);
  captured_n = 0;

  memset(pkt, 0, 188);
  pkt[0] = 0x47;
  pkt[3] = 0x10;
  ck_assert_int_eq(scrambler_encrypt_packet_queued(s, pkt, SCRAMBLE_PARITY_EVEN, capture_emit, NULL), 0);
  ck_assert_uint_eq(captured_n, 0); /* held, batch not full yet */

  memset(pkt, 0, 188);
  pkt[0] = 0x47;
  pkt[3] = 0x10;
  ck_assert_int_eq(scrambler_encrypt_packet_queued(s, pkt, SCRAMBLE_PARITY_ODD, capture_emit, NULL), 0);
  ck_assert_uint_eq(captured_n, 1); /* the EVEN one flushed before starting the ODD batch */

  scrambler_flush(s, capture_emit, NULL);
  ck_assert_uint_eq(captured_n, 2);
  scrambler_free(s);
}
END_TEST

START_TEST(scrambler_decrypt_queued_preserves_order_with_passthrough) {
  scrambler_t *senc = scrambler_new(SCRAMBLE_ALGO_CSA2);
  scrambler_t *sdec = scrambler_new(SCRAMBLE_ALGO_CSA2);
  unsigned char clear_a[188], clear_b[188], unscr[188];
  unsigned char pkt_a[188], pkt_b[188];

  ck_assert_ptr_nonnull(senc);
  ck_assert_ptr_nonnull(sdec);
  ck_assert_int_eq(scrambler_set_key(senc, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  ck_assert_int_eq(scrambler_set_key(sdec, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);

  memset(clear_a, 0, 188);
  clear_a[0] = 0x47;
  clear_a[3] = 0x10;
  clear_a[4] = 0xAA;
  memset(clear_b, 0, 188);
  clear_b[0] = 0x47;
  clear_b[3] = 0x10;
  clear_b[4] = 0xBB;
  memset(unscr, 0x55, 188);
  unscr[0] = 0x47;
  unscr[3] = 0x10; /* ctrl 00: never scrambled, must ride through untouched */

  memcpy(pkt_a, clear_a, 188);
  memcpy(pkt_b, clear_b, 188);
  ck_assert_int_eq(scrambler_encrypt_packet(senc, pkt_a, SCRAMBLE_PARITY_EVEN), 0);
  ck_assert_int_eq(scrambler_encrypt_packet(senc, pkt_b, SCRAMBLE_PARITY_EVEN), 0);

  captured_n = 0;
  /* scrambled, then unscrambled passthrough, then scrambled again - none of
     these fill a whole batch, so nothing should flush until explicitly
     forced, and the feed order must be preserved on flush */
  ck_assert_int_eq(scrambler_decrypt_packet_queued(sdec, pkt_a, capture_emit, NULL), 0);
  ck_assert_int_eq(scrambler_decrypt_packet_queued(sdec, unscr, capture_emit, NULL), 0);
  ck_assert_int_eq(scrambler_decrypt_packet_queued(sdec, pkt_b, capture_emit, NULL), 0);
  ck_assert_uint_eq(captured_n, 0);

  scrambler_flush(sdec, capture_emit, NULL);
  ck_assert_uint_eq(captured_n, 3);
  ck_assert_mem_eq(captured[0], clear_a, 188);
  ck_assert_mem_eq(captured[1], unscr, 188);
  ck_assert_mem_eq(captured[2], clear_b, 188);

  scrambler_free(senc);
  scrambler_free(sdec);
}
END_TEST

START_TEST(scrambler_set_key_flushes_pending_batch_under_old_key) {
  static const unsigned char cw2[CSA2_CW_LEN] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  scrambler_t *senc, *sdec;
  unsigned char clear_a[188], clear_b[188], pkt_a[188], pkt_b[188];

  senc = scrambler_new(SCRAMBLE_ALGO_CSA2);
  sdec = scrambler_new(SCRAMBLE_ALGO_CSA2);
  ck_assert_ptr_nonnull(senc);
  ck_assert_ptr_nonnull(sdec);
  ck_assert_int_eq(scrambler_set_key(senc, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  ck_assert_int_eq(scrambler_set_key(sdec, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);

  memset(clear_a, 0, 188);
  clear_a[0] = 0x47;
  clear_a[3] = 0x10;
  clear_a[4] = 0xAA;
  memcpy(pkt_a, clear_a, 188);
  ck_assert_int_eq(scrambler_encrypt_packet(senc, pkt_a, SCRAMBLE_PARITY_EVEN), 0);

  captured_n = 0;
  /* queued, batch not full yet - still sitting in sdec's queue, waiting on the old (cw) key */
  ck_assert_int_eq(scrambler_decrypt_packet_queued(sdec, pkt_a, capture_emit, NULL), 0);
  ck_assert_uint_eq(captured_n, 0);

  /* CW rotates for the same parity while pkt_a is still queued: must flush pkt_a
     under the outgoing key before installing cw2, not silently decrypt it with cw2 */
  ck_assert_int_eq(scrambler_set_key(sdec, SCRAMBLE_PARITY_EVEN, cw2, sizeof cw2, capture_emit, NULL), 0);
  ck_assert_uint_eq(captured_n, 1);
  ck_assert_mem_eq(captured[0], clear_a, 188);

  /* new key now active: a packet encrypted under cw2 decrypts correctly too */
  memset(clear_b, 0, 188);
  clear_b[0] = 0x47;
  clear_b[3] = 0x10;
  clear_b[4] = 0xBB;
  memcpy(pkt_b, clear_b, 188);
  ck_assert_int_eq(scrambler_set_key(senc, SCRAMBLE_PARITY_EVEN, cw2, sizeof cw2, NULL, NULL), 0);
  ck_assert_int_eq(scrambler_encrypt_packet(senc, pkt_b, SCRAMBLE_PARITY_EVEN), 0);
  ck_assert_int_eq(scrambler_decrypt_packet_queued(sdec, pkt_b, capture_emit, NULL), 0);
  scrambler_flush(sdec, capture_emit, NULL);
  ck_assert_uint_eq(captured_n, 2);
  ck_assert_mem_eq(captured[1], clear_b, 188);

  scrambler_free(senc);
  scrambler_free(sdec);
}
END_TEST

START_TEST(scrambler_encrypt_queued_survives_flush_at_queue_capacity) {
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  unsigned char pkt[188], ref_a[188], ref_b[188];
  struct dvbcsa_key_s *dk;
  unsigned bs = csa2_batch_size();
  unsigned cap = bs * 16; /* SCRAMBLER_QUEUE_CAP_MULTIPLIER in scrambler.c */
  unsigned i;

  ck_assert_ptr_nonnull(s);
  captured_n = 0;

  /* fills queue via passthrough (no key yet): mirrors MPTS pid waiting on
     its parity's CW */
  for (i = 0; i < cap; i++) {
    memset(pkt, 0x66, 188);
    pkt[0] = 0x47;
    pkt[3] = 0x10; /* not scrambled */
    scrambler_passthrough_queued(s, pkt, capture_emit, NULL);
  }
  ck_assert_uint_eq(captured_n, 0);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, capture_emit, NULL), 0);

  /* lands on full queue: resulting flush must not lose ensure_batch's
     committed mode/parity */
  memset(pkt, 0, 188);
  pkt[0] = 0x47;
  pkt[3] = 0x10;
  pkt[4] = 0xAA;
  memcpy(ref_a, pkt, 188);
  ck_assert_int_eq(scrambler_encrypt_packet_queued(s, pkt, SCRAMBLE_PARITY_EVEN, capture_emit, NULL), 0);
  ck_assert_uint_eq(captured_n, cap); /* passthrough batch flushed, new entry held */

  /* second same-parity packet must flush first cleanly, not crash on state
     flush above corrupted */
  memset(pkt, 0, 188);
  pkt[0] = 0x47;
  pkt[3] = 0x10;
  pkt[4] = 0xBB;
  memcpy(ref_b, pkt, 188);
  ck_assert_int_eq(scrambler_encrypt_packet_queued(s, pkt, SCRAMBLE_PARITY_EVEN, capture_emit, NULL), 0);

  scrambler_flush(s, capture_emit, NULL);
  ck_assert_uint_eq(captured_n, cap + 2);

  dk = dvbcsa_key_alloc();
  ck_assert_ptr_nonnull(dk);
  dvbcsa_key_set(cw, dk);
  dvbcsa_decrypt(dk, captured[cap] + 4, 184);
  dvbcsa_decrypt(dk, captured[cap + 1] + 4, 184);
  dvbcsa_key_free(dk);
  ck_assert_mem_eq(captured[cap] + 4, ref_a + 4, 184);
  ck_assert_mem_eq(captured[cap + 1] + 4, ref_b + 4, 184);

  scrambler_free(s);
}
END_TEST

START_TEST(scrambler_passthrough_queued_preserves_order) {
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  unsigned char pkt[188], side[188];

  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, NULL, NULL), 0);
  captured_n = 0;

  memset(pkt, 0, 188);
  pkt[0] = 0x47;
  pkt[3] = 0x10;
  ck_assert_int_eq(scrambler_encrypt_packet_queued(s, pkt, SCRAMBLE_PARITY_EVEN, capture_emit, NULL), 0);
  ck_assert_uint_eq(captured_n, 0);

  memset(side, 0x77, 188);
  side[0] = 0x47;
  scrambler_passthrough_queued(s, side, capture_emit, NULL);
  ck_assert_uint_eq(captured_n, 0); /* still held, rides with the batch */

  scrambler_flush(s, capture_emit, NULL);
  ck_assert_uint_eq(captured_n, 2);
  ck_assert_mem_eq(captured[1], side, 188); /* passthrough kept its position, byte-identical */

  scrambler_free(s);
}
END_TEST

static Suite *csa2_suite(void) {
  Suite *s = suite_create("csa2");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, csa2_encrypt_block_round_trips_with_library_decrypt);
  tcase_add_test(tc, scrambler_csa2_packet_rounds_to_8byte_blocks_and_round_trips);
  tcase_add_test(tc, scrambler_csa2_packet_encrypts_full_payload_including_residual);
  tcase_add_test(tc, scrambler_set_key_rejects_wrong_cw_len_for_csa2);
  tcase_add_test(tc, csa2_decrypt_block_round_trips_with_library_encrypt);
  tcase_add_test(tc, scrambler_csa2_packet_descrambles_full_payload_round_trip);
  tcase_add_test(tc, scrambler_decrypt_packet_passes_through_unscrambled_csa2);
  tcase_add_test(tc, scrambler_decrypt_packet_rejects_unset_key_csa2);
  tcase_add_test(tc, csa2_batch_matches_single_packet_api_mixed_lengths);
  tcase_add_test(tc, scrambler_encrypt_queued_flushes_on_batch_full);
  tcase_add_test(tc, scrambler_encrypt_queued_flushes_on_parity_change);
  tcase_add_test(tc, scrambler_decrypt_queued_preserves_order_with_passthrough);
  tcase_add_test(tc, scrambler_set_key_flushes_pending_batch_under_old_key);
  tcase_add_test(tc, scrambler_encrypt_queued_survives_flush_at_queue_capacity);
  tcase_add_test(tc, scrambler_passthrough_queued_preserves_order);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(csa2_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
