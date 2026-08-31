/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "uriparse.h"

#include <arpa/inet.h>
#include <stdio.h>
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

void uriparse_mcast_describe(int family, const char *group, unsigned port, char *buf, size_t n) {
  if (family == AF_INET6)
    snprintf(buf, n, "[%s]:%u", group, port);
  else
    snprintf(buf, n, "%s:%u", group, port);
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
