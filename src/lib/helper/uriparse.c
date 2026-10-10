/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "uriparse.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#include "argutil.h"
#include "../sys/ioutil.h"

int uriparse_mcast_addrport(const char *rest, int *family, char *group, size_t groupsz, unsigned *port) {
  if (argutil_addrport_parse(rest, family, group, groupsz, port)) return -1;
  if (*family == AF_INET) {
    struct in_addr a;
    inet_pton(AF_INET, group, &a);
    if ((ntohl(a.s_addr) >> 28) != 0xE) return -1; /* 224.0.0.0/4 */
  } else {
    struct in6_addr a6;
    inet_pton(AF_INET6, group, &a6);
    if (a6.s6_addr[0] != 0xFF) return -1; /* ff00::/8 */
  }
  return 0;
}

int uriparse_mcast_src_addrport(const char *rest, int *family, char *group, size_t groupsz, unsigned *port, char *src, size_t srcsz) {
  const char *at = strchr(rest, '@');
  char tmp[64];
  size_t len;
  const char *sp;
  src[0] = '\0';
  if (!at) return uriparse_mcast_addrport(rest, family, group, groupsz, port);
  if (at == rest) return uriparse_mcast_addrport(rest + 1, family, group, groupsz, port);
  if (uriparse_mcast_addrport(at + 1, family, group, groupsz, port)) return -1;
  sp = rest;
  len = (size_t)(at - rest);
  if (len > 2 && rest[0] == '[' && at[-1] == ']') {
    sp++;
    len -= 2;
  }
  if (len >= sizeof tmp || len >= srcsz) return -1;
  memcpy(tmp, sp, len);
  tmp[len] = '\0';
  if (*family == AF_INET) {
    struct in_addr a;
    if (inet_pton(AF_INET, tmp, &a) != 1) return -1;
    if ((ntohl(a.s_addr) >> 28) == 0xE || a.s_addr == 0) return -1;
  } else {
    struct in6_addr a6;
    if (inet_pton(AF_INET6, tmp, &a6) != 1) return -1;
    if (a6.s6_addr[0] == 0xFF || IN6_IS_ADDR_UNSPECIFIED(&a6)) return -1;
  }
  memcpy(src, tmp, len + 1);
  return 0;
}

void uriparse_mcast_src_uri(char *buf, size_t n, const char *scheme, int family, const char *src, const char *group, unsigned port) {
  sbuf_t b;
  sbuf_init(&b, buf, n);
  sbuf_add(&b, scheme);
  sbuf_add(&b, "://");
  if (src && src[0]) sbuf_add(&b, src);
  sbuf_add(&b, "@");
  if (family == AF_INET6) sbuf_add(&b, "[");
  sbuf_add(&b, group);
  if (family == AF_INET6) sbuf_add(&b, "]");
  sbuf_add(&b, ":");
  sbuf_add_uint(&b, port);
}

void uriparse_mcast_describe(int family, const char *group, unsigned port, char *buf, size_t n) {
  sbuf_t b;
  sbuf_init(&b, buf, n);
  if (family == AF_INET6) sbuf_add(&b, "[");
  sbuf_add(&b, group);
  if (family == AF_INET6) sbuf_add(&b, "]");
  sbuf_add(&b, ":");
  sbuf_add_uint(&b, port);
}

int uriparse_rtmp_or_file(const char *uri, char *rtmp_buf, size_t rtmp_cap, char *file_buf, size_t file_cap) {
  if (strncmp(uri, "rtmps://", 8) == 0 || strncmp(uri, "rtmp://", 7) == 0) {
    if (strlen(uri) >= rtmp_cap) return -1;
    bufcpy(rtmp_buf, rtmp_cap, uri);
    return uri[4] == 's' ? 2 : 1;
  }
  if (strlen(uri) >= file_cap) return -1;
  bufcpy(file_buf, file_cap, uri);
  return 0;
}
