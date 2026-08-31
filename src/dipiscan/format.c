/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "format.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/playlist_out.h"
#include "lib/helper/sds_xml.h"

void format_init(FILE *f, out_fmt_t fmt, const char *invocation, const char *provider) {
  playlist_out_init(f, (playlist_out_fmt_t)fmt, invocation, "dipiscan ");
  if (fmt == OUT_XML)
    sds_broadcast_open(f, provider, 1);
}

void format_item(FILE *f, out_fmt_t fmt, const char *name, const char *uri, int family, const char *group, unsigned port, int rtp, unsigned tsid, unsigned onid, unsigned sid) {
  switch (fmt) {
    case OUT_M3U:
      playlist_out_m3u_item(f, name, uri, tsid, onid, sid);
      break;
    case OUT_CSV:
      playlist_out_csv_item(f, name, uri, tsid, onid, sid);
      break;
    case OUT_XSPF:
      playlist_out_xspf_item(f, name, uri, tsid, onid, sid);
      break;
    case OUT_XML: {
      sds_service_t s;
      memset(&s, 0, sizeof s);
      bufcpy(s.name, sizeof s.name, name);
      bufcpy(s.address, sizeof s.address, group);
      s.family = family;
      s.port = port;
      s.rtp = rtp;
      s.tsid = tsid;
      s.onid = onid;
      s.sid = sid;
      sds_broadcast_item(f, &s, NULL, NULL);
      break;
    }
    case OUT_NULL:
      break;
  }
}

void format_close(FILE *f, out_fmt_t fmt) {
  playlist_out_close(f, (playlist_out_fmt_t)fmt);
  if (fmt == OUT_XML)
    sds_broadcast_close(f);
}
