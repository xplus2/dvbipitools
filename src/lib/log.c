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
  strftime(ts, n, "%Y-%m-%d %H:%M:%S", &tm);
}

int log_stderr_is_tty(void) {
  static int tty = -1;
  if (tty < 0)
    tty = isatty(STDERR_FILENO);
  return tty;
}

/* color only if tty and not NO_COLOR / TERM=dumb */
static log_color_t color_mode = LOG_COLOR_AUTO;

void log_set_color(log_color_t mode) { color_mode = mode; }

int log_color_from_string(const char *s, log_color_t *out) {
  if (!strcmp(s, "auto"))
    *out = LOG_COLOR_AUTO;
  else if (!strcmp(s, "always"))
    *out = LOG_COLOR_ALWAYS;
  else if (!strcmp(s, "never"))
    *out = LOG_COLOR_NEVER;
  else
    return -1;
  return 0;
}

log_color_t log_color_prescan(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    const char *v = NULL;
    if (!strcmp(argv[i], "--color") && i + 1 < argc)
      v = argv[i + 1];
    else if (!strncmp(argv[i], "--color=", 8))
      v = argv[i] + 8;
    if (!v)
      continue;
    if (!strcmp(v, "always"))
      return LOG_COLOR_ALWAYS;
    if (!strcmp(v, "never"))
      return LOG_COLOR_NEVER;
  }
  return LOG_COLOR_AUTO;
}

/* let's have nice things */
int log_colors_enabled(void) {
  static int on = -1;
  if (color_mode == LOG_COLOR_ALWAYS)
    return 1;
  if (color_mode == LOG_COLOR_NEVER)
    return 0;
  if (on < 0) {
    const char *term = getenv("TERM");
    on = log_stderr_is_tty() && !getenv("NO_COLOR") && !(term && strcmp(term, "dumb") == 0);
  }
  return on;
}

/* skips a CSI escape sequence (ESC '[' params... final byte), *rp already past "ESC[" */
static void skip_csi_params(char **rp) {
  char *r = *rp;
  while (*r && (*r < 0x40 || *r > 0x7E)) /* params */
    r++;
  if (*r)
    r++; /* final */
  *rp = r;
}

/* strip ANSI escapes in place: CSI and two-byte */
static void strip_ansi(char *s) {
  char *r = s, *w = s;

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

void log_line(const char *fmt, ...) {
  char ts[20];
  va_list ap;
  stamp(ts, sizeof ts);
  fputs(ts, stderr);
  fputc(' ', stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}

void log_line_ansi(const char *fmt, ...) {
  char ts[20], msg[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  if (!log_colors_enabled())
    strip_ansi(msg);
  stamp(ts, sizeof ts);
  fprintf(stderr, "%s %s\n", ts, msg);
}
