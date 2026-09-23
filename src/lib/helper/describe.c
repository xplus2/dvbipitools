/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>

#include "describe.h"
#include "ioutil.h"

static void add_host_port(sbuf_t *b, int family, const char *host, unsigned port) {
  if (family == AF_INET6) sbuf_add(b, "[");
  sbuf_add(b, host);
  if (family == AF_INET6) sbuf_add(b, "]");
  sbuf_add(b, ":");
  sbuf_add_uint(b, port);
}

void describe_mcast_uri(char *buf, size_t n, const char *scheme, int family, const char *group, unsigned port) {
  sbuf_t b;
  sbuf_init(&b, buf, n);
  sbuf_add(&b, scheme);
  sbuf_add(&b, "://@");
  add_host_port(&b, family, group, port);
}

void describe_http_uri(char *buf, size_t n, int tls, const char *host, unsigned port, const char *path) {
  sbuf_t b;
  sbuf_init(&b, buf, n);
  sbuf_add(&b, tls ? "https://" : "http://");
  sbuf_add(&b, host);
  sbuf_add(&b, ":");
  sbuf_add_uint(&b, port);
  sbuf_add(&b, path);
}

void describe_srt_uri(char *buf, size_t n, int family, int listen, const char *host, unsigned port) {
  sbuf_t b;
  sbuf_init(&b, buf, n);
  sbuf_add(&b, listen ? "srt://@" : "srt://");
  add_host_port(&b, family, host, port);
}
