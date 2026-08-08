/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "cas_args.h"

#include <stdlib.h>
#include <string.h>

int cas_super_id_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v == 0 || v > 0xFFFFFFFFUL)
    return -1;
  *out = (unsigned)v;
  return 0;
}

int cas_endpoint_parse(const char *s, char *host_out, size_t host_out_sz, unsigned *port_out) {
  const char *p = s, *host, *colon;
  size_t hostlen;
  char *end;
  unsigned long port;

  if (!strncmp(p, "tcp://", 6))
    p += 6;
  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close)
      return -1;
    host = p + 1;
    hostlen = (size_t)(close - host);
    if (close[1] != ':')
      return -1;
    colon = close + 1;
  } else {
    host = p;
    colon = strrchr(p, ':');
    if (!colon)
      return -1;
    hostlen = (size_t)(colon - host);
  }
  if (hostlen == 0 || hostlen >= host_out_sz)
    return -1;
  memcpy(host_out, host, hostlen);
  host_out[hostlen] = '\0';

  port = strtoul(colon + 1, &end, 10);
  if (end == colon + 1 || port == 0 || port > 65535)
    return -1;
  if (*end != '\0' && *end != '/')
    return -1;
  *port_out = (unsigned)port;
  return 0;
}

int cas_version_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 10);
  if (*end != '\0' || (v != 2 && v != 3))
    return -1;
  *out = (unsigned)v;
  return 0;
}
