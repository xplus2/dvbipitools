/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "args.h"
#include "lib/log.h"
#include "lib/signal.h"
#include "record.h"
#include "version.h"

static const char *fmt_name(out_fmt_t f) {
  switch (f) {
    case FMT_RAW:   return "raw";
    case FMT_TS:    return "ts";
    case FMT_MKV:   return "mkv";
    case FMT_MKA:   return "mka";
  }
  return "?";
}

static const char *sub_name(sub_mode_t s) {
  switch (s) {
    case SUB_KEEP:    return "keep";
    case SUB_STRIP:   return "strip";
    case SUB_SRT:     return "srt";
  }
  return "?";
}

/* append, length clamped */
static int app(char *b, size_t cap, int n, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static int app(char *b, size_t cap, int n, const char *fmt, ...) {
  va_list ap;
  int r;

  if (n < 0 || (size_t)n >= cap)
    return (int)cap;
  va_start(ap, fmt);
  r = vsnprintf(b + n, cap - (size_t)n, fmt, ap);
  va_end(ap);
  if (r < 0)
    return n;
  n += r;
  return ((size_t)n > cap) ? (int)cap : n;
}

/* banner prints before parsing: --color read early */
static log_color_t color_prescan(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; i++) {
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

int main(int argc, char **argv) {
  config_t cfg;
  char src[1024], line[2048];
  args_status_t st;
  int n = 0;

  log_set_color(color_prescan(argc, argv));

  /* banner */
  log_line_ansi("\e[1m%s\e[0m \e[0;32mv%s\e[0m \e[0;37m%s\e[0m \e[0;37m%s\e[0m \e[0;34m%s\e[0m", TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_OK)
    log_set_color((log_color_t)cfg.color_mode);
  if (st == ARGS_HELP)
    return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }

  source_describe(&cfg.source, src, sizeof src);
  n = app(line, sizeof line, n, "\e[1mi:\e[0m\e[0;37m%s\e[0m \e[1mo:\e[0m\e[0;37m%s\e[0m \e[1mf:\e[0m\e[0;37m%s\e[0m", src, cfg.out_path, fmt_name(cfg.format));
  if (cfg.audio_all) n = app(line, sizeof line, n, " \e[1ma:\e[0m\e[0;37mall\e[0m");
  else n = app(line, sizeof line, n, " \e[1ma:\e[0m\e[0;37m%u\e[0m", cfg.audio_track);
  n = app(line, sizeof line, n, " \e[1ms:\e[0m\e[0;37m%s\e[0m", sub_name(cfg.subs));
  if (cfg.duration_s) n = app(line, sizeof line, n, " \e[1md:\e[0m\e[0;37m%ld\e[0m s", cfg.duration_s);
  else n = app(line, sizeof line, n, " \e[1md:\e[0m\e[0;37mforever\e[0m");
  if (cfg.iface) app(line, sizeof line, n, " \e[1mif:\e[0m\e[0;37m%s\e[0m", cfg.iface);
  log_line_ansi("%s", line);
  signals_install();
  return record_run(&cfg);
}
