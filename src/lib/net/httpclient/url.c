/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../helper/log.h"
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

/* RFC 3986 5.2.4. path starts with '/', optional ?query kept */
static int remove_dot_segments(const char *path, char *out, size_t cap) {
  size_t plen = strcspn(path, "?");
  const char *end = path + plen;
  const char *s = path;
  size_t o = 0;
  if (plen + strlen(path + plen) >= cap) return -1;
  while (s < end) {
    const char *e = s + 1;
    size_t seglen;
    int last;
    while (e < end && *e != '/') e++;
    seglen = (size_t)(e - (s + 1));
    last = e == end;
    if (seglen == 1 && s[1] == '.') {
      if (last) out[o++] = '/';
    } else if (seglen == 2 && s[1] == '.' && s[2] == '.') {
      while (o > 0 && out[o - 1] != '/') o--;
      if (o > 0) o--;
      if (last) out[o++] = '/';
    } else {
      memcpy(out + o, s, (size_t)(e - s));
      o += (size_t)(e - s);
    }
    s = e;
  }
  if (o == 0) out[o++] = '/';
  memcpy(out + o, end, strlen(end) + 1);
  return 0;
}

/* RFC 3986 scheme: ALPHA *( ALPHA / DIGIT / + - . ) ':' */
static int has_scheme(const char *s) {
  if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))) return 0;
  for (s++; (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '+' || *s == '-' || *s == '.'; s++) {
  }
  return *s == ':';
}

int resolve_location(http_url_t *u, const char *loc) {
  char ref[sizeof u->path];
  char merged[sizeof u->path];
  char norm[sizeof u->path];
  size_t n;
  if (!strncmp(loc, "http://", 7) || !strncmp(loc, "https://", 8))
    return http_url_parse(loc, u);
  if (!loc[0] || has_scheme(loc)) return -1;
  n = strcspn(loc, "#");
  if (n >= sizeof ref) return -1;
  memcpy(ref, loc, n);
  ref[n] = '\0';
  if (ref[0] == '/' && ref[1] == '/') {
    char full[sizeof u->path + 8];
    if (snprintf(full, sizeof full, "%s:%s", u->tls ? "https" : "http", ref) >= (int)sizeof full) return -1;
    return http_url_parse(full, u);
  }
  if (ref[0] == '\0') return 0; /* fragment only */
  if (ref[0] == '/') {
    bufcpy(merged, sizeof merged, ref);
  } else {
    size_t base_len = strcspn(u->path, "?");
    if (ref[0] != '?') {
      while (base_len > 0 && u->path[base_len - 1] != '/') base_len--;
    }
    if (base_len + strlen(ref) >= sizeof merged) return -1;
    memcpy(merged, u->path, base_len);
    memcpy(merged + base_len, ref, strlen(ref) + 1);
  }
  if (remove_dot_segments(merged, norm, sizeof norm) != 0) return -1;
  bufcpy(u->path, sizeof u->path, norm);
  return 0;
}

int check_redirect_scheme(const http_url_t *from, const http_url_t *to, net_err_reason_t *reason_out) {
  if (!from->tls || to->tls) return 0;
  log_code("LE300173", "http: refusing https to http redirect to %s", to->host);
  if (reason_out) *reason_out = NET_ERR_HTTP;
  return -1;
}
