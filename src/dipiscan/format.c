/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>
#include <time.h>

#include "format.h"
#include "lib/sds_xml.h"
#include "lib/xml_util.h"

static void stamp(char *buf, size_t n) {
  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);
  strftime(buf, n, "%Y-%m-%d %H:%M", &tm);
}

void format_init(FILE *f, out_fmt_t fmt, const char *invocation, const char *provider) {
  char ts[24];
  stamp(ts, sizeof ts);
  switch (fmt) {
    case OUT_M3U:
      fprintf(f, "#EXTM3U\n# %s UTC\n# %s\n\n", ts, invocation);
      break;
    case OUT_XSPF:
      fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n", f);
      fprintf(f, "  <title>dipiscan %s UTC</title>\n", ts);
      break;
    case OUT_XML:
      sds_broadcast_open(f, provider, 1);
      break;
    case OUT_CSV:
    case OUT_NULL:
      break;
  }
}

void format_item(FILE *f, out_fmt_t fmt, const char *name, const char *uri, int family, const char *group, unsigned port, int rtp, unsigned tsid, unsigned onid, unsigned sid) {
  switch (fmt) {
    case OUT_M3U:
      fprintf(f, "#EXTINF:-1 tsid=\"%u\" onid=\"%u\" sid=\"%u\",%s\n%s\n", tsid, onid, sid, name, uri);
      break;
    case OUT_CSV: {
      /* comma is the field separator, keep it out of the name */
      const char *p;
      for (p = name; *p; p++)
        if (*p != ',')
          fputc(*p, f);
      fprintf(f, ",%s,%u,%u,%u\n", uri, tsid, onid, sid);
      break;
    }
    case OUT_XSPF:
      fputs("  <track><location>", f);
      xml_escape(f, uri);
      fputs("</location><title>", f);
      xml_escape(f, name);
      fprintf(f, "</title><extension application=\"urn:dvbipitools:dvb-triplet\" tsid=\"%u\" onid=\"%u\" sid=\"%u\"/></track>\n", tsid, onid, sid);
      break;
    case OUT_XML: {
      sds_service_t s;
      memset(&s, 0, sizeof s);
      snprintf(s.name, sizeof s.name, "%s", name);
      snprintf(s.address, sizeof s.address, "%s", group);
      s.family = family;
      s.port = port;
      s.rtp = rtp;
      s.tsid = tsid;
      s.onid = onid;
      s.sid = sid;
      sds_broadcast_item(f, &s, NULL);
      break;
    }
    case OUT_NULL:
      break;
  }
}

void format_close(FILE *f, out_fmt_t fmt) {
  switch (fmt) {
    case OUT_M3U:       fputs("\n#EXT-X-ENDLIST\n", f); break;
    case OUT_XSPF:      fputs("</playlist>\n", f);      break;
    case OUT_XML:       sds_broadcast_close(f);          break;
    case OUT_CSV:
    case OUT_NULL:      break;
  }
}
