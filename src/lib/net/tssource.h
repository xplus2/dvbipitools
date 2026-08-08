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

/* opens per cfg->kind. NULL on failure (logged). reason_out: nullable, set only on NULL return */
tssrc_t *tssrc_open(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out);

/* TS bytes, RTP payload unwrapped if present. >0 len, 0 transient (retry), -1 hard error/EOF.
   reason_out: nullable, set only on -1 */
ssize_t tssrc_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out);

/* underlying multicast handle, for a caller layering its own repair logic (e.g. dipirec's RET NACK client)
   on top of the joined group. NULL unless the kind is TSSRC_RTP/TSSRC_UDP. */
mcast_t *tssrc_mcast(tssrc_t *s);

/* underlying fd, for a caller's own poll(); valid for the life of s. -1 for TSSRC_UDPXY (no
   fd accessor exists there yet - unreachable from any tool's CLI today, so not added). */
int tssrc_fd(const tssrc_t *s);

void tssrc_close(tssrc_t *s);

typedef enum { TSSRC_OPEN_PENDING, TSSRC_OPEN_DONE, TSSRC_OPEN_ERROR } tssrc_open_state_t;
typedef struct tssrc_open tssrc_open_t;

/* async tssrc_open(): never blocks caller's thread. TSSRC_RTP/UDP/STDIN/UDPXY complete on first step()
   regardless (their setup is cheap, local-only work. joining mcasts, or stdin).
   only TSSRC_HTTP genuinely spans multiple steps (connect+TLS handshake+header read).
   No internal timeout, caller decides when to give up. NULL only on immediate setup failure (calloc - logged). */
tssrc_open_t *tssrc_open_async_start(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out);

int tssrc_open_async_poll_fd(const tssrc_open_t *o);
short tssrc_open_async_poll_events(const tssrc_open_t *o);
/* reason_out: nullable, set only on TSSRC_OPEN_ERROR */
tssrc_open_state_t tssrc_open_async_step(tssrc_open_t *o, net_err_reason_t *reason_out);

/* DONE only: hands over the tssrc_t, frees the async handle */
tssrc_t *tssrc_open_async_take(tssrc_open_t *o);

/* frees handle + owned state; safe at any state incl. PENDING */
void tssrc_open_async_free(tssrc_open_t *o);

#endif
