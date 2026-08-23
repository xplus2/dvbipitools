/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/net/netconnect.h"
#include "lib/net/rtmp/rtmp.h"
#include "lib/net/tls.h"
#include "lib/signal.h"
#include "rtmpout.h"

#define RTMPOUT_RETRY_S 3
#define RTMPOUT_READBUF 4096
#define RTMPOUT_SEQHDR_MAX 2048

typedef enum { RTMPOUT_DOWN, RTMPOUT_TCP_CONNECTING, RTMPOUT_TLS_HANDSHAKING, RTMPOUT_PROTOCOL } rtmpout_phase_t;

struct rtmpout {
  char host[256];
  unsigned port;
  char app[128], stream_key[256], tcurl[512];
  char user[128], pass[128];
  int use_tls, insecure;

  rtmpout_phase_t phase;
  int fd;
  netconnect_pending_t *connect_pending;
  tls_t *tls;
  rtmp_t *rtmp;
  int ready, failed;
  double next_retry;

  int need_keyframe;
  unsigned char meta[512];
  size_t meta_len;
  unsigned char vseq[RTMPOUT_SEQHDR_MAX], aseq[RTMPOUT_SEQHDR_MAX];
  size_t vseq_len, aseq_len;
};

static void rtmp_transport_write(void *ctx, const unsigned char *data, size_t len) {
  rtmpout_t *o = ctx;
  size_t sent = 0;

  while (sent < len) {
    ssize_t n = o->tls ? tls_write(o->tls, data + sent, len - sent) : write(o->fd, data + sent, len - sent);
    if (n > 0) {
      sent += (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR)
      continue;
    o->failed = 1; /* real error, or would-block on a message this small: treat both as connection-dead */
    return;
  }
}

static void on_ready(void *ctx) {
  rtmpout_t *o = ctx;
  o->ready = 1;
  o->need_keyframe = 1;
  if (o->meta_len)
    rtmp_send_data(o->rtmp, o->meta, o->meta_len);
  if (o->vseq_len)
    rtmp_send_video(o->rtmp, 0, o->vseq, o->vseq_len);
  if (o->aseq_len)
    rtmp_send_audio(o->rtmp, 0, o->aseq, o->aseq_len);
}

static void on_error(void *ctx, const char *msg) {
  (void)msg;
  ((rtmpout_t *)ctx)->failed = 1;
}

static void teardown(rtmpout_t *o) {
  if (o->rtmp) {
    rtmp_free(o->rtmp);
    o->rtmp = NULL;
  }
  if (o->tls) {
    tls_close(o->tls);
    o->tls = NULL;
  }
  if (o->fd >= 0) {
    close(o->fd);
    o->fd = -1;
  }
  o->phase = RTMPOUT_DOWN;
  o->ready = 0;
  o->failed = 0;
  o->next_retry = mono_seconds() + RTMPOUT_RETRY_S;
}

static void try_connect(rtmpout_t *o) {
  net_err_reason_t reason;
  o->fd = netconnect_tcp_start(o->host, o->port, &o->connect_pending, &reason);
  if (o->fd < 0) {
    o->next_retry = mono_seconds() + RTMPOUT_RETRY_S;
    return;
  }
  o->phase = RTMPOUT_TCP_CONNECTING;
}

static void start_rtmp(rtmpout_t *o) {
  rtmp_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.app = o->app;
  cfg.tcurl = o->tcurl;
  cfg.stream_name = o->stream_key;
  cfg.user = o->user;
  cfg.password = o->pass;
  cfg.write_cb = rtmp_transport_write;
  cfg.ready_cb = on_ready;
  cfg.error_cb = on_error;
  cfg.cb_ctx = o;
  o->rtmp = rtmp_new(&cfg);
  if (!o->rtmp) {
    teardown(o);
    return;
  }
  o->phase = RTMPOUT_PROTOCOL;
  rtmp_start(o->rtmp);
}

/* reads one chunk (via TLS or plain fd) and feeds it to rtmp_feed().
   1: got chunk, caller keeps looping. 0: transient/EAGAIN/short read, caller stops.
   -1: fatal for this connection, caller stops and marks o->failed */
static int protocol_read_step(rtmpout_t *o) {
  unsigned char buf[RTMPOUT_READBUF];
  ssize_t n;

  if (o->tls) {
    n = tls_read(o->tls, buf, sizeof buf);
    if (n < 0)
      return -1;
    if (0 == n)
      return 0; /* transient */
  } else {
    n = read(o->fd, buf, sizeof buf);
    if (0 == n)
      return -1; /* peer closed */
    if (n < 0) {
      if (EAGAIN == errno || EWOULDBLOCK == errno)
        return 0;
      return -1;
    }
  }
  if (rtmp_feed(o->rtmp, buf, (size_t)n) != 0)
    return -1;
  return (size_t)n < sizeof buf ? 0 : 1;
}

static void service(rtmpout_t *o) {
  double now = mono_seconds();
  struct pollfd pfd;

  if (o->failed) {
    teardown(o);
    return;
  }

  switch (o->phase) {
    case RTMPOUT_DOWN:
      if (now >= o->next_retry)
        try_connect(o);
      return;

    case RTMPOUT_TCP_CONNECTING: {
      net_err_reason_t reason;
      int cr;
      pfd.fd = o->fd;
      pfd.events = POLLOUT;
      if (poll(&pfd, 1, 0) <= 0)
        return;
      cr = netconnect_tcp_finish(&o->connect_pending, &o->fd, &reason);
      if (cr == 0)
        return; /* this address failed, next candidate connecting on o->fd */
      if (cr != 1) {
        o->fd = -1;
        o->phase = RTMPOUT_DOWN;
        o->next_retry = now + RTMPOUT_RETRY_S;
        return;
      }
      if (!o->use_tls) {
        start_rtmp(o);
        return;
      }
      o->tls = tls_connect_start(o->fd, o->host, o->insecure);
      if (!o->tls) {
        close(o->fd);
        o->fd = -1;
        o->phase = RTMPOUT_DOWN;
        o->next_retry = now + RTMPOUT_RETRY_S;
        return;
      }
      o->phase = RTMPOUT_TLS_HANDSHAKING;
      return;
    }

    case RTMPOUT_TLS_HANDSHAKING: {
      tls_handshake_status_t st = tls_handshake_step(o->tls);
      if (TLS_HANDSHAKE_DONE == st)
        start_rtmp(o);
      else if (TLS_HANDSHAKE_ERROR == st) {
        tls_close(o->tls);
        o->tls = NULL;
        close(o->fd);
        o->fd = -1;
        o->phase = RTMPOUT_DOWN;
        o->next_retry = now + RTMPOUT_RETRY_S;
      }
      return;
    }

    case RTMPOUT_PROTOCOL: {
      pfd.fd = o->fd;
      pfd.events = POLLIN;
      if (poll(&pfd, 1, 0) <= 0)
        return;
      for (;;) {
        int r = protocol_read_step(o);
        if (r < 0) {
          o->failed = 1;
          break;
        }
        if (r == 0)
          break;
      }
      if (o->failed)
        teardown(o);
      return;
    }
  }
}

static int is_video_key(const unsigned char *d, size_t n) {
  return n >= 1 && ((d[0] >> 4) & 0x07) == 1;
}

static void cache_tag(unsigned char *dst, size_t *dst_len, size_t cap, const unsigned char *data, size_t len) {
  if (len > cap)
    return;
  memcpy(dst, data, len);
  *dst_len = len;
}

int rtmpout_write(rtmpout_t *o, flv_tag_type_t type, uint32_t timestamp_ms, const unsigned char *data, size_t len) {
  if (FLV_TAG_SCRIPT == type) {
    cache_tag(o->meta, &o->meta_len, sizeof o->meta, data, len);
  } else if (FLV_TAG_VIDEO == type && len >= 2) {
    int is_seqhdr = (data[0] & 0x80) ? (data[0] & 0x0F) == 0 : data[1] == 0;
    if (is_seqhdr)
      cache_tag(o->vseq, &o->vseq_len, sizeof o->vseq, data, len);
  } else if (FLV_TAG_AUDIO == type && len >= 2) {
    int is_seqhdr = ((data[0] >> 4) == 9) ? (data[0] & 0x0F) == 0 : data[1] == 0;
    if (is_seqhdr)
      cache_tag(o->aseq, &o->aseq_len, sizeof o->aseq, data, len);
  }

  service(o);
  if (RTMPOUT_PROTOCOL != o->phase || !o->ready)
    return -1;

  if (FLV_TAG_VIDEO == type) {
    if (o->need_keyframe) {
      if (!is_video_key(data, len))
        return 0;
      o->need_keyframe = 0;
    }
    return rtmp_send_video(o->rtmp, timestamp_ms, data, len);
  }
  if (FLV_TAG_AUDIO == type)
    return rtmp_send_audio(o->rtmp, timestamp_ms, data, len);
  return rtmp_send_data(o->rtmp, data, len);
}

/* rtmp(s)://[user[:pass]@]host[:port]/app/key, key = final path segment */
static int parse_url(const char *url, rtmpout_t *o) {
  const char *p = url;
  const char *host_end, *at, *colon, *last_slash;
  size_t hostlen, applen;

  if (0 == strncmp(p, "rtmps://", 8)) {
    o->use_tls = 1;
    o->port = 443;
    p += 8;
  } else if (0 == strncmp(p, "rtmp://", 7)) {
    o->use_tls = 0;
    o->port = 1935;
    p += 7;
  } else {
    return -1;
  }

  host_end = strchr(p, '/');
  if (!host_end || host_end == p)
    return -1;

  o->user[0] = o->pass[0] = '\0';
  at = NULL;
  for (colon = p; colon < host_end; colon++) /* last '@': allows literal '@' in password */
    if (*colon == '@')
      at = colon;
  if (at) {
    const char *upcolon = memchr(p, ':', (size_t)(at - p));
    size_t ulen = upcolon ? (size_t)(upcolon - p) : (size_t)(at - p);
    if (!ulen || ulen >= sizeof o->user)
      return -1;
    memcpy(o->user, p, ulen);
    o->user[ulen] = '\0';
    if (upcolon) {
      size_t plen = (size_t)(at - upcolon - 1);
      if (plen >= sizeof o->pass)
        return -1;
      memcpy(o->pass, upcolon + 1, plen);
      o->pass[plen] = '\0';
    }
    p = at + 1;
  }

  colon = memchr(p, ':', (size_t)(host_end - p));
  hostlen = colon ? (size_t)(colon - p) : (size_t)(host_end - p);
  if (!hostlen || hostlen >= sizeof o->host)
    return -1;
  memcpy(o->host, p, hostlen);
  o->host[hostlen] = '\0';
  if (colon) {
    char portbuf[8];
    size_t n = (size_t)(host_end - colon - 1);
    if (!n || n >= sizeof portbuf)
      return -1;
    memcpy(portbuf, colon + 1, n);
    portbuf[n] = '\0';
    o->port = (unsigned)strtoul(portbuf, NULL, 10);
  }

  p = host_end + 1;
  if (!*p)
    return -1;
  last_slash = strrchr(p, '/');
  if (last_slash) {
    applen = (size_t)(last_slash - p);
    if (applen >= sizeof o->app)
      return -1;
    memcpy(o->app, p, applen);
    o->app[applen] = '\0';
    bufcpy(o->stream_key, sizeof o->stream_key, last_slash + 1);
  } else {
    bufcpy(o->app, sizeof o->app, p);
    o->stream_key[0] = '\0';
  }

  snprintf(o->tcurl, sizeof o->tcurl, "%s://%s:%u/%s", o->use_tls ? "rtmps" : "rtmp", o->host, o->port, o->app);
  return 0;
}

/* redacts userinfo before logging, url may embed user:pass@ */
static void log_invalid_url(const char *url) {
  const char *scheme_end = strstr(url, "://");
  const char *at = scheme_end ? strchr(scheme_end, '@') : NULL;
  if (at)
    log_line("rtmpout: invalid url: %.*s[redacted]@%s", (int)(scheme_end - url + 3), url, at + 1);
  else
    log_line("rtmpout: invalid url: %s", url);
}

rtmpout_t *rtmpout_open(const rtmpout_cfg_t *cfg) {
  rtmpout_t *o = calloc(1, sizeof *o);
  if (!o)
    return NULL;
  if (parse_url(cfg->url, o)) {
    log_invalid_url(cfg->url);
    free(o);
    return NULL;
  }
  o->insecure = cfg->insecure;
  o->fd = -1;
  o->phase = RTMPOUT_DOWN;
  o->need_keyframe = 1;
  o->next_retry = mono_seconds();
  return o;
}

void rtmpout_close(rtmpout_t *o) {
  if (!o)
    return;
  if (o->rtmp)
    rtmp_free(o->rtmp);
  if (o->tls)
    tls_close(o->tls);
  if (o->connect_pending)
    netconnect_tcp_abort(o->connect_pending);
  if (o->fd >= 0)
    close(o->fd);
  free(o);
}
