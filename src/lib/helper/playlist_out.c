/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "playlist_out.h"

#include <string.h>
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
    case PLAYLIST_OUT_XSPF: fputs("</playlist>\n", f); break;
    case PLAYLIST_OUT_M3U:
    case PLAYLIST_OUT_CSV:
    case PLAYLIST_OUT_XML:
    case PLAYLIST_OUT_NULL: break;
  }
}

/* drop control chars, opt '"' for quoted attributes */
static void m3u_put(FILE *f, const char *s, int in_quotes) {
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x20 || c == 0x7f || (in_quotes && c == '"'))
      continue;
    fputc(c, f);
  }
}

void playlist_out_m3u_item(FILE *f, const char *name, const char *uri, const char *icon_uri, unsigned tsid, unsigned onid, unsigned sid) {
  fputs("#EXTINF:-1 ", f);
  if (icon_uri && *icon_uri) {
    fputs("tvg-logo=\"", f);
    m3u_put(f, icon_uri, 1);
    fputs("\" ", f);
  }
  fprintf(f, "tsid=\"%u\" onid=\"%u\" sid=\"%u\",", tsid, onid, sid);
  m3u_put(f, name, 0);
  fputc('\n', f);
  m3u_put(f, uri, 0);
  fputc('\n', f);
}

/* RFC 4180 quoting where reader would split, trim or skip ('#' comment) */
static int csv_needs_quotes(const char *s) {
  size_t n = strlen(s);
  return strpbrk(s, ",\"") || s[0] == '#' || (n && (s[0] == ' ' || s[0] == '\t' || s[n - 1] == ' ' || s[n - 1] == '\t'));
}

void playlist_out_csv_item(FILE *f, const char *name, const char *uri, unsigned tsid, unsigned onid, unsigned sid) {
  int quote = csv_needs_quotes(name);
  if (quote) fputc('"', f);
  for (const char *p = name; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x20 || c == 0x7f) continue;
    if (c == '"') fputc('"', f);
    fputc(c, f);
  }
  if (quote) fputc('"', f);
  fprintf(f, ",%s,%u,%u,%u\n", uri, tsid, onid, sid);
}

void playlist_out_xspf_item(FILE *f, const char *name, const char *uri, const char *icon_uri, unsigned tsid, unsigned onid, unsigned sid) {
  fputs("  <track><location>", f);
  xml_escape(f, uri);
  fputs("</location><title>", f);
  xml_escape(f, name);
  fputs("</title>", f);
  if (icon_uri && *icon_uri) {
    fputs("<image>", f);
    xml_escape(f, icon_uri);
    fputs("</image>", f);
  }
  fprintf(f, "<extension application=\"urn:dvbipitools:dvb-triplet\" tsid=\"%u\" onid=\"%u\" sid=\"%u\"/></track>\n", tsid, onid, sid);
}
