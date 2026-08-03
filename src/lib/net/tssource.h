/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_TSSOURCE_H
#define DVBIPITOOLS_LIB_NET_TSSOURCE_H

#include <stddef.h>
#include <sys/types.h>

#include "httpclient.h"
#include "multicast.h"

typedef enum { TSSRC_RTP, TSSRC_UDP, TSSRC_STDIN, TSSRC_HTTP, TSSRC_UDPXY } tssrc_kind_t;

typedef struct {
  tssrc_kind_t kind;
  /* TSSRC_RTP / TSSRC_UDP */
  int family; /* AF_INET or AF_INET6 */
  const char *group;
  unsigned port;
  const char *iface; /* NULL = kernel default route */
  /* TSSRC_HTTP */
  http_url_t http;
  int insecure_tls; /* skip TLS verification */
  /* TSSRC_UDPXY */
  const char *udpxy_host;
  unsigned udpxy_port;
  const char *udpxy_path;
  /* TSSRC_HTTP / TSSRC_UDPXY; NULL = "dvbipitools" */
  const char *user_agent;
} tssrc_cfg_t;

typedef struct tssrc tssrc_t;

/* opens per cfg->kind. NULL on failure (logged) */
tssrc_t *tssrc_open(const tssrc_cfg_t *cfg);

/* TS bytes, RTP payload unwrapped if present. >0 len, 0 transient (retry), -1 hard error/EOF */
ssize_t tssrc_read(tssrc_t *s, unsigned char *buf, size_t cap);

/* underlying multicast handle, for a caller layering its own repair logic (e.g.
   dipirec's RET NACK client) on top of the joined group. NULL unless the kind is
   TSSRC_RTP/TSSRC_UDP. */
mcast_t *tssrc_mcast(tssrc_t *s);

void tssrc_close(tssrc_t *s);

#endif
