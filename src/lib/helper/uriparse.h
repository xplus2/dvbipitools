/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_URIPARSE_H
#define DVBIPITOOLS_LIB_HELPER_URIPARSE_H

#include <stddef.h>

/* rest: addr:port or [addr6]:port, multicast required (224.0.0.0/4, ff00::/8). caller strips leading '@' first. */
int uriparse_mcast_addrport(const char *rest, int *family, char *group, size_t groupsz, unsigned *port);
int uriparse_mcast_cfg(const char *v, int *family, char *group, size_t groupsz, unsigned *port, char *err, size_t errsz);

/* like uriparse_mcast_addrport() with optional SSM source: [@]addr:port or src@addr:port.
   src: unicast literal of group's family, bare or [bracketed]. "" absent. */
int uriparse_mcast_src_addrport(const char *rest, int *family, char *group, size_t groupsz, unsigned *port, char *src, size_t srcsz);

/* scheme://[src]@group:port, [group] when family is AF_INET6. src NULL or "" = ASM */
void uriparse_mcast_src_uri(char *buf, size_t n, const char *scheme, int family, const char *src, const char *group, unsigned port);

/* group:port, or [group]:port when family is AF_INET6 */
void uriparse_mcast_describe(int family, const char *group, unsigned port, char *buf, size_t n);

/* classifies uri as rtmp/rtmps/file, copies into matching buffer.
   returns 0 = file, 1 = rtmp, 2 = rtmps, -1 if uri doesn't fit target buffer. */
int uriparse_rtmp_or_file(const char *uri, char *rtmp_buf, size_t rtmp_cap, char *file_buf, size_t file_cap);

/* malloc'd copy of uri without userinfo, query, fragment. empty userinfo ("rtp://@host") kept. NULL on oom */
char *uriparse_redact_dup(const char *uri);

/* RFC3986 urlencode path segment. NUL-terminates */
void uriparse_pct_encode_seg(const char *s, char *out, size_t outcap);

/* %XX -> byte, decoded in place. 0 ok, -1 malformed escape */
int uriparse_pct_decode(char *s);

#endif
