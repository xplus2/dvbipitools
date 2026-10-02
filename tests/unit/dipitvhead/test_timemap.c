/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "dipitvhead/mux/pcrclock.h"
#include "dipitvhead/mux/timemap.h"

#define TICKS(sec) ((uint64_t)((sec) * 27000000.0))

static int64_t signed_diff(uint64_t a, uint64_t b) {
  uint64_t d = pcr_sub(a, b);
  return d > PCR_MODULUS / 2 ? -(int64_t)(PCR_MODULUS - d) : (int64_t)d;
}

START_TEST(first_pcr_passes_through_unchanged) {
  timemap_t t;
  timemap_init(&t);
  ck_assert_uint_eq(timemap_pcr(&t, 123456789, 10.0), 123456789u);
  ck_assert_uint_eq(timemap_k90(&t), 0u);
  ck_assert_uint_eq((unsigned)t.relatches, 0u);
}
END_TEST

START_TEST(continuous_pcr_stays_untouched_over_many_samples) {
  timemap_t t;
  uint64_t src = 1000000;
  timemap_init(&t);
  for (int i = 0; i < 1000; i++) {
    ck_assert_uint_eq(timemap_pcr(&t, src, 10.0 + i * 0.04), src);
    src += TICKS(0.04);
  }
  ck_assert_uint_eq((unsigned)t.relatches, 0u);
}
END_TEST

START_TEST(forward_jump_is_bridged_with_wall_clock_progress) {
  timemap_t t;
  uint64_t before;
  uint64_t out;
  timemap_init(&t);
  before = timemap_pcr(&t, TICKS(100), 10.0);
  out = timemap_pcr(&t, TICKS(100) + TICKS(37), 10.04);
  ck_assert_uint_eq((unsigned)t.relatches, 1u);
  ck_assert_int_le(llabs(signed_diff(out, before) - (int64_t)TICKS(0.04)), 150);
}
END_TEST

START_TEST(backward_jump_is_bridged_with_wall_clock_progress) {
  timemap_t t;
  uint64_t before;
  uint64_t out;
  timemap_init(&t);
  before = timemap_pcr(&t, TICKS(500), 10.0);
  out = timemap_pcr(&t, TICKS(20), 10.04);
  ck_assert_uint_eq((unsigned)t.relatches, 1u);
  ck_assert_int_le(llabs(signed_diff(out, before) - (int64_t)TICKS(0.04)), 150);
}
END_TEST

START_TEST(offset_is_kept_after_a_relatch) {
  timemap_t t;
  uint64_t o1;
  uint64_t o2;
  timemap_init(&t);
  timemap_pcr(&t, TICKS(100), 10.0);
  o1 = timemap_pcr(&t, TICKS(5000), 10.04);
  o2 = timemap_pcr(&t, TICKS(5000) + TICKS(0.04), 10.08);
  ck_assert_uint_eq((unsigned)t.relatches, 1u);
  ck_assert_uint_eq(pcr_sub(o2, o1), TICKS(0.04));
  ck_assert_uint_eq(pcr_sub(o2, TICKS(5000) + TICKS(0.04)), timemap_k90(&t) * 300u);
}
END_TEST

START_TEST(outage_advances_output_by_the_wall_clock_gap) {
  timemap_t t;
  uint64_t before;
  uint64_t out;
  timemap_init(&t);
  before = timemap_pcr(&t, TICKS(100), 10.0);
  out = timemap_pcr(&t, TICKS(7), 15.0);
  ck_assert_int_le(llabs(signed_diff(out, before) - (int64_t)TICKS(5.0)), 150);
}
END_TEST

START_TEST(offset_stays_within_the_33_bit_range) {
  timemap_t t;
  timemap_init(&t);
  timemap_pcr(&t, PCR_MODULUS - 1000, 10.0);
  timemap_pcr(&t, 5000000, 10.04);
  ck_assert_uint_lt(timemap_k90(&t), (uint64_t)1 << 33);
}
END_TEST

START_TEST(same_instant_samples_use_a_small_step_fence) {
  timemap_t t;
  timemap_init(&t);
  timemap_pcr(&t, TICKS(100), 10.0);
  timemap_pcr(&t, TICKS(100.04), 10.0);
  ck_assert_uint_eq((unsigned)t.relatches, 0u);
  timemap_pcr(&t, TICKS(110), 10.0);
  ck_assert_uint_eq((unsigned)t.relatches, 1u);
}
END_TEST

START_TEST(plausibility_fence_matches_the_documented_bounds) {
  ck_assert_int_eq(timemap_pcr_plausible(1000, 1000, 0.04), 0);
  ck_assert_int_eq(timemap_pcr_plausible(1000, 1000 + TICKS(0.04), 0.04), 1);
  ck_assert_int_eq(timemap_pcr_plausible(1000, 1000 + TICKS(61), 100.0), 0);
  ck_assert_int_eq(timemap_pcr_plausible(1000, 1000 + TICKS(0.6), 0.04), 1);
  ck_assert_int_eq(timemap_pcr_plausible(1000, 1000 + TICKS(0.8), 0.04), 0);
  ck_assert_int_eq(timemap_pcr_plausible(PCR_MODULUS - 100, 5000, 0.04), 1);
  ck_assert_int_eq(timemap_pcr_plausible(5000, 4000, 0.04), 0);
}
END_TEST

#define MOD33 ((uint64_t)1 << 33)

static int64_t lead_of(const timemap_t *t, uint64_t stamp, uint64_t p90) {
  uint64_t d = (stamp + t->k90 + MOD33 - p90) % MOD33;
  return d >= MOD33 / 2 ? (int64_t)d - (int64_t)MOD33 : (int64_t)d;
}

START_TEST(regen_without_drift_keeps_the_offset_constant) {
  timemap_t t;
  uint64_t stamp = 1000000;
  uint64_t p = 500000;
  timemap_init(&t);
  timemap_regen_first(&t, 77);
  for (int i = 0; i < 2500; i++) {
    ck_assert_int_eq(timemap_regen_sample(&t, stamp, p, 63000), 0);
    stamp += 3600;
    p += 3600;
  }
  ck_assert_uint_eq(timemap_k90(&t), 77u);
  ck_assert_uint_eq((unsigned)t.rg_relatches, 0u);
}
END_TEST

static void drift_run(int per_sample_drift, int64_t *k_delta, int64_t *lead_change, int *max_step) {
  timemap_t t;
  uint64_t stamp = 4000000;
  uint64_t p = 1000000;
  int64_t first_lead = 0;
  uint64_t prev_k;
  timemap_init(&t);
  timemap_regen_first(&t, 0);
  prev_k = 0;
  *max_step = 0;
  for (int i = 0; i < 600; i++) {
    int64_t step;
    ck_assert_int_eq(timemap_regen_sample(&t, stamp, p, 63000), 0);
    if (i == 0) first_lead = lead_of(&t, stamp, p);
    step = (int64_t)t.k90 - (int64_t)prev_k;
    if (step < 0) step = -step;
    if (step > *max_step) *max_step = (int)step;
    prev_k = t.k90;
    p += 90000;
    stamp += (uint64_t)(90000 + per_sample_drift);
  }
  *k_delta = (int64_t)t.k90;
  *lead_change = lead_of(&t, stamp - (uint64_t)(90000 + per_sample_drift), p - 90000) - first_lead;
}

START_TEST(regen_slow_source_clock_is_compensated_without_steps) {
  int64_t k;
  int64_t lead;
  int max_step;
  drift_run(-5, &k, &lead, &max_step);
  ck_assert_int_ge((int)k, 2400);
  ck_assert_int_le((int)k, 3600);
  ck_assert_int_ge((int)lead, -120);
  ck_assert_int_le((int)lead, 120);
  ck_assert_int_le(max_step, 9);
}
END_TEST

START_TEST(regen_fast_source_clock_is_compensated_without_steps) {
  int64_t k;
  int64_t lead;
  int max_step;
  drift_run(5, &k, &lead, &max_step);
  ck_assert_int_ge((int)((int64_t)MOD33 - k), 2400);
  ck_assert_int_le((int)((int64_t)MOD33 - k), 3600);
  ck_assert_int_ge((int)lead, -120);
  ck_assert_int_le((int)lead, 120);
  ck_assert_int_le(max_step, 9);
}
END_TEST

START_TEST(regen_arrival_jitter_does_not_make_the_offset_wander) {
  timemap_t t;
  uint64_t stamp = 4000000;
  uint64_t p = 1000000;
  uint32_t rnd = 12345;
  int64_t k;
  timemap_init(&t);
  timemap_regen_first(&t, 0);
  for (int i = 0; i < 7500; i++) {
    uint64_t jitter;
    rnd = rnd * 1103515245u + 12345u;
    jitter = (rnd >> 16) % 3000;
    ck_assert_int_eq(timemap_regen_sample(&t, stamp, p - jitter, 63000), 0);
    stamp += 3600;
    p += 3600;
  }
  k = t.k90 > MOD33 / 2 ? (int64_t)t.k90 - (int64_t)MOD33 : (int64_t)t.k90;
  ck_assert_int_ge((int)k, -600);
  ck_assert_int_le((int)k, 600);
}
END_TEST

START_TEST(regen_forward_and_backward_jumps_relatch_to_the_lead) {
  timemap_t t;
  timemap_init(&t);
  timemap_regen_first(&t, 0);
  ck_assert_int_eq(timemap_regen_sample(&t, 1000000, 500000, 63000), 0);
  ck_assert_int_eq(timemap_regen_sample(&t, 1003600, 503600, 63000), 0);
  ck_assert_int_eq(timemap_regen_sample(&t, 1003600 + 90000 * 3, 507200, 63000), 1);
  ck_assert_int_eq((int)lead_of(&t, 1003600 + 90000 * 3, 507200), 63000);
  ck_assert_int_eq(timemap_regen_sample(&t, 1003600 + 90000 * 3 - 1000, 510800, 63000), 1);
  ck_assert_int_eq((int)lead_of(&t, 1003600 + 90000 * 3 - 1000, 510800), 63000);
  ck_assert_uint_eq((unsigned)t.rg_relatches, 2u);
}
END_TEST

START_TEST(regen_stamps_across_the_33_bit_wrap_are_continuous) {
  timemap_t t;
  uint64_t stamp = MOD33 - 7200;
  uint64_t p = 1000;
  timemap_init(&t);
  timemap_regen_first(&t, 0);
  for (int i = 0; i < 10; i++) {
    ck_assert_int_eq(timemap_regen_sample(&t, stamp % MOD33, p, 63000), 0);
    stamp += 3600;
    p += 3600;
  }
  ck_assert_uint_eq((unsigned)t.rg_relatches, 0u);
}
END_TEST

START_TEST(regen_first_resets_the_window_and_reduces_the_offset) {
  timemap_t t;
  timemap_init(&t);
  timemap_regen_first(&t, MOD33 + 5);
  ck_assert_uint_eq(timemap_k90(&t), 5u);
  ck_assert_int_eq(timemap_regen_active(&t), 1);
}
END_TEST

static Suite *timemap_suite(void) {
  Suite *s = suite_create("timemap");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, first_pcr_passes_through_unchanged);
  tcase_add_test(tc, continuous_pcr_stays_untouched_over_many_samples);
  tcase_add_test(tc, forward_jump_is_bridged_with_wall_clock_progress);
  tcase_add_test(tc, backward_jump_is_bridged_with_wall_clock_progress);
  tcase_add_test(tc, offset_is_kept_after_a_relatch);
  tcase_add_test(tc, outage_advances_output_by_the_wall_clock_gap);
  tcase_add_test(tc, offset_stays_within_the_33_bit_range);
  tcase_add_test(tc, same_instant_samples_use_a_small_step_fence);
  tcase_add_test(tc, plausibility_fence_matches_the_documented_bounds);
  tcase_add_test(tc, regen_without_drift_keeps_the_offset_constant);
  tcase_add_test(tc, regen_slow_source_clock_is_compensated_without_steps);
  tcase_add_test(tc, regen_fast_source_clock_is_compensated_without_steps);
  tcase_add_test(tc, regen_arrival_jitter_does_not_make_the_offset_wander);
  tcase_add_test(tc, regen_forward_and_backward_jumps_relatch_to_the_lead);
  tcase_add_test(tc, regen_stamps_across_the_33_bit_wrap_are_continuous);
  tcase_add_test(tc, regen_first_resets_the_window_and_reduces_the_offset);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(timemap_suite());
  int failed;
  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? 0 : 1;
}
