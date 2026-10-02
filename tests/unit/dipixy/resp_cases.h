/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_TEST_RESP_CASES_H
#define DIPIXY_TEST_RESP_CASES_H

#include <stdint.h>
#include <stdlib.h>

#include <check.h>

#define MID_BODY 1500
#define BIG_BODY 100000

static const char *const cors_list = "https://a.example, https://b.example";

static inline uint8_t *make_body(size_t len) {
  uint8_t *b = malloc(len);

  ck_assert_ptr_nonnull(b);
  for (size_t i = 0; i < len; i++) b[i] = (uint8_t)(i * 7 + 3);
  return b;
}

static inline uint32_t body_checksum(size_t len) {
  uint32_t sum = 0;
  for (size_t i = 0; i < len; i++) sum = sum * 31u + (uint8_t)(i * 7 + 3);
  return sum;
}

typedef struct {
  int given;
  int expect;
} status_case_t;

static const status_case_t status_cases[] = {
  {200, 200},
  {304, 304},
  {404, 404},
  {500, 500},
  {418, 500},
  {0, 500},
  {-1, 500},
};

typedef struct {
  const char *origin;
  const char *expect_allow;
  int expect_vary;
} cors_case_t;

static const cors_case_t cors_cases[] = {
  {"https://b.example", "https://b.example", 1},
  {"https://a.example", "https://a.example", 1},
  {"https://evil.example", NULL, 0},
  {NULL, NULL, 0},
};

#endif
