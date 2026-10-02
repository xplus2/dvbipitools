/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "../../sys/ioutil.h"

#include "priv.h"

int http_url_parse(const char *uri, http_url_t *u) {
  const char *p = uri;
  const char *host;
  const char *rest;
  size_t hostlen;
  http_url_t out;
  memset(&out, 0, sizeof out);
  if (!strncmp(p, "https://", 8)) {
    out.tls = 1;
    out.port = 443;
    p += 8;
  } else if (!strncmp(p, "http://", 7)) {
    out.tls = 0;
    out.port = 80;
    p += 7;
  } else {
    return -1;
  }
  if (!*p) return -1;

  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close) return -1;
    host = p + 1;
    hostlen = (size_t)(close - host);
    rest = close + 1;
    if (*rest != '\0' && *rest != ':' && *rest != '/') return -1;
    if (*rest == '\0') rest = NULL;
  } else {
    host = p;
    rest = strpbrk(p, ":/");
    hostlen = rest ? (size_t)(rest - host) : strlen(host);
  }
  if (hostlen == 0 || hostlen >= sizeof out.host) return -1;
  memcpy(out.host, host, hostlen);
  out.host[hostlen] = '\0';

  if (rest && *rest == ':') {
    char *end;
    unsigned long port = strtoul(rest + 1, &end, 10);
    if (end == rest + 1 || port == 0 || port > 65535) return -1;
    out.port = (unsigned)port;
    rest = strchr(rest, '/');
  }
  if (rest) {
    if (bufcpy(out.path, sizeof out.path, rest) >= sizeof out.path) return -1;
  } else {
    bufcpy(out.path, sizeof out.path, "/");
  }
  *u = out;
  return 0;
}

int resolve_location(http_url_t *u, const char *loc) {
  if (!strncmp(loc, "http://", 7) || !strncmp(loc, "https://", 8))
    return http_url_parse(loc, u);
  if (loc[0] == '/') {
    char path[sizeof u->path];
    if (bufcpy(path, sizeof path, loc) >= sizeof path) return -1;
    memcpy(u->path, path, sizeof u->path);
    return 0;
  }
  return -1;
}
