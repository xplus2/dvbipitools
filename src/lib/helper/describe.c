/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdio.h>

#include "describe.h"

void describe_mcast_uri(char *buf, size_t n, const char *scheme, int family, const char *group, unsigned port) {
  if (family == AF_INET6)
    snprintf(buf, n, "%s://@[%s]:%u", scheme, group, port);
  else
    snprintf(buf, n, "%s://@%s:%u", scheme, group, port);
}

void describe_http_uri(char *buf, size_t n, int tls, const char *host, unsigned port, const char *path) {
  snprintf(buf, n, "%s://%s:%u%s", tls ? "https" : "http", host, port, path);
}

void describe_srt_uri(char *buf, size_t n, int family, int listen, const char *host, unsigned port) {
  if (family == AF_INET6)
    snprintf(buf, n, "srt://%s[%s]:%u", listen ? "@" : "", host, port);
  else
    snprintf(buf, n, "srt://%s%s:%u", listen ? "@" : "", host, port);
}
