/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

static void stamp(char *ts, size_t n) {
  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);
  strftime(ts, n, "%Y%m%d %H%M%S", &tm);
}

int log_stderr_is_tty(void) {
  static int tty = -1;
  if (tty < 0) tty = isatty(STDERR_FILENO);
  return tty;
}

/* color only if tty and not NO_COLOR / TERM=dumb */
static log_color_t color_mode = LOG_COLOR_AUTO;

void log_set_color(log_color_t mode) { color_mode = mode; }

log_color_t log_color_prescan(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    const char *v = NULL;
    if (!strcmp(argv[i], "--color") && i + 1 < argc)    v = argv[i + 1];
    else if (!strncmp(argv[i], "--color=", 8)) v = argv[i] + 8;
    if (!v) continue;
    if (!strcmp(v, "always")) return LOG_COLOR_ALWAYS;
    if (!strcmp(v, "never")) return LOG_COLOR_NEVER;
  }
  return LOG_COLOR_AUTO;
}

/* let's have nice things */
int log_colors_enabled(void) {
  static int on = -1;
  if (color_mode == LOG_COLOR_ALWAYS) return 1;
  if (color_mode == LOG_COLOR_NEVER) return 0;
  if (on < 0) {
    const char *term = getenv("TERM");
    on = log_stderr_is_tty() && !getenv("NO_COLOR") && !(term && strcmp(term, "dumb") == 0);
  }
  return on;
}

/* skips a CSI escape sequence (ESC '[' params... final byte), *rp already past "ESC[" */
static void skip_csi_params(char **rp) {
  char *r = *rp;
  while (*r && (*r < 0x40 || *r > 0x7E)) r++; /* params */
  if (*r) r++; /* final */
  *rp = r;
}

/* strip ANSI escapes in place: CSI and two-byte */
static void strip_ansi(char *s) {
  char *r = s;
  char *w = s;
  while (*r) {
    if (*r == 0x1B) {
      r++;
      if (*r == '[') {
        r++;
        skip_csi_params(&r);
      } else if (*r) {
        r++;
      }
      continue;
    }
    *w++ = *r++;
  }
  *w = '\0';
}

static void emit(const char *code, const char *msg) {
  char ts[20];
  stamp(ts, sizeof ts);
  long pid = (long)getpid();
  if (code) fprintf(stderr, "%s %ld [%s] %s\n", ts, pid, code, msg);
  else      fprintf(stderr, "%s %ld %s\n", ts, pid, msg);
}

void log_line(const char *fmt, ...) {
  char msg[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  emit(NULL, msg);
}

void log_code(const char *code, const char *fmt, ...) {
  char msg[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  emit(code, msg);
}

void log_throttled(log_throttle_t *t, int window_s, const char *code, const char *fmt, ...) {
  struct timespec now;
  long next;
  long new_next;
  va_list ap;
  char msg[512];
  unsigned long suppressed;
  clock_gettime(CLOCK_MONOTONIC, &now);
  next = atomic_load_explicit(&t->next_log_s, memory_order_relaxed);
  if ((long)now.tv_sec < next) {
    atomic_fetch_add_explicit(&t->suppressed, 1, memory_order_relaxed);
    return;
  }
  new_next = (long)now.tv_sec + window_s;
  if (!atomic_compare_exchange_strong_explicit(&t->next_log_s, &next, new_next, memory_order_relaxed, memory_order_relaxed)) {
    atomic_fetch_add_explicit(&t->suppressed, 1, memory_order_relaxed);
    return;
  }
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  suppressed = atomic_exchange_explicit(&t->suppressed, 0, memory_order_relaxed);
  if (suppressed) log_code(code, "%s (%lu more in %ds)", msg, suppressed, window_s);
  else            log_code(code, "%s", msg);
}

static void vemit_ansi(const char *code, const char *fmt, va_list ap) __attribute__((format(printf, 2, 0)));

static void vemit_ansi(const char *code, const char *fmt, va_list ap) {
  char msg[4096];
  vsnprintf(msg, sizeof msg, fmt, ap);
  if (!log_colors_enabled()) strip_ansi(msg);
  emit(code, msg);
}

void log_line_ansi(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vemit_ansi(NULL, fmt, ap);
  va_end(ap);
}

void log_code_ansi(const char *code, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vemit_ansi(code, fmt, ap);
  va_end(ap);
}
