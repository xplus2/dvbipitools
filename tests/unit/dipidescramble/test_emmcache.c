/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "cas_fixture.h"

#include <fcntl.h>
#include <sys/stat.h>

#include "dipidescramble/emmcache.h"
#include "lib/demux/psi/section_asm.h"

#define EMM_BUF 1024

typedef struct {
  EVP_PKEY *key;
  device_state_t *dev;
  emmcache_t *cache;
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
  char path[64];
} fx_t;

static void fill(unsigned char *p, unsigned char base) {
  for (int i = 0; i < CRYPTO_KEY_LEN; i++)
    p[i] = (unsigned char)(base + i);
}

static void fx_open(fx_t *fx, size_t dev_services, size_t cache_services) {
  memset(fx, 0, sizeof *fx);
  fx->key = make_rsa_key();
  fx->dev = make_device_for_key(fx->key, TEST_SERIAL, dev_services);
  fx->cache = emmcache_new(cache_services);
  ck_assert_ptr_nonnull(fx->cache);
  fill(fx->bk, 10);
  fill(fx->sk, 100);
  snprintf(fx->path, sizeof fx->path, "/tmp/dipidescramble_emmcache_XXXXXX");
  close(mkstemp(fx->path));
}

static void fx_close(fx_t *fx) {
  unlink(fx->path);
  emmcache_free(fx->cache);
  device_state_free(fx->dev);
  EVP_PKEY_free(fx->key);
}

static int resolves(device_state_t *d, const unsigned char sk[CRYPTO_KEY_LEN], unsigned service_id) {
  unsigned char ecm[64];
  unsigned char cw[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  unsigned char out[16];
  size_t n = build_ecm(sk, cw, 8, ecm, sizeof ecm);

  return device_resolve_cw(d, ecm, n, service_id, 8, TEST_ECM_PID, out) == 0 && memcmp(out, cw, 8) == 0;
}

START_TEST(emm_u_is_recorded_once_and_a_changed_one_marks_the_cache_stale) {
  fx_t fx;
  unsigned char emm[EMM_BUF];
  unsigned char again[EMM_BUF];
  size_t n;
  size_t n2;

  fx_open(&fx, 0, 0);
  n = build_emm_u(fx.key, fx.bk, emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 1);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 0);
  n2 = build_emm_u(fx.key, fx.bk, again, sizeof again);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, again, n2), 1);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, again, n2), 0);
  fx_close(&fx);
}
END_TEST

START_TEST(emm_g_is_recorded_per_service_and_changes_are_detected) {
  fx_t fx;
  unsigned char emm[EMM_BUF];
  unsigned char other_sk[CRYPTO_KEY_LEN];
  size_t n;

  fx_open(&fx, 0, 0);
  fill(other_sk, 200);
  n = build_emm_u(fx.key, fx.bk, emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 1);

  n = build_emm_g(fx.bk, fx.sk, 0x0064, emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 1);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 0);
  n = build_emm_g(fx.bk, other_sk, 0x0064, emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 1);
  n = build_emm_g(fx.bk, fx.sk, 0x0065, emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 1);
  ck_assert_uint_eq(emmcache_dropped_total(fx.cache), 0u);
  fx_close(&fx);
}
END_TEST

START_TEST(emm_g_beyond_the_cache_capacity_is_counted_as_dropped) {
  fx_t fx;
  unsigned char emm[EMM_BUF];
  size_t n;

  fx_open(&fx, 0, 1);
  n = build_emm_u(fx.key, fx.bk, emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 1);
  n = build_emm_g(fx.bk, fx.sk, 1, emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 1);
  n = build_emm_g(fx.bk, fx.sk, 2, emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 0);
  ck_assert_uint_eq(emmcache_dropped_total(fx.cache), 1u);
  n = build_emm_g(fx.bk, fx.sk, 1, emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 0);
  ck_assert_uint_eq(emmcache_dropped_total(fx.cache), 1u);
  fx_close(&fx);
}
END_TEST

START_TEST(sections_the_device_rejects_are_not_recorded) {
  fx_t fx;
  unsigned char emm[EMM_BUF];
  size_t n;

  fx_open(&fx, 0, 0);
  n = build_emm_u_for(fx.key, fx.bk, "somebody-elses-serial", emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 0);
  n = build_emm_g(fx.bk, fx.sk, 7, emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 0);
  ck_assert_int_eq(emmcache_save(fx.cache, fx.path), 0);
  {
    struct stat sb;

    ck_assert_int_eq(stat(fx.path, &sb), 0);
    ck_assert_int_eq((int)sb.st_size, 0);
  }
  fx_close(&fx);
}
END_TEST

START_TEST(oversized_service_limit_is_clamped_to_the_ceiling) {
  emmcache_t *c = emmcache_new(100000);

  ck_assert_ptr_nonnull(c);
  ck_assert_uint_eq(emmcache_dropped_total(c), 0u);
  emmcache_free(c);
}
END_TEST

START_TEST(bad_section_lengths_are_ignored) {
  fx_t fx;
  static unsigned char big[PSI_SECTION_ASM_BUF_LEN + 8];

  fx_open(&fx, 0, 0);
  memset(big, 0, sizeof big);
  big[0] = SC_SECTION_TID_EMM;
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, big, 0), 0);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, big, 2), 0);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, big, sizeof big), 0);
  fx_close(&fx);
}
END_TEST

START_TEST(saved_cache_rehydrates_a_fresh_device) {
  fx_t fx;
  device_state_t *fresh;
  emmcache_t *cache2 = emmcache_new(0);
  unsigned char emm[EMM_BUF];
  size_t n;

  fx_open(&fx, 0, 0);
  n = build_emm_u(fx.key, fx.bk, emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 1);
  n = build_emm_g(fx.bk, fx.sk, 0x0064, emm, sizeof emm);
  ck_assert_int_eq(emmcache_feed(fx.cache, fx.dev, emm, n), 1);
  ck_assert_int_eq(emmcache_save(fx.cache, fx.path), 0);

  fresh = make_device_for_key(fx.key, TEST_SERIAL, 0);
  ck_assert_int_eq(resolves(fresh, fx.sk, 0x0064), 0);
  ck_assert_int_eq(emmcache_load(cache2, fresh, fx.path), 0);
  ck_assert_int_eq(resolves(fresh, fx.sk, 0x0064), 1);

  emmcache_free(cache2);
  device_state_free(fresh);
  fx_close(&fx);
}
END_TEST

START_TEST(loaded_sections_count_as_already_cached_repeats) {
  fx_t fx;
  device_state_t *fresh;
  emmcache_t *cache2 = emmcache_new(0);
  unsigned char emm[EMM_BUF];
  size_t n;

  fx_open(&fx, 0, 0);
  n = build_emm_u(fx.key, fx.bk, emm, sizeof emm);
  emmcache_feed(fx.cache, fx.dev, emm, n);
  ck_assert_int_eq(emmcache_save(fx.cache, fx.path), 0);
  fresh = make_device_for_key(fx.key, TEST_SERIAL, 0);
  ck_assert_int_eq(emmcache_load(cache2, fresh, fx.path), 0);
  ck_assert_int_eq(emmcache_feed(cache2, fresh, emm, n), 0);
  emmcache_free(cache2);
  device_state_free(fresh);
  fx_close(&fx);
}
END_TEST

START_TEST(truncated_cache_file_replays_only_the_complete_sections) {
  fx_t fx;
  device_state_t *fresh;
  emmcache_t *cache2 = emmcache_new(0);
  unsigned char emm_u[EMM_BUF];
  unsigned char emm_g[EMM_BUF];
  size_t nu;
  size_t ng;

  fx_open(&fx, 0, 0);
  nu = build_emm_u(fx.key, fx.bk, emm_u, sizeof emm_u);
  ng = build_emm_g(fx.bk, fx.sk, 0x0064, emm_g, sizeof emm_g);
  emmcache_feed(fx.cache, fx.dev, emm_u, nu);
  emmcache_feed(fx.cache, fx.dev, emm_g, ng);
  ck_assert_int_eq(emmcache_save(fx.cache, fx.path), 0);
  ck_assert_int_eq(truncate(fx.path, (off_t)(nu + ng - 5)), 0);

  fresh = make_device_for_key(fx.key, TEST_SERIAL, 0);
  ck_assert_int_eq(emmcache_load(cache2, fresh, fx.path), 0);
  ck_assert_int_eq(resolves(fresh, fx.sk, 0x0064), 0);
  ck_assert_int_eq(emmcache_feed(cache2, fresh, emm_g, ng), 1);
  ck_assert_int_eq(resolves(fresh, fx.sk, 0x0064), 1);
  emmcache_free(cache2);
  device_state_free(fresh);
  fx_close(&fx);
}
END_TEST

START_TEST(file_over_one_mebibyte_is_refused_and_absent_or_garbage_files_are_harmless) {
  fx_t fx;
  FILE *f;
  static unsigned char junk[(1 << 20) + 4096];

  fx_open(&fx, 0, 0);
  memset(junk, 0xAB, sizeof junk);
  f = fopen(fx.path, "wb");
  ck_assert_ptr_nonnull(f);
  ck_assert_uint_eq(fwrite(junk, 1, sizeof junk, f), sizeof junk);
  fclose(f);
  ck_assert_int_eq(emmcache_load(fx.cache, fx.dev, fx.path), -1);

  ck_assert_int_eq(emmcache_load(fx.cache, fx.dev, "/nonexistent-dir-dipidescramble/none.bin"), 0);

  f = fopen(fx.path, "wb");
  if (!f) abort();
  fwrite(junk, 1, 100, f);
  fclose(f);
  ck_assert_int_eq(emmcache_load(fx.cache, fx.dev, fx.path), 0);
  f = fopen(fx.path, "wb");
  if (!f) abort();
  fwrite("\x82\x7f\xff", 1, 3, f);
  fclose(f);
  ck_assert_int_eq(emmcache_load(fx.cache, fx.dev, fx.path), 0);
  fx_close(&fx);
}
END_TEST

START_TEST(save_fails_cleanly_for_unwritable_paths) {
  fx_t fx;
  unsigned char emm[EMM_BUF];
  size_t n;

  fx_open(&fx, 0, 0);
  n = build_emm_u(fx.key, fx.bk, emm, sizeof emm);
  emmcache_feed(fx.cache, fx.dev, emm, n);
  ck_assert_int_eq(emmcache_save(fx.cache, "/nonexistent-dir-dipidescramble/cache.bin"), -1);
  if (access("/dev/full", W_OK) == 0)
    ck_assert_int_eq(emmcache_save(fx.cache, "/dev/full"), -1);
  fx_close(&fx);
}
END_TEST

static Suite *emmcache_suite(void) {
  Suite *s = suite_create("emmcache");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, emm_u_is_recorded_once_and_a_changed_one_marks_the_cache_stale);
  tcase_add_test(tc, emm_g_is_recorded_per_service_and_changes_are_detected);
  tcase_add_test(tc, emm_g_beyond_the_cache_capacity_is_counted_as_dropped);
  tcase_add_test(tc, sections_the_device_rejects_are_not_recorded);
  tcase_add_test(tc, oversized_service_limit_is_clamped_to_the_ceiling);
  tcase_add_test(tc, bad_section_lengths_are_ignored);
  tcase_add_test(tc, saved_cache_rehydrates_a_fresh_device);
  tcase_add_test(tc, loaded_sections_count_as_already_cached_repeats);
  tcase_add_test(tc, truncated_cache_file_replays_only_the_complete_sections);
  tcase_add_test(tc, file_over_one_mebibyte_is_refused_and_absent_or_garbage_files_are_harmless);
  tcase_add_test(tc, save_fails_cleanly_for_unwritable_paths);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(emmcache_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
