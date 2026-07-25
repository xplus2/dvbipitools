/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/framer/aac_latm.h"
#include "lib/bim/bitwriter.h"

/* builds a LOAS/LATM frame: useSameStreamMux=0, a StreamMuxConfig with
 * audioMuxVersion=0, single program/layer, AAC LC (2) @ 44100 Hz (sr_idx 4) */
static size_t build_full_config_frame(unsigned char *out, size_t cap, unsigned channel_config) {
  bitwriter_t bw;
  const unsigned char *payload;
  size_t plen;

  bitwriter_init(&bw);
  bitwriter_put(&bw, 0, 1); /* useSameStreamMux = 0 */
  bitwriter_put(&bw, 0, 1); /* audioMuxVersion = 0 */
  bitwriter_put(&bw, 1, 1); /* allStreamsSameTimeFraming */
  bitwriter_put(&bw, 0, 6); /* numSubFrames */
  bitwriter_put(&bw, 0, 4); /* numProgram */
  bitwriter_put(&bw, 0, 3); /* numLayer */
  bitwriter_put(&bw, 2, 5); /* audioObjectType = 2 (AAC LC) */
  bitwriter_put(&bw, 4, 4); /* samplingFrequencyIndex = 4 (44100 Hz) */
  bitwriter_put(&bw, channel_config, 4);

  payload = bitwriter_data(&bw, &plen);
  if (3 + plen > cap) {
    bitwriter_free(&bw);
    return 0;
  }
  out[0] = 0x56;
  out[1] = (unsigned char)(0xE0 | ((plen >> 8) & 0x1F));
  out[2] = (unsigned char)plen;
  memcpy(out + 3, payload, plen);
  bitwriter_free(&bw);
  return 3 + plen;
}

/* builds a "reuse previous config" frame: just the useSameStreamMux=1 bit, no payload beyond it */
static size_t build_reuse_config_frame(unsigned char *out, size_t cap) {
  bitwriter_t bw;
  const unsigned char *payload;
  size_t plen;

  bitwriter_init(&bw);
  bitwriter_put(&bw, 1, 1); /* useSameStreamMux = 1 */
  payload = bitwriter_data(&bw, &plen);
  if (3 + plen > cap) {
    bitwriter_free(&bw);
    return 0;
  }
  out[0] = 0x56;
  out[1] = (unsigned char)(0xE0 | ((plen >> 8) & 0x1F));
  out[2] = (unsigned char)plen;
  memcpy(out + 3, payload, plen);
  bitwriter_free(&bw);
  return 3 + plen;
}

START_TEST(aac_latm_probe_parses_full_stream_mux_config) {
  aac_latm_t *c = aac_latm_new();
  unsigned char frame[32];
  size_t len = build_full_config_frame(frame, sizeof frame, 1);
  aac_latm_info_t info;

  ck_assert_int_eq(aac_latm_probe(c, frame, len, &info), 1);
  ck_assert_uint_eq(info.sample_rate, 44100u);
  ck_assert_uint_eq(info.samples_per_frame, 1024u);
  ck_assert_uint_eq(info.frame_len, len);

  aac_latm_free(c);
}
END_TEST

START_TEST(aac_latm_probe_reuses_config_across_frames) {
  aac_latm_t *c = aac_latm_new();
  unsigned char frame1[32], frame2[32];
  size_t len1 = build_full_config_frame(frame1, sizeof frame1, 1);
  size_t len2 = build_reuse_config_frame(frame2, sizeof frame2);
  aac_latm_info_t info;

  ck_assert_int_eq(aac_latm_probe(c, frame1, len1, &info), 1);
  ck_assert_int_eq(aac_latm_probe(c, frame2, len2, &info), 1);
  ck_assert_uint_eq(info.sample_rate, 44100u); /* carried over from the first frame's config */

  aac_latm_free(c);
}
END_TEST

START_TEST(aac_latm_probe_rejects_reuse_before_any_config_seen) {
  aac_latm_t *c = aac_latm_new();
  unsigned char frame[32];
  size_t len = build_reuse_config_frame(frame, sizeof frame);
  aac_latm_info_t info;

  ck_assert_int_eq(aac_latm_probe(c, frame, len, &info), -1);

  aac_latm_free(c);
}
END_TEST

START_TEST(aac_latm_is_sync_checks_header_bits) {
  unsigned char good[2] = {0x56, 0xE0};
  unsigned char bad[2] = {0x56, 0x00};
  ck_assert_int_eq(aac_latm_is_sync(good, 2), 1);
  ck_assert_int_eq(aac_latm_is_sync(bad, 2), 0);
}
END_TEST

START_TEST(aac_latm_probe_needs_more_bytes) {
  aac_latm_t *c = aac_latm_new();
  unsigned char frame[32];
  aac_latm_info_t info;
  size_t len = build_full_config_frame(frame, sizeof frame, 1);

  ck_assert_int_eq(aac_latm_probe(c, frame, len - 1, &info), 0); /* payload truncated */
  ck_assert_int_eq(aac_latm_probe(c, frame, 2, &info), 0);       /* not even the length header yet */

  aac_latm_free(c);
}
END_TEST

static Suite *aac_latm_suite(void) {
  Suite *s = suite_create("aac_latm");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, aac_latm_probe_parses_full_stream_mux_config);
  tcase_add_test(tc, aac_latm_probe_reuses_config_across_frames);
  tcase_add_test(tc, aac_latm_probe_rejects_reuse_before_any_config_seen);
  tcase_add_test(tc, aac_latm_is_sync_checks_header_bits);
  tcase_add_test(tc, aac_latm_probe_needs_more_bytes);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(aac_latm_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
