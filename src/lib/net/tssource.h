/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_TSSOURCE_H
#define DVBIPITOOLS_LIB_NET_TSSOURCE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "httpclient/httpclient.h"
#include "multicast.h"

typedef enum { TSSRC_RTP, TSSRC_UDP, TSSRC_STDIN, TSSRC_HTTP, TSSRC_FILE } tssrc_kind_t;

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
  /* TSSRC_FILE */
  const char *file_path;
  /* TSSRC_HTTP. NULL = "dvbipitools" */
  const char *user_agent;
} tssrc_cfg_t;

typedef struct tssrc tssrc_t;

/* opens per cfg->kind. NULL on failure. reason_out: nullable, set only on NULL return */
tssrc_t *tssrc_open(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out);

/* TS bytes, RTP payload unwrapped if present. >0 len, 0 transient (retry), -1 hard error/EOF.
   reason_out: nullable, set only on -1.
   TSSRC_STDIN/TSSRC_FILE: raw vs RTP-wrapped auto-detected from first bytes, locked in after.
   RTP framing stripped here too, once detected. */
ssize_t tssrc_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out);

/* TSSRC_STDIN/TSSRC_FILE, valid after first tssrc_read(): 1 if RTP-wrapped.
   0 before that, and always for every other kind. */
int tssrc_is_rtp_framed(const tssrc_t *s);

/* TSSRC_STDIN/TSSRC_FILE + tssrc_is_rtp_framed(): 90kHz ts of most recently deframed record. undefined before first tssrc_read(). */
uint32_t tssrc_last_rtp_ts(const tssrc_t *s);

/* underlying multicast handle, for a caller layering its own repair logic (e.g. dipirec's RET NACK client)
   on top of the joined group. NULL unless the kind is TSSRC_RTP/TSSRC_UDP. */
mcast_t *tssrc_mcast(tssrc_t *s);

/* underlying fd, for caller's own poll(). valid for life of s. */
int tssrc_fd(const tssrc_t *s);

/* TSSRC_FILE/TSSRC_STDIN, seekable underlying fd only (best-effort, silent no-op otherwise):
   rewinds to byte 0, keeps already-decided raw/RTP framing.
   for a caller that probed PAT/PMT first, consuming bytes, before real reading starts */
void tssrc_rewind(tssrc_t *s);

void tssrc_close(tssrc_t *s);

typedef enum { TSSRC_OPEN_PENDING, TSSRC_OPEN_DONE, TSSRC_OPEN_ERROR } tssrc_open_state_t;
typedef struct tssrc_open tssrc_open_t;

/* async tssrc_open(): never blocks caller's thread. TSSRC_RTP/UDP/STDIN/FILE complete
   on first step() regardless (cheap, local-only work: joining mcasts, stdin, open()).
   only TSSRC_HTTP genuinely spans multiple steps (connect+TLS handshake+header read).
   No internal timeout, caller decides when to give up. NULL only on immediate setup failure */
tssrc_open_t *tssrc_open_async_start(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out);

int tssrc_open_async_poll_fd(const tssrc_open_t *o);
short tssrc_open_async_poll_events(const tssrc_open_t *o);
/* reason_out: nullable, set only on TSSRC_OPEN_ERROR */
tssrc_open_state_t tssrc_open_async_step(tssrc_open_t *o, net_err_reason_t *reason_out);

/* DONE only: hands over tssrc_t, frees async handle */
tssrc_t *tssrc_open_async_take(tssrc_open_t *o);

/* frees handle + owned state. safe at any state incl. PENDING */
void tssrc_open_async_free(tssrc_open_t *o);

#endif
