/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <string.h>
#include <time.h>

#include "format_out.h"

static void stamp(char *buf, size_t n) {
  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);
  strftime(buf, n, "%Y-%m-%d %H:%M", &tm);
}

static void xml_escape(FILE *f, const char *s) {
  for (; *s; s++) {
    switch (*s) {
    case '&': fputs("&amp;", f); break;
    case '<': fputs("&lt;", f); break;
    case '>': fputs("&gt;", f); break;
    case '"': fputs("&quot;", f); break;
    case '\'': fputs("&apos;", f); break;
    default: fputc(*s, f); break;
    }
  }
}

void format_out_init(FILE *f, out_fmt_t fmt, const char *invocation) {
  char ts[24];
  stamp(ts, sizeof ts);
  switch (fmt) {
  case OUT_M3U:
    fprintf(f, "#EXTM3U\n# %s UTC\n# %s\n\n", ts, invocation);
    break;
  case OUT_XSPF:
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n", f);
    fprintf(f, "  <title>%s UTC</title>\n", ts);
    break;
  case OUT_CSV:
  case OUT_XML:
  case OUT_NULL:
    break;
  }
}

void format_out_item(FILE *f, out_fmt_t fmt, const sds_service_t *s) {
  const char *scheme = s->rtp ? "rtp" : "udp";
  switch (fmt) {
  case OUT_M3U:
    fprintf(f, "#EXTINF:-1 tsid=\"%u\" onid=\"%u\" sid=\"%u\",%s\n", s->tsid, s->onid, s->sid, s->name);
    if (s->family == AF_INET6)
      fprintf(f, "%s://@[%s]:%u\n", scheme, s->address, s->port);
    else
      fprintf(f, "%s://@%s:%u\n", scheme, s->address, s->port);
    break;
  case OUT_CSV:
    fprintf(f, "%s,%s://@%s:%u,%u,%u,%u\n", s->name, scheme, s->address, s->port, s->tsid, s->onid, s->sid);
    break;
  case OUT_XSPF:
    fputs("  <track><location>", f);
    if (s->family == AF_INET6)
      fprintf(f, "%s://@[%s]:%u", scheme, s->address, s->port);
    else
      fprintf(f, "%s://@%s:%u", scheme, s->address, s->port);
    fputs("</location><title>", f);
    xml_escape(f, s->name);
    fprintf(f, "</title><extension application=\"urn:dvbipitools:dvb-triplet\" tsid=\"%u\" onid=\"%u\" sid=\"%u\"/></track>\n", s->tsid, s->onid, s->sid);
    break;
  case OUT_XML:
  case OUT_NULL:
    break;
  }
}

void format_out_close(FILE *f, out_fmt_t fmt) {
  switch (fmt) {
  case OUT_M3U: fputs("\n#EXT-X-ENDLIST\n", f); break;
  case OUT_XSPF: fputs("</playlist>\n", f); break;
  case OUT_CSV:
  case OUT_XML:
  case OUT_NULL: break;
  }
}

void format_out_raw(FILE *f, out_fmt_t fmt, const unsigned char *data, size_t len) {
  if (fmt != OUT_XML)
    return;
  fwrite(data, 1, len, f);
  fputc('\n', f);
}
