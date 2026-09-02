/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../demux/rtp.h"

#include "httpclient/httpclient.h"
#include "multicast.h"
#include "rist/ristin.h"
#include "srt/srtsrc.h"
#include "tssource.h"

/* max plausible RTP/MPEGTS record under a 1500-byte MTU: 12 + 7*188 */
#define TSSRC_RTP_MAX_RECORD (12 + 7 * 188)
/* bytes buffered before deciding raw vs RTP framing, TSSRC_STDIN/TSSRC_FILE */
#define TSSRC_DETECT_CAP (2 * TSSRC_RTP_MAX_RECORD + 20)
#define TSSRC_RAWBUF_CAP 65536

typedef enum { DEFRAME_DETECT, DEFRAME_RAW, DEFRAME_RTP } deframe_state_t;

struct tssrc {
  tssrc_kind_t kind;
  mcast_t *m;
  http_t *h;
  ristin_t *rist;
  srtsrc_t *srt;
  int fd; /* TSSRC_FILE */
  /* TSSRC_STDIN/TSSRC_FILE byte-stream framing */
  deframe_state_t deframe_state;
  unsigned char rawbuf[TSSRC_RAWBUF_CAP];
  size_t raw_len;
  size_t rtp_stride; /* DEFRAME_RTP: 12 + 188*N */
  size_t rtp_pos;     /* DEFRAME_RTP: offset within current stride, carried across reads */
  uint32_t last_rtp_ts;
};

tssrc_t *tssrc_open(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out) {
  const char *ua = cfg->user_agent ? cfg->user_agent : "dvbipitools";
  tssrc_t *s = calloc(1, sizeof *s);
  if (!s) {
    if (reason_out)
      *reason_out = NET_ERR_OTHER;
    return NULL;
  }
  s->kind = cfg->kind;
  switch (s->kind) {
  case TSSRC_RTP:
  case TSSRC_UDP:
    s->m = mcast_open(cfg->family, cfg->group, cfg->port, cfg->iface, 1000);
    if (!s->m) {
      free(s);
      if (reason_out)
        *reason_out = NET_ERR_CONNECT;
      return NULL;
    }
    return s;
  case TSSRC_HTTP:
    s->h = http_get(&cfg->http, ua, cfg->insecure_tls, NULL, reason_out);
    if (!s->h) {
      free(s);
      return NULL;
    }
    return s;
  case TSSRC_STDIN:
    return s;
  case TSSRC_FILE:
    s->fd = open(cfg->file_path, O_RDONLY);
    if (s->fd < 0) {
      free(s);
      if (reason_out)
        *reason_out = NET_ERR_OTHER;
      return NULL;
    }
    return s;
  case TSSRC_RIST: {
    ristin_cfg_t rc;
    memset(&rc, 0, sizeof rc);
    rc.peer_uri = cfg->rist_uri;
    rc.profile = cfg->rist_profile_main ? RISTIN_PROFILE_MAIN : RISTIN_PROFILE_SIMPLE;
    rc.secret = cfg->rist_secret;
    rc.cname = cfg->rist_cname;
    rc.buffer_ms = cfg->rist_buffer_ms;
    rc.verbose = cfg->rist_verbose;
    rc.mx = cfg->rist_mx;
    rc.tool_version = cfg->rist_tool_version;
    s->rist = ristin_open(&rc);
    if (!s->rist) {
      free(s);
      if (reason_out)
        *reason_out = NET_ERR_CONNECT;
      return NULL;
    }
    return s;
  }
  case TSSRC_SRT: {
    srtsrc_cfg_t sc;
    memset(&sc, 0, sizeof sc);
    sc.host = cfg->srt_host;
    sc.port = cfg->srt_port;
    sc.listen = cfg->srt_listen;
    sc.passphrase = cfg->srt_passphrase;
    sc.pbkeylen = cfg->srt_pbkeylen;
    sc.streamid = cfg->srt_streamid;
    sc.packetfilter = cfg->srt_packetfilter;
    sc.latency_ms = cfg->srt_latency_ms;
    sc.verbose = cfg->srt_verbose;
    sc.mx = cfg->srt_mx;
    sc.tool_version = cfg->srt_tool_version;
    s->srt = srtsrc_open(&sc);
    if (!s->srt) {
      free(s);
      if (reason_out)
        *reason_out = NET_ERR_CONNECT;
      return NULL;
    }
    return s;
  }
  }
  free(s);
  return NULL;
}

static ssize_t raw_fd_read(int fd, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  ssize_t n = read(fd, buf, cap);
  if (n == 0) {
    if (reason_out)
      *reason_out = NET_ERR_EOF;
    return -1;
  }
  if (n < 0) {
    if (errno == EINTR)
      return 0;
    if (reason_out)
      *reason_out = NET_ERR_READ;
    return -1;
  }
  return n;
}

/* raw: sync at every 188. RTP: v2 header, N (1..7) TS packets in sync,
   next record's first TS packet in sync too. else: raw, matches old
   TSSRC_STDIN behavior. */
/* checks that the first nn 188-byte TS packets in this candidate RTP-framed stride
   are sync-aligned (0x47 at their start) */
static int rtp_stride_candidate_ok(const unsigned char *b, int nn) {
  for (size_t k = 0; k < (size_t)nn; k++)
    if (b[12 + 188 * k] != 0x47)
      return 0;
  return 1;
}

static void detect_framing(tssrc_t *s) {
  const unsigned char *b = s->rawbuf;
  size_t n = s->raw_len;

  if (n >= 3 * 188 && b[0] == 0x47 && b[188] == 0x47 && b[376] == 0x47) {
    s->deframe_state = DEFRAME_RAW;
    return;
  }
  if (n >= 12 + 188 && (b[0] >> 6) == 2) {
    for (int nn = 1; nn <= 7; nn++) {
      size_t stride = 12 + (size_t)nn * 188;
      if (n < stride + 12 + 1)
        break;
      if (rtp_stride_candidate_ok(b, nn) && b[stride + 12] == 0x47) {
        s->deframe_state = DEFRAME_RTP;
        s->rtp_stride = stride;
        s->rtp_pos = 0;
        return;
      }
    }
  }
  s->deframe_state = DEFRAME_RAW;
}

/* strips leading 12 bytes of every rtp_stride window from s->rawbuf, into
   dst up to dstcap. advances rtp_pos across calls, shifts leftover to
   front. returns bytes written. */
static size_t rtp_deframe_step(tssrc_t *s, unsigned char *dst, size_t dstcap) {
  size_t i = 0, o = 0;

  while (i < s->raw_len && o < dstcap) {
    size_t in_stride = s->rtp_pos % s->rtp_stride;
    if (in_stride < 12) {
      size_t need = 12 - in_stride;
      size_t avail = s->raw_len - i;
      if (in_stride == 0 && avail >= 12)
        s->last_rtp_ts = ((uint32_t)s->rawbuf[i + 4] << 24) | ((uint32_t)s->rawbuf[i + 5] << 16) |
                          ((uint32_t)s->rawbuf[i + 6] << 8) | s->rawbuf[i + 7];
      if (need > avail)
        need = avail;
      i += need;
      s->rtp_pos += need;
      continue;
    }
    {
      size_t chunk = s->rtp_stride - in_stride;
      size_t avail = s->raw_len - i;
      size_t room = dstcap - o;
      size_t take = chunk < avail ? chunk : avail;
      if (take > room)
        take = room;
      memcpy(dst + o, s->rawbuf + i, take);
      i += take;
      o += take;
      s->rtp_pos += take;
    }
  }
  memmove(s->rawbuf, s->rawbuf + i, s->raw_len - i);
  s->raw_len -= i;
  return o;
}

/* TSSRC_STDIN/TSSRC_FILE byte-stream read: detect once, then raw passthrough or RTP header strip after.
   uses local reason always (dipirec's own caller passes reason_out NULL).
   EOF-vs-error branch below needs it anyway */
/* handles a failed raw_fd_read() during framing detection. 1: EOF with data buffered
   (caller should detect framing from what's there). 0: real error (reason_out set) */
static int handle_detect_read_error(tssrc_t *s, net_err_reason_t r, net_err_reason_t *reason_out) {
  if (r == NET_ERR_EOF && s->raw_len > 0)
    return 1;
  if (reason_out)
    *reason_out = r;
  return 0;
}

static ssize_t deframe_read(tssrc_t *s, int fd, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  net_err_reason_t r;

  if (s->deframe_state == DEFRAME_DETECT) {
    ssize_t got = raw_fd_read(fd, s->rawbuf + s->raw_len, TSSRC_DETECT_CAP - s->raw_len, &r);
    if (got < 0) {
      if (!handle_detect_read_error(s, r, reason_out))
        return -1;
      detect_framing(s);
    } else {
      s->raw_len += (size_t)got;
      if (s->raw_len < TSSRC_DETECT_CAP)
        return 0;
      detect_framing(s);
    }
  }

  if (s->deframe_state == DEFRAME_RAW) {
    ssize_t got;
    if (s->raw_len > 0) {
      size_t take = s->raw_len < cap ? s->raw_len : cap;
      memcpy(buf, s->rawbuf, take);
      memmove(s->rawbuf, s->rawbuf + take, s->raw_len - take);
      s->raw_len -= take;
      return (ssize_t)take;
    }
    got = raw_fd_read(fd, buf, cap, &r);
    if (got < 0 && reason_out)
      *reason_out = r;
    return got;
  }

  {
    size_t out = rtp_deframe_step(s, buf, cap);
    if (out == 0) {
      ssize_t got = raw_fd_read(fd, s->rawbuf + s->raw_len, sizeof s->rawbuf - s->raw_len, &r);
      if (got < 0) {
        if (reason_out)
          *reason_out = r;
        return -1;
      }
      s->raw_len += (size_t)got;
      out = rtp_deframe_step(s, buf, cap);
    }
    return (ssize_t)out;
  }
}

ssize_t tssrc_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  switch (s->kind) {
  case TSSRC_RTP:
  case TSSRC_UDP: {
    ssize_t n = mcast_recv(s->m, buf, cap, reason_out);
    size_t off;
    if (n <= 0)
      return n;
    off = rtp_payload_offset(buf, (size_t)n);
    if (off) {
      /* offset+len contract instead of copy: needs a tssrc_read() API change across both tools, saves a sub-us memmove. not worth it. */
      memmove(buf, buf + off, (size_t)n - off);
      n -= (ssize_t)off;
    }
    return n;
  }
  case TSSRC_HTTP:
    return http_read(s->h, buf, cap, reason_out);
  case TSSRC_STDIN:
    return deframe_read(s, STDIN_FILENO, buf, cap, reason_out);
  case TSSRC_FILE:
    return deframe_read(s, s->fd, buf, cap, reason_out);
  case TSSRC_RIST:
    return raw_fd_read(ristin_fd(s->rist), buf, cap, reason_out);
  case TSSRC_SRT:
    return raw_fd_read(srtsrc_fd(s->srt), buf, cap, reason_out);
  }
  return -1;
}

int tssrc_is_rtp_framed(const tssrc_t *s) { return s->deframe_state == DEFRAME_RTP; }
uint32_t tssrc_last_rtp_ts(const tssrc_t *s) { return s->last_rtp_ts; }

void tssrc_rewind(tssrc_t *s) {
  int fd;

  if (s->kind != TSSRC_FILE && s->kind != TSSRC_STDIN)
    return;
  fd = (s->kind == TSSRC_FILE) ? s->fd : STDIN_FILENO;
  if (lseek(fd, 0, SEEK_SET) < 0)
    return;
  s->raw_len = 0;
  s->rtp_pos = 0;
}

mcast_t *tssrc_mcast(tssrc_t *s) { return s->m; }

int tssrc_fd(const tssrc_t *s) {
  switch (s->kind) {
  case TSSRC_RTP:
  case TSSRC_UDP:
    return mcast_fd(s->m);
  case TSSRC_HTTP:
    return http_fd(s->h);
  case TSSRC_STDIN:
    return STDIN_FILENO;
  case TSSRC_FILE:
    return s->fd;
  case TSSRC_RIST:
    return ristin_fd(s->rist);
  case TSSRC_SRT:
    return srtsrc_fd(s->srt);
  }
  return -1;
}

void tssrc_close(tssrc_t *s) {
  if (!s)
    return;
  if (s->m)
    mcast_close(s->m);
  if (s->h)
    http_close(s->h);
  if (s->rist)
    ristin_close(s->rist);
  if (s->srt)
    srtsrc_close(s->srt);
  if (s->kind == TSSRC_FILE)
    close(s->fd);
  free(s);
}

struct tssrc_open {
  http_async_t *ha; /* TSSRC_HTTP only, during PENDING */
  tssrc_t *result;  /* set once done, any kind */
  int done;
};

tssrc_open_t *tssrc_open_async_start(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out) {
  tssrc_open_t *o = calloc(1, sizeof *o);
  if (!o)
    return NULL;
  if (cfg->kind == TSSRC_HTTP) {
    const char *ua = cfg->user_agent ? cfg->user_agent : "dvbipitools";
    o->ha = http_async_start(&cfg->http, ua, cfg->insecure_tls, NULL, reason_out);
    if (!o->ha) {
      free(o);
      return NULL;
    }
    return o;
  }
  o->result = tssrc_open(cfg, reason_out); /* RTP/UDP/STDIN/FILE. cheap, local-only, done synchronously */
  if (!o->result) {
    free(o);
    return NULL;
  }
  o->done = 1;
  return o;
}

int tssrc_open_async_poll_fd(const tssrc_open_t *o) {
  if (o->ha)
    return http_async_poll_fd(o->ha);
  return -1;
}

short tssrc_open_async_poll_events(const tssrc_open_t *o) {
  if (o->ha)
    return http_async_poll_events(o->ha);
  return 0;
}

tssrc_open_state_t tssrc_open_async_step(tssrc_open_t *o, net_err_reason_t *reason_out) {
  http_async_state_t st;

  if (o->done)
    return TSSRC_OPEN_DONE;

  st = http_async_step(o->ha, reason_out);
  if (st == HTTP_ASYNC_PENDING)
    return TSSRC_OPEN_PENDING;
  if (st == HTTP_ASYNC_ERROR) {
    http_async_free(o->ha);
    o->ha = NULL;
    return TSSRC_OPEN_ERROR;
  }

  o->result = calloc(1, sizeof *o->result);
  if (!o->result) {
    http_close(http_async_take(o->ha));
    o->ha = NULL;
    if (reason_out)
      *reason_out = NET_ERR_OTHER;
    return TSSRC_OPEN_ERROR;
  }
  o->result->kind = TSSRC_HTTP;
  o->result->h = http_async_take(o->ha);
  o->ha = NULL;
  o->done = 1;
  return TSSRC_OPEN_DONE;
}

tssrc_t *tssrc_open_async_take(tssrc_open_t *o) {
  tssrc_t *r = o->result;
  free(o);
  return r;
}

void tssrc_open_async_free(tssrc_open_t *o) {
  if (!o)
    return;
  if (o->ha)
    http_async_free(o->ha);
  if (o->result)
    tssrc_close(o->result);
  free(o);
}
