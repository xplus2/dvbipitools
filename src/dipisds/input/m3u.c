/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/playlist_in.h"
#include "lib/helper/log.h"
#include "lib/helper/xml_util.h"
#include "dipisds/input.h"
#include "dipisds/input/input_int.h"

int load_m3u(FILE *f, input_t *in) {
  char line[2048];
  char pending_name[SDS_MAX_NAME];
  unsigned pending_tsid = 1;
  unsigned pending_onid = 1;
  unsigned pending_sid = 0;
  int have_pending = 0;
  int idx = 0;
  int lineno = 0;
  int rc;

  while ((rc = read_line(f, line, sizeof line)) != 0) {
    sds_service_t *s;
    char *body;
    lineno++;
    if (rc < 0) {
      log_code("AE400205", "line %d: too long (max %zu)", lineno, sizeof line - 1);
      return -1;
    }
    chomp(line);
    body = skip_bom(line, lineno);
    if (!body[0]) continue;
    if (!strncmp(body, "#EXTINF:", 8)) {
      const char *comma = extinf_comma(body);
      char tmp[32];
      if (!comma) {
        log_code("AE400111", "malformed #EXTINF line: %s", body);
        return -1;
      }
      bufcpy(pending_name, sizeof pending_name, comma + 1);
      pending_tsid = 1;
      pending_onid = 1;
      pending_sid = 0;
      if ((xml_attr_list(body, comma, "tsid", tmp, sizeof tmp) == 0 && dvb_id("tsid", tmp, &pending_tsid)) ||
          (xml_attr_list(body, comma, "onid", tmp, sizeof tmp) == 0 && dvb_id("onid", tmp, &pending_onid)) ||
          (xml_attr_list(body, comma, "sid", tmp, sizeof tmp) == 0 && dvb_id("sid", tmp, &pending_sid))) {
        log_code("AE400216", "line %d: bad service id field", lineno);
        return -1;
      }
      have_pending = 1;
      continue;
    }
    if (body[0] == '#' || !have_pending) continue;
    if (idx >= SDS_MAX_SERVICES) {
      log_code("AE000225", "too many entries (max %d)", SDS_MAX_SERVICES);
      return -1;
    }
    s = &in->services[idx];
    memset(s, 0, sizeof *s);
    bufcpy(s->name, sizeof s->name, pending_name);
    if (parse_mcast_uri(body, s)) {
      log_code("AE400112", "bad uri: %s", body);
      return -1;
    }
    s->tsid = pending_tsid;
    s->onid = pending_onid;
    s->sid = pending_sid ? pending_sid : (unsigned)(idx + 1);
    idx++;
    have_pending = 0;
  }
  in->service_count = idx;
  return 0;
}
