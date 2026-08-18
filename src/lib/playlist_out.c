/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "playlist_out.h"

#include <time.h>

#include "xml_util.h"

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

void playlist_out_m3u_item(FILE *f, const char *name, const char *uri, unsigned tsid, unsigned onid, unsigned sid) {
  fprintf(f, "#EXTINF:-1 tsid=\"%u\" onid=\"%u\" sid=\"%u\",%s\n%s\n", tsid, onid, sid, name, uri);
}

void playlist_out_csv_item(FILE *f, const char *name, const char *uri, unsigned tsid, unsigned onid, unsigned sid) {
  const char *p;
  for (p = name; *p; p++)
    if (*p != ',')
      fputc(*p, f);
  fprintf(f, ",%s,%u,%u,%u\n", uri, tsid, onid, sid);
}

void playlist_out_xspf_item(FILE *f, const char *name, const char *uri, unsigned tsid, unsigned onid, unsigned sid) {
  fputs("  <track><location>", f);
  xml_escape(f, uri);
  fputs("</location><title>", f);
  xml_escape(f, name);
  fprintf(f, "</title><extension application=\"urn:dvbipitools:dvb-triplet\" tsid=\"%u\" onid=\"%u\" sid=\"%u\"/></track>\n", tsid, onid, sid);
}
