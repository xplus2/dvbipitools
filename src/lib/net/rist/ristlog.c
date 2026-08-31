/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <librist/librist.h>

#include "lib/helper/log.h"

#include "ristlog.h"

static int rist_log_cb(void *arg, enum rist_log_level level, const char *msg) {
  (void)arg;
  (void)level;
  log_line("rist: %s", msg);
  return 0;
}

static struct rist_logging_settings *settings;
static int tried;

struct rist_logging_settings *ristlog_get(int verbose) {
  if (tried)
    return settings;
  tried = 1;
  if (rist_logging_set(&settings, verbose ? RIST_LOG_DEBUG : RIST_LOG_WARN, rist_log_cb, NULL, NULL, NULL) != 0) {
    settings = NULL;
    return NULL;
  }
  rist_logging_set_global(settings);
  return settings;
}
