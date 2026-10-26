/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "dipisds/input.h"
#include "dipisds/input/input_int.h"

int load_csv(FILE *f, input_t *in) {
  char line[512];
  int idx = 0;
  int lineno = 0;
  int rc;

  while ((rc = read_line(f, line, sizeof line)) != 0) {
    char *fields[5];
    char *body;
    size_t nf;
    sds_service_t *s;
    lineno++;
    if (rc < 0) {
      log_code("AE400205", "line %d: too long (max %zu)", lineno, sizeof line - 1);
      return -1;
    }
    chomp(line);
    if (!(body = csv_line_body(line, lineno))) continue;
    nf = csv_split(body, fields, 5);
    if (nf < 2) {
      log_code("AE000224", "line %d: expected name,uri", lineno);
      return -1;
    }
    if (idx >= SDS_MAX_SERVICES) {
      log_code("AE000225", "too many entries (max %d)", SDS_MAX_SERVICES);
      return -1;
    }
    s = &in->services[idx];
    memset(s, 0, sizeof *s);
    bufcpy(s->name, sizeof s->name, fields[0]);
    if (parse_mcast_uri(fields[1], s)) {
      log_code("AE400110", "line %d: bad uri: %s", lineno, fields[1]);
      return -1;
    }
    s->tsid = 1;
    s->onid = 1;
    s->sid = (unsigned)(idx + 1);
    if ((nf > 2 && dvb_id("tsid", fields[2], &s->tsid)) || (nf > 3 && dvb_id("onid", fields[3], &s->onid)) ||
        (nf > 4 && dvb_id("sid", fields[4], &s->sid))) {
      log_code("AE400216", "line %d: bad service id field", lineno);
      return -1;
    }
    idx++;
  }
  in->service_count = idx;
  return 0;
}
