/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <string.h>

#include "format_out.h"
#include "lib/playlist_out.h"
#include "lib/xml_util.h"

void format_out_init(FILE *f, out_fmt_t fmt, const char *invocation) {
  playlist_out_init(f, (playlist_out_fmt_t)fmt, invocation, "");
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
  case OUT_CSV: {
    /* comma is the field separator, keep it out of the name */
    const char *p;
    for (p = s->name; *p; p++)
      if (*p != ',')
        fputc(*p, f);
    fprintf(f, ",%s://@%s:%u,%u,%u,%u\n", scheme, s->address, s->port, s->tsid, s->onid, s->sid);
    break;
  }
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
  playlist_out_close(f, (playlist_out_fmt_t)fmt);
}

void format_out_raw(FILE *f, out_fmt_t fmt, const unsigned char *data, size_t len) {
  if (fmt != OUT_XML)
    return;
  fwrite(data, 1, len, f);
  fputc('\n', f);
}
