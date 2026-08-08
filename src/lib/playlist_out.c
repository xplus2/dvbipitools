/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "playlist_out.h"

#include <time.h>

void playlist_out_stamp(char *buf, size_t n) {
  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);
  strftime(buf, n, "%Y-%m-%d %H:%M", &tm);
}

void playlist_out_init(FILE *f, playlist_out_fmt_t fmt, const char *invocation, const char *title_prefix) {
  char ts[24];
  playlist_out_stamp(ts, sizeof ts);
  switch (fmt) {
    case PLAYLIST_OUT_M3U:
      fprintf(f, "#EXTM3U\n# %s UTC\n# %s\n\n", ts, invocation);
      break;
    case PLAYLIST_OUT_XSPF:
      fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n", f);
      fprintf(f, "  <title>%s%s UTC</title>\n", title_prefix, ts);
      break;
    case PLAYLIST_OUT_CSV:
    case PLAYLIST_OUT_XML:
    case PLAYLIST_OUT_NULL:
      break;
  }
}

void playlist_out_close(FILE *f, playlist_out_fmt_t fmt) {
  switch (fmt) {
    case PLAYLIST_OUT_M3U:  fputs("\n#EXT-X-ENDLIST\n", f); break;
    case PLAYLIST_OUT_XSPF: fputs("</playlist>\n", f);      break;
    case PLAYLIST_OUT_CSV:
    case PLAYLIST_OUT_XML:
    case PLAYLIST_OUT_NULL: break;
  }
}
