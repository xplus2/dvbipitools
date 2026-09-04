/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#include "format_out.h"
#include "lib/helper/playlist_out.h"

void format_out_init(FILE *f, out_fmt_t fmt, const char *invocation) {
  playlist_out_init(f, (playlist_out_fmt_t)fmt, invocation, "");
}

void format_out_item(FILE *f, out_fmt_t fmt, const sds_service_t *s) {
  const char *scheme = s->rtp ? "rtp" : "udp";
  char uri[300];

  if (fmt == OUT_M3U || fmt == OUT_CSV || fmt == OUT_XSPF) {
    if (s->family == AF_INET6)
      snprintf(uri, sizeof uri, "%s://@[%s]:%u", scheme, s->address, s->port);
    else
      snprintf(uri, sizeof uri, "%s://@%s:%u", scheme, s->address, s->port);
  }

  switch (fmt) {
  case OUT_M3U:
    playlist_out_m3u_item(f, s->name, uri, NULL, s->tsid, s->onid, s->sid);
    break;
  case OUT_CSV:
    playlist_out_csv_item(f, s->name, uri, s->tsid, s->onid, s->sid);
    break;
  case OUT_XSPF:
    playlist_out_xspf_item(f, s->name, uri, NULL, s->tsid, s->onid, s->sid);
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
