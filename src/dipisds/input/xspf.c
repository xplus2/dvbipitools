/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/xml_util.h"
#include "dipisds/input.h"
#include "dipisds/input/input_int.h"

typedef struct {
  input_t *in;
  int idx;
} xspf_ctx_t;

static int xspf_track_cb(const char *tag, const char *end, void *vctx) {
  xspf_ctx_t *c = vctx;
  input_t *in = c->in;
  char loc[2 * SDS_MAX_ADDR + 16];
  char title[SDS_MAX_NAME];
  char tmp[32];
  sds_service_t *s;

  if (c->idx >= SDS_MAX_SERVICES) {
    log_code("AE000225", "too many entries (max %d)", SDS_MAX_SERVICES);
    return -1;
  }
  if (xml_elem_text(tag, end, "location", loc, sizeof loc)) {
    log_code("AE000226", "xspf track missing <location>");
    return -1;
  }
  if (xml_elem_text(tag, end, "title", title, sizeof title)) title[0] = '\0';

  s = &in->services[c->idx];
  memset(s, 0, sizeof *s);
  bufcpy(s->name, sizeof s->name, title);
  if (parse_mcast_uri(loc, s)) {
    log_code("AE400112", "bad uri: %s", loc);
    return -1;
  }
  s->tsid = 1;
  s->onid = 1;
  s->sid = (unsigned)(c->idx + 1);
  if ((xml_attr(tag, end, "tsid", tmp, sizeof tmp) == 0 && dvb_id("tsid", tmp, &s->tsid)) ||
      (xml_attr(tag, end, "onid", tmp, sizeof tmp) == 0 && dvb_id("onid", tmp, &s->onid)) ||
      (xml_attr(tag, end, "sid", tmp, sizeof tmp) == 0 && dvb_id("sid", tmp, &s->sid))) {
    log_code("AE400217", "xspf track %d: bad service id field", c->idx + 1);
    return -1;
  }
  c->idx++;
  return 0;
}

int load_xspf(input_t *in, const unsigned char *buf) {
  const char *p = (const char *)buf;
  xspf_ctx_t c = {in, 0};
  if (for_each_xml_elem(p, p + strlen(p), "track", xspf_track_cb, &c)) return -1;
  in->service_count = c.idx;
  return 0;
}

static int count_cb(const char *tag, const char *blk_end, void *ctx) {
  (void)tag;
  (void)blk_end;
  ++*(int *)ctx;
  return 0;
}

int count_xml_elems(const char *buf, size_t len, const char *name) {
  int n = 0;
  for_each_xml_elem(buf, buf + len, name, count_cb, &n);
  return n;
}
