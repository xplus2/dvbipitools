/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "../../ioutil.h"

#include "priv.h"

int http_url_parse(const char *uri, http_url_t *u) {
  const char *p = uri, *host, *rest;
  size_t hostlen;
  memset(u, 0, sizeof *u);
  if (!strncmp(p, "https://", 8)) {
    u->tls = 1;
    u->port = 443;
    p += 8;
  } else if (!strncmp(p, "http://", 7)) {
    u->tls = 0;
    u->port = 80;
    p += 7;
  } else {
    return -1;
  }
  if (!*p)
    return -1;

  host = p;
  rest = strpbrk(p, ":/");
  hostlen = rest ? (size_t)(rest - host) : strlen(host);
  if (hostlen == 0 || hostlen >= sizeof u->host)
    return -1;
  memcpy(u->host, host, hostlen);
  u->host[hostlen] = '\0';

  if (rest && *rest == ':') {
    char *end;
    unsigned long port = strtoul(rest + 1, &end, 10);
    if (end == rest + 1 || port == 0 || port > 65535)
      return -1;
    u->port = (unsigned)port;
    rest = strchr(rest, '/');
  }
  if (rest) {
    if (bufcpy(u->path, sizeof u->path, rest) >= sizeof u->path)
      return -1;
  } else {
    bufcpy(u->path, sizeof u->path, "/");
  }
  return 0;
}

int resolve_location(http_url_t *u, const char *loc) {
  if (!strncmp(loc, "http://", 7) || !strncmp(loc, "https://", 8))
    return http_url_parse(loc, u);
  if (loc[0] == '/') {
    if (bufcpy(u->path, sizeof u->path, loc) >= sizeof u->path)
      return -1;
    return 0;
  }
  return -1;
}
