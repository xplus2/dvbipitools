/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_TS_SOURCE_H
#define DVBIPITOOLS_LIB_NET_TS_SOURCE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "../httpclient/httpclient.h"
#include "../multicast.h"
#include "../rist/ristin.h"

typedef enum { TSSRC_RTP, TSSRC_UDP, TSSRC_STDIN, TSSRC_HTTP, TSSRC_FILE, TSSRC_RIST, TSSRC_SRT } tssrc_kind_t;

typedef struct {
  tssrc_kind_t kind;
  /* TSSRC_RTP / TSSRC_UDP */
  int family; /* AF_INET or AF_INET6 */
  const char *group;
  const char *source; /* SSM source, NULL or "" = ASM */
  unsigned port;
  const char *iface; /* NULL = kernel default route */
  unsigned al_fec_l;
  unsigned al_fec_d;
  unsigned al_fec_port;
  /* TSSRC_HTTP */
  http_url_t http;
  int insecure_tls; /* skip TLS verification */
  /* TSSRC_FILE */
  const char *file_path;
  /* TSSRC_HTTP. NULL = "dvbipitools" */
  const char *user_agent;
  /* TSSRC_RIST */
  const char *rist_uri;    /* rist://@host:port[?query], single peer, @ required */
  int rist_profile_main;   /* 0 = simple (default), 1 = main */
  const char *rist_secret; /* profile main only */
  int rist_key_size;       /* 0 = default, else 128/256 */
  const char *rist_cname;
  unsigned rist_buffer_ms;
  int rist_verbose;
  metrics_exporter_t *rist_mx;   /* NULL = no stats push */
  const char *rist_tool_version; /* required if rist_mx set */
  /* TSSRC_SRT: single peer, no bonding/rendezvous (dipisrt for that) */
  int srt_listen;                /* 1 = @, bind/listen/accept. 0 = call out (connect) */
  const char *srt_host;
  unsigned srt_port;
  const char *srt_passphrase;    /* NULL/"" = no encryption */
  int srt_pbkeylen;              /* 0 = default (16), else 16/24/32 */
  const char *srt_streamid;      /* NULL/"" = none */
  const char *srt_packetfilter;  /* NULL/"" = none, raw SRTO_PACKETFILTER string */
  unsigned srt_latency_ms;       /* 0 = library default */
  unsigned srt_connect_timeout_ms; /* caller connect. 0 = 3000 */
  int srt_verbose;
  metrics_exporter_t *srt_mx;    /* NULL = no stats push */
  const char *srt_tool_version;  /* required if srt_mx set */
  /* RTP/UDP/RIST/SRT de-jitter ms. 0 = off. RTP reorders, rest delays. tssrc_fd() also wakes for due packets. >1500 B dropped */
  unsigned jitter_ms;
} tssrc_cfg_t;

typedef struct tssrc tssrc_t;

/* opens per cfg->kind. NULL on failure. reason_out: nullable, set only on NULL return */
tssrc_t *tssrc_open(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out);

/* TS bytes, RTP payload unwrapped if present. >0 len, 0 transient (retry), -1 hard error/EOF.
   reason_out: nullable, set only on -1.
   TSSRC_STDIN/TSSRC_FILE: raw vs RTP-wrapped auto-detected from first bytes, locked in after.
   RTP framing stripped here too, once detected. */
ssize_t tssrc_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out);

int tssrc_enable_rx_timestamps(tssrc_t *s);
uint64_t tssrc_last_rx_ns(const tssrc_t *s);

/* de-jitter fill in ms, -1 = no de-jitter buffer */
int64_t tssrc_buffer_ms(const tssrc_t *s);

/* TSSRC_STDIN/TSSRC_FILE, valid after first tssrc_read(): 1 if RTP-wrapped.
   0 before that, and always for every other kind. */
int tssrc_is_rtp_framed(const tssrc_t *s);

/* TSSRC_STDIN/TSSRC_FILE + tssrc_is_rtp_framed(): 90kHz ts of most recently deframed record. undefined before first tssrc_read(). */
uint32_t tssrc_last_rtp_ts(const tssrc_t *s);

/* underlying multicast handle, for a caller layering its own repair logic (e.g. dipirec's RET NACK client)
   on top of the joined group. NULL unless the kind is TSSRC_RTP/TSSRC_UDP. */
mcast_t *tssrc_mcast(const tssrc_t *s);

/* underlying fd, for caller's own poll(). valid for life of s. */
int tssrc_fd(const tssrc_t *s);

/* events to poll tssrc_fd() for. POLLIN except HLS/DASH mid-connect */
short tssrc_poll_events(const tssrc_t *s);

/* 1: tssrc_fd() alone cannot wake caller (HLS/DASH idle on timers, or several media). caller must call
   tssrc_read() every tick + cap its poll to TSSRC_TIMER_WAIT_MS. tssrc_read() returns 0 when nothing due */
int tssrc_timer_driven(const tssrc_t *s);
#define TSSRC_TIMER_WAIT_MS 20

/* blocks up to timeout_ms. 1: tssrc_read() likely has data. 0: timeout/EINTR. -1: poll error.
   unlike poll(tssrc_fd()): HLS/DASH covers every media, connect (POLLOUT), buffered data, timer-driven idle */
int tssrc_wait(const tssrc_t *s, int timeout_ms);

/* TSSRC_UDP/TSSRC_RTP: socket non-blocking, tssrc_read() returns 0 when empty so callers may drain. 1 on success, else 0 */
int tssrc_set_nonblock(tssrc_t *s);

int tssrc_fec_fd(const tssrc_t *s);
void tssrc_fec_poll(tssrc_t *s);

/* start recording everything tssrc_read() returns (64 MiB max, then recording is abandoned).
   pair with tssrc_rewind() after a probe that consumed the head of the stream */
void tssrc_capture_start(tssrc_t *s);

/* 1 while tssrc_read() still serves recorded bytes. callers reading the transport directly must drain via tssrc_read() first */
int tssrc_replay_pending(const tssrc_t *s);

/* seekable TSSRC_FILE/TSSRC_STDIN: rewinds to byte 0, keeps raw/RTP framing decided so far.
   anything else: replays bytes recorded since tssrc_capture_start() on following reads, then continues live.
   1: stream restarts at the probed head. 0: head lost (not recording, nothing read, or cap exceeded) */
int tssrc_rewind(tssrc_t *s);

void tssrc_close(tssrc_t *s);

typedef enum { TSSRC_OPEN_PENDING, TSSRC_OPEN_DONE, TSSRC_OPEN_ERROR } tssrc_open_state_t;
typedef struct tssrc_open tssrc_open_t;

/* async tssrc_open(): never block caller's thread. TSSRC_RTP/UDP/STDIN/FILE complete on first step():
   cheap, local-only: joining mcasts, stdin, open().
   only TSSRC_HTTP spans multiple steps (connect+TLS handshake+header read).
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

/* blocking open that classifies HTTP content (HLS/DASH/TS). timeout_s bounds HTTP only.
   NULL on failure, timeout (NET_ERR_TIMEOUT) or stop request. reason_out: nullable */
tssrc_t *tssrc_open_blocking(const tssrc_cfg_t *cfg, double timeout_s, net_err_reason_t *reason_out);

#endif
