/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_CFG_FIXTURE_H
#define DVBIPITOOLS_TESTS_UNIT_CFG_FIXTURE_H

#include <check.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  char dir[64];
  char path[128];
  char extra[160];
} cfg_fixture_t;

typedef enum { CFG_UINT, CFG_SIZE, CFG_UCHAR, CFG_INT, CFG_LONG, CFG_DOUBLE, CFG_STRPTR, CFG_CHARARR } cfg_kind_t;

typedef struct {
  const char *yaml;
  cfg_kind_t kind;
  size_t off;
  double num;
  const char *str;
} cfg_field_case_t;

#define CFG_FIELD(type, y, k, field, n, s) {y, k, offsetof(type, field), n, s}

static inline void cfg_fixture_open(cfg_fixture_t *fx) {
  snprintf(fx->dir, sizeof fx->dir, "/tmp/dvbipitools_cfg_XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(fx->dir));
  snprintf(fx->path, sizeof fx->path, "%s/c.yaml", fx->dir);
  fx->extra[0] = '\0';
}

static inline void cfg_fixture_store(const cfg_fixture_t *fx, const char *text) {
  FILE *f = fopen(fx->path, "w");

  ck_assert_ptr_nonnull(f);
  ck_assert_uint_eq(fwrite(text, 1, strlen(text), f), strlen(text));
  fclose(f);
}

static inline void cfg_fixture_write(cfg_fixture_t *fx, const char *text) {
  cfg_fixture_open(fx);
  cfg_fixture_store(fx, text);
}

static inline void cfg_fixture_write_with_file(cfg_fixture_t *fx, const char *text, const char *token, const char *name) {
  char expanded[2048];
  const char *p = text;
  size_t n = 0;
  FILE *f;

  cfg_fixture_open(fx);
  snprintf(fx->extra, sizeof fx->extra, "%s/%s", fx->dir, name);
  f = fopen(fx->extra, "w");
  ck_assert_ptr_nonnull(f);
  fclose(f);
  while (*p) {
    if (!strncmp(p, token, strlen(token))) {
      n += (size_t)snprintf(expanded + n, sizeof expanded - n, "%s", fx->extra);
      p += strlen(token);
    } else {
      ck_assert_uint_lt(n, sizeof expanded - 1);
      expanded[n++] = *p++;
    }
  }
  expanded[n] = '\0';
  cfg_fixture_store(fx, expanded);
}

static inline void cfg_fixture_remove(cfg_fixture_t *fx) {
  if (fx->extra[0]) unlink(fx->extra);
  unlink(fx->path);
  rmdir(fx->dir);
}

static inline void cfg_field_check(const void *cfg, const cfg_field_case_t *c) {
  const unsigned char *base = (const unsigned char *)cfg + c->off;
  switch (c->kind) {
    case CFG_UINT: ck_assert_uint_eq(*(const unsigned *)base, (unsigned)c->num); break;
    case CFG_SIZE: ck_assert_uint_eq(*(const size_t *)base, (size_t)c->num); break;
    case CFG_UCHAR: ck_assert_uint_eq(*base, (unsigned)c->num); break;
    case CFG_INT: ck_assert_int_eq(*(const int *)base, (int)c->num); break;
    case CFG_LONG: ck_assert_int_eq(*(const long *)base, (long)c->num); break;
    case CFG_DOUBLE: {
      double v;
      memcpy(&v, base, sizeof v);
      ck_assert_double_eq(v, c->num);
      break;
    }
    case CFG_STRPTR: ck_assert_str_eq(*(const char *const *)base, c->str); break;
    case CFG_CHARARR: ck_assert_str_eq((const char *)base, c->str); break;
  }
}

#endif
