/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "uriparse.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <string.h>

#include "argutil.h"
#include "ioutil.h"

int uriparse_mcast_addrport(const char *rest, int *family, char *group, size_t groupsz, unsigned *port) {
  if (argutil_addrport_parse(rest, family, group, groupsz, port))
    return -1;

  if (*family == AF_INET) {
    struct in_addr a;
    inet_pton(AF_INET, group, &a);
    if ((ntohl(a.s_addr) >> 28) != 0xE) /* 224.0.0.0/4 */
      return -1;
  } else {
    struct in6_addr a6;
    inet_pton(AF_INET6, group, &a6);
    if (a6.s6_addr[0] != 0xFF) /* ff00::/8 */
      return -1;
  }
  return 0;
}

int uriparse_udpxy(const char *rest, char *host_out, size_t host_cap, unsigned *port_out, int *rtp_wrapped_out, char *path_out, size_t path_cap) {
  const char *p = rest;
  const char *seg, *segend;
  size_t len;

  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close)
      return -1;
    len = (size_t)(close - (p + 1));
    if (len == 0 || len >= host_cap)
      return -1;
    memcpy(host_out, p + 1, len);
    host_out[len] = '\0';
    p = close + 1;
  } else {
    const char *hp = p;
    while (*hp && *hp != ':' && *hp != '/')
      hp++;
    len = (size_t)(hp - p);
    if (len == 0 || len >= host_cap)
      return -1;
    memcpy(host_out, p, len);
    host_out[len] = '\0';
    p = hp;
  }

  if (*p == ':') {
    const char *pe = ++p;
    char portbuf[6];
    while (isdigit((unsigned char)*pe))
      pe++;
    len = (size_t)(pe - p);
    if (len == 0 || len >= sizeof portbuf)
      return -1;
    memcpy(portbuf, p, len);
    portbuf[len] = '\0';
    if (argutil_port_parse(portbuf, port_out))
      return -1;
    p = pe;
  } else {
    *port_out = 80;
  }

  if (*p != '/')
    return -1;

  seg = p + 1;
  segend = strchr(seg, '/');
  len = segend ? (size_t)(segend - seg) : strlen(seg);
  if (len == 3 && memcmp(seg, "rtp", 3) == 0)
    *rtp_wrapped_out = 1;
  else if (len == 3 && memcmp(seg, "udp", 3) == 0)
    *rtp_wrapped_out = 0;
  else
    return -1;

  len = strlen(p);
  if (len >= path_cap)
    return -1;
  memcpy(path_out, p, len + 1);
  return 0;
}

int uriparse_rtmp_or_file(const char *uri, char *rtmp_buf, size_t rtmp_cap, char *file_buf, size_t file_cap) {
  if (strncmp(uri, "rtmps://", 8) == 0 || strncmp(uri, "rtmp://", 7) == 0) {
    if (strlen(uri) >= rtmp_cap)
      return -1;
    bufcpy(rtmp_buf, rtmp_cap, uri);
    return uri[4] == 's' ? 2 : 1;
  }
  if (strlen(uri) >= file_cap)
    return -1;
  bufcpy(file_buf, file_cap, uri);
  return 0;
}
