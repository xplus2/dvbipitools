/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <string.h>

#include "lib/helper/uriparse.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "dipisds/input/input_int.h"

const char *suffix(const char *path) {
  const char *dot = strrchr(path, '.');
  const char *slash = strrchr(path, '/');
  if (!dot || (slash && dot < slash)) return "";
  return dot;
}

char *skip_bom(char *line, int lineno) {
  return lineno == 1 && !strncmp(line, "\xEF\xBB\xBF", 3) ? line + 3 : line;
}

char *csv_line_body(char *line, int lineno) {
  line = skip_bom(line, lineno);
  while (*line == ' ' || *line == '\t') line++;
  return *line && *line != '#' ? line : NULL;
}

/* rtp://[[src]@]<addr>:<port> or udp://..., [addr6] for v6. fills address/source/family/port */
int parse_mcast_uri(const char *uri, sds_service_t *s) {
  if (!strncmp(uri, "rtp://", 6)) s->rtp = 1;
  else if (!strncmp(uri, "udp://", 6)) s->rtp = 0;
  else return -1;
  return uriparse_mcast_src_addrport(uri + 6, &s->family, s->address, sizeof s->address, &s->port, s->source, sizeof s->source);
}

int dvb_id(const char *name, const char *v, unsigned *out) {
  unsigned long n;
  if (parse_ulong(v, 65535, &n)) {
    log_code("AE400215", "bad %s (0..65535): %s", name, v);
    return -1;
  }
  *out = (unsigned)n;
  return 0;
}
