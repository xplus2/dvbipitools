/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_DIPIRADIOHEAD_ARGS_FIXTURE_H
#define DVBIPITOOLS_TESTS_UNIT_DIPIRADIOHEAD_ARGS_FIXTURE_H

#include <stddef.h>

#include "dipiradiohead/cli/args.h"
#include "dipiradiohead/config.h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define ARGS_FIXTURE_MAX 64
#define ARGS_FIXTURE_ARGC(a) ((int)ARRAY_LEN(a) - 1)

typedef struct {
  const char *opt;
  const char *val;
} opt_case_t;

static inline int args_fixture_base(char **argv) {
  static const char *const base[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000"};
  int n = 0;

  for (size_t i = 0; i < ARRAY_LEN(base); i++) argv[n++] = (char *)base[i];
  return n;
}

static inline args_status_t args_fixture_parse_list(config_t *cfg, const char *const *extra) {
  char *argv[ARGS_FIXTURE_MAX];
  int n = args_fixture_base(argv);

  for (size_t i = 0; extra[i]; i++) argv[n++] = (char *)extra[i];
  argv[n] = NULL;
  return args_parse(n, argv, cfg);
}

static inline args_status_t args_fixture_parse_with(config_t *cfg, const char *const *prefix, const opt_case_t *oc) {
  char *argv[ARGS_FIXTURE_MAX];
  int n = args_fixture_base(argv);

  for (size_t i = 0; prefix && prefix[i]; i++) argv[n++] = (char *)prefix[i];
  argv[n++] = (char *)oc->opt;
  if (oc->val) argv[n++] = (char *)oc->val;
  argv[n] = NULL;
  return args_parse(n, argv, cfg);
}

#endif
