/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../dash/live.h"
#include "../dash/mpd.h"
#include "../demux/fec2022.h"
#include "../demux/rtp.h"
#include "../helper/ioutil.h"
#include "../hls/live.h"
#include "../hls/playlist.h"
#include "../hls/tspassthrough.h"
#include "../mux/esbuild/remux.h"

#include "httpclient/httpclient.h"
#include "multicast.h"
#include "rist/ristin.h"
#include "srt/srtsrc.h"
#include "tssource.h"

#define TSSRC_HTTP_SNIFF_CAP 2048
#define TSSRC_HTTP_MANIFEST_CAP (64 * 1024)
#define TSSRC_HTTP_INIT_CAP (256 * 1024)

typedef enum { HTTP_CONTENT_TS, HTTP_CONTENT_HLS, HTTP_CONTENT_DASH, HTTP_CONTENT_UNKNOWN } http_content_kind_t;

static int has_ts_sync(const unsigned char *b, size_t n) {
  return n >= 3 * 188 && b[0] == 0x47 && b[188] == 0x47 && b[376] == 0x47;
}

static http_content_kind_t classify_http_content(const unsigned char *b, size_t n) {
  size_t off = (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) ? 3 : 0;
  if (n - off >= 7 && !memcmp(b + off, "#EXTM3U", 7)) return HTTP_CONTENT_HLS;
  if (n - off >= 5 && !memcmp(b + off, "<?xml", 5) && memmem(b, n, "<MPD", 4)) return HTTP_CONTENT_DASH;
  if (has_ts_sync(b, n)) return HTTP_CONTENT_TS;
  return HTTP_CONTENT_UNKNOWN;
}

/* max plausible RTP/MPEGTS record under a 1500-byte MTU: 12 + 7*188 */
#define TSSRC_RTP_MAX_RECORD (12 + 7 * 188)
/* bytes buffered before deciding raw vs RTP framing, TSSRC_STDIN/TSSRC_FILE */
#define TSSRC_DETECT_CAP (2 * TSSRC_RTP_MAX_RECORD + 20)
#define TSSRC_RAWBUF_CAP 65536

typedef enum { DEFRAME_DETECT, DEFRAME_RAW, DEFRAME_RTP } deframe_state_t;

typedef enum { HTTP_SUB_TS, HTTP_SUB_HLS, HTTP_SUB_DASH } http_sub_kind_t;

struct tssrc;

typedef struct {
  struct tssrc *owner;
  unsigned stream_idx;
  int got_init;
} tssrc_media_ctx_t;

struct tssrc {
  tssrc_kind_t kind;
  mcast_t *m;
  mcast_t *fec_m;
  fec2022_dec_t *fec_dec;
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

  http_sub_kind_t http_sub;
  unsigned char http_sniff_buf[TSSRC_HTTP_SNIFF_CAP];
  size_t http_sniff_len;
  size_t http_sniff_pos;
  hls_live_t *hls[2];
  dash_live_t *dash[2];
  unsigned n_media;
  tssrc_media_ctx_t media_ctx[2];
  hls_ts_passthrough_t ts_pass;
  esbuild_remux_t remux;
};

static void hls_fmp4_emit_pkt(void *ctx, const unsigned char *pkt188) { hls_live_emit((hls_live_t *)ctx, pkt188, 188); }

static void hls_fmp4_segment_feed(void *ctx, hls_live_t *h, const unsigned char *data, size_t len) {
  tssrc_media_ctx_t *mc = ctx;
  esbuild_remux_feed(&mc->owner->remux, mc->stream_idx, data, len, hls_fmp4_emit_pkt, h);
}

static void dash_fmp4_emit_pkt(void *ctx, const unsigned char *pkt188) { dash_live_emit((dash_live_t *)ctx, pkt188, 188); }

static void dash_fmp4_segment_feed(void *ctx, dash_live_t *h, const unsigned char *data, size_t len) {
  tssrc_media_ctx_t *mc = ctx;
  if (!mc->got_init) {
    mc->got_init = 1;
    esbuild_remux_add_init(&mc->owner->remux, mc->stream_idx, data, len);
    return;
  }
  esbuild_remux_feed(&mc->owner->remux, mc->stream_idx, data, len, dash_fmp4_emit_pkt, h);
}

tssrc_t *tssrc_open(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out) {
  const char *ua = cfg->user_agent ? cfg->user_agent : "dvbipitools";
  tssrc_t *s = calloc(1, sizeof *s);
  if (!s) {
    if (reason_out) *reason_out = NET_ERR_OTHER;
    return NULL;
  }
  s->kind = cfg->kind;
  switch (s->kind) {
  case TSSRC_RTP:
  case TSSRC_UDP:
    s->m = mcast_open(cfg->family, cfg->group, cfg->port, cfg->iface, 1000);
    if (!s->m) {
      free(s);
      if (reason_out) *reason_out = NET_ERR_CONNECT;
      return NULL;
    }
    if (cfg->kind == TSSRC_RTP && cfg->al_fec_l) {
      s->fec_m = mcast_open(cfg->family, cfg->group, cfg->al_fec_port, cfg->iface, 0);
      s->fec_dec = s->fec_m ? fec2022_dec_new(cfg->al_fec_l, cfg->al_fec_d) : NULL;
      if (!s->fec_m || !s->fec_dec || mcast_set_nonblock(s->fec_m)) {
        if (s->fec_dec) fec2022_dec_free(s->fec_dec);
        if (s->fec_m) mcast_close(s->fec_m);
        mcast_close(s->m);
        free(s);
        if (reason_out) *reason_out = NET_ERR_CONNECT;
        return NULL;
      }
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
      if (reason_out) *reason_out = NET_ERR_CONNECT;
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
      if (reason_out) *reason_out = NET_ERR_CONNECT;
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
    if (reason_out) *reason_out = NET_ERR_EOF;
    return -1;
  }
  if (n < 0) {
    if (errno == EINTR) return 0;
    if (reason_out) *reason_out = NET_ERR_READ;
    return -1;
  }
  return n;
}

static int rtp_stride_candidate_ok(const unsigned char *b, int nn) {
  for (size_t k = 0; k < (size_t)nn; k++) if (b[12 + 188 * k] != 0x47) return 0;
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

static size_t rtp_deframe_step(tssrc_t *s, unsigned char *dst, size_t dstcap) {
  size_t i = 0;
  size_t o = 0;

  while (i < s->raw_len && o < dstcap) {
    size_t in_stride = s->rtp_pos % s->rtp_stride;
    if (in_stride < 12) {
      size_t need = 12 - in_stride;
      size_t avail = s->raw_len - i;
      if (in_stride == 0 && avail >= 12)
        s->last_rtp_ts = ((uint32_t)s->rawbuf[i + 4] << 24) | ((uint32_t)s->rawbuf[i + 5] << 16) | ((uint32_t)s->rawbuf[i + 6] << 8) | s->rawbuf[i + 7];
      if (need > avail) need = avail;
      i += need;
      s->rtp_pos += need;
      continue;
    }
    size_t chunk = s->rtp_stride - in_stride;
    size_t avail = s->raw_len - i;
    size_t room = dstcap - o;
    size_t take = chunk < avail ? chunk : avail;
    if (take > room) take = room;
    memcpy(dst + o, s->rawbuf + i, take);
    i += take;
    o += take;
    s->rtp_pos += take;
  }
  memmove(s->rawbuf, s->rawbuf + i, s->raw_len - i);
  s->raw_len -= i;
  return o;
}

static int handle_detect_read_error(const tssrc_t *s, net_err_reason_t r, net_err_reason_t *reason_out) {
  if (r == NET_ERR_EOF && s->raw_len > 0) return 1;
  if (reason_out) *reason_out = r;
  return 0;
}

static ssize_t deframe_read(tssrc_t *s, int fd, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  net_err_reason_t r;
  size_t out;

  if (s->deframe_state == DEFRAME_DETECT) {
    ssize_t got = raw_fd_read(fd, s->rawbuf + s->raw_len, TSSRC_DETECT_CAP - s->raw_len, &r);
    if (got < 0) {
      if (!handle_detect_read_error(s, r, reason_out)) return -1;
      detect_framing(s);
    } else {
      s->raw_len += (size_t)got;
      if (s->raw_len < TSSRC_DETECT_CAP) return 0;
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
    if (got < 0 && reason_out) *reason_out = r;
    return got;
  }

  out = rtp_deframe_step(s, buf, cap);
  if (out == 0) {
    ssize_t got = raw_fd_read(fd, s->rawbuf + s->raw_len, sizeof s->rawbuf - s->raw_len, &r);
    if (got < 0) {
      if (reason_out) *reason_out = r;
      return -1;
    }
    s->raw_len += (size_t)got;
    out = rtp_deframe_step(s, buf, cap);
  }
  return (ssize_t)out;
}

static ssize_t rtp_strip(unsigned char *buf, size_t n) {
  size_t off = rtp_payload_offset(buf, n);
  if (off) {
    memmove(buf, buf + off, n - off);
    n -= off;
  }
  return (ssize_t)n;
}

int tssrc_enable_rx_timestamps(tssrc_t *s) {
  if ((s->kind != TSSRC_UDP && s->kind != TSSRC_RTP) || !s->m) return -1;
  return mcast_enable_rx_timestamps(s->m);
}

uint64_t tssrc_last_rx_ns(const tssrc_t *s) {
  return s->m ? mcast_last_rx_ns(s->m) : 0;
}

static ssize_t tssrc_http_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  unsigned i;
  switch (s->http_sub) {
  case HTTP_SUB_TS:
    if (s->http_sniff_pos < s->http_sniff_len) {
      size_t n = s->http_sniff_len - s->http_sniff_pos;
      if (n > cap) n = cap;
      memcpy(buf, s->http_sniff_buf + s->http_sniff_pos, n);
      s->http_sniff_pos += n;
      return (ssize_t)n;
    }
    return http_read(s->h, buf, cap, reason_out);
  case HTTP_SUB_HLS:
    for (i = 0; i < s->n_media; i++) {
      ssize_t n = hls_live_read(s->hls[i], buf, cap, reason_out);
      if (n != 0) return n;
    }
    return 0;
  case HTTP_SUB_DASH:
    for (i = 0; i < s->n_media; i++) {
      ssize_t n = dash_live_read(s->dash[i], buf, cap, reason_out);
      if (n != 0) return n;
    }
    return 0;
  }
  return -1;
}

static int tssrc_http_fd(const tssrc_t *s) {
  unsigned i;
  switch (s->http_sub) {
  case HTTP_SUB_TS:
    return http_fd(s->h);
  case HTTP_SUB_HLS:
    for (i = 0; i < s->n_media; i++) {
      int fd = hls_live_poll_fd(s->hls[i]);
      if (fd >= 0) return fd;
    }
    return -1;
  case HTTP_SUB_DASH:
    for (i = 0; i < s->n_media; i++) {
      int fd = dash_live_poll_fd(s->dash[i]);
      if (fd >= 0) return fd;
    }
    return -1;
  }
  return -1;
}

ssize_t tssrc_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  switch (s->kind) {
    case TSSRC_RTP:
    case TSSRC_UDP: {
      ssize_t n;
      if (s->fec_dec) {
        n = mcast_recv(s->m, buf, cap, reason_out);
        if (n < 0) return n;
        if (n > 0) fec2022_dec_source(s->fec_dec, buf, (size_t)n);
        n = (ssize_t)fec2022_dec_drain(s->fec_dec, buf, cap);
        if (n == 0) return 0;
        return rtp_strip(buf, (size_t)n);
      }
      n = mcast_recv(s->m, buf, cap, reason_out);
      if (n <= 0) return n;
      return rtp_strip(buf, (size_t)n);
    }
    case TSSRC_HTTP:
      return tssrc_http_read(s, buf, cap, reason_out);
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
  if (s->kind != TSSRC_FILE && s->kind != TSSRC_STDIN) return;
  fd = (s->kind == TSSRC_FILE) ? s->fd : STDIN_FILENO;
  if (lseek(fd, 0, SEEK_SET) < 0) return;
  s->raw_len = 0;
  s->rtp_pos = 0;
}

mcast_t *tssrc_mcast(const tssrc_t *s) { return s->m; }

int tssrc_fec_fd(const tssrc_t *s) {
  return s->fec_dec ? mcast_fd(s->fec_m) : -1;
}

void tssrc_fec_poll(tssrc_t *s) {
  unsigned char repair_buf[FEC2022_MAX_REPAIR];
  ssize_t rn;
  if (!s->fec_dec) return;
  rn = mcast_recv(s->fec_m, repair_buf, sizeof repair_buf, NULL);
  if (rn > 0) fec2022_dec_repair(s->fec_dec, repair_buf, (size_t)rn);
}

int tssrc_fd(const tssrc_t *s) {
  switch (s->kind) {
  case TSSRC_RTP:
  case TSSRC_UDP:
    return mcast_fd(s->m);
  case TSSRC_HTTP:
    return tssrc_http_fd(s);
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
  unsigned i;
  if (!s) return;
  if (s->fec_dec) fec2022_dec_free(s->fec_dec);
  if (s->fec_m) mcast_close(s->fec_m);
  if (s->m) mcast_close(s->m);
  if (s->h) http_close(s->h);
  if (s->rist) ristin_close(s->rist);
  if (s->srt) srtsrc_close(s->srt);
  if (s->kind == TSSRC_FILE) close(s->fd);
  for (i = 0; i < s->n_media; i++) {
    if (s->hls[i]) hls_live_free(s->hls[i]);
    if (s->dash[i]) dash_live_free(s->dash[i]);
  }
  free(s);
}

typedef enum {
  HOPEN_SNIFF_CLASSIFY,
  HOPEN_SNIFF_MANIFEST,
  HOPEN_HLS_VARIANT_FETCH,
  HOPEN_HLS_INIT_FETCH
} http_open_phase_t;

struct tssrc_open {
  http_async_t *ha;
  http_t *h;
  http_fetch_t *hf;
  tssrc_t *result;
  int done;

  tssrc_kind_t kind;
  char user_agent[128];
  int insecure;

  http_open_phase_t phase;
  http_content_kind_t content;
  unsigned char buf[TSSRC_HTTP_MANIFEST_CAP];
  size_t buf_len;

  http_url_t media_url[2];
  hls_playlist_t hls_pl[2];
  unsigned n_media_url;
  unsigned fetch_idx;
  unsigned char fetch_buf[TSSRC_HTTP_INIT_CAP];
};

static int http_body_read_step(http_t *h, unsigned char *buf, size_t cap, size_t *len, net_err_reason_t *reason_out) {
  ssize_t n;
  net_err_reason_t r = NET_ERR_OTHER;
  if (*len >= cap) return 2;
  n = http_read(h, buf + *len, cap - *len, &r);
  if (n < 0) {
    if (r == NET_ERR_EOF) return 2;
    if (reason_out) *reason_out = r;
    return -1;
  }
  if (n == 0) return 0;
  *len += (size_t)n;
  return 1;
}

static int dash_find_adaptation_set(const dash_period_t *p, const char *mime_prefix) {
  unsigned i;
  size_t n = strlen(mime_prefix);
  for (i = 0; i < p->n_adaptation_sets; i++)
    if (!strncmp(p->adaptation_sets[i].mime_type, mime_prefix, n)) return (int)i;
  return -1;
}

static int build_dash_result(tssrc_open_t *o, const dash_mpd_t *mpd, const http_url_t *mpd_url, net_err_reason_t *reason_out) {
  int video_idx = dash_find_adaptation_set(&mpd->periods[0], "video/");
  int audio_idx = dash_find_adaptation_set(&mpd->periods[0], "audio/");
  tssrc_t *r;

  if (video_idx < 0 && audio_idx < 0) {
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return 0;
  }
  r = calloc(1, sizeof *r);
  if (!r) {
    if (reason_out) *reason_out = NET_ERR_OTHER;
    return 0;
  }
  r->kind = TSSRC_HTTP;
  r->http_sub = HTTP_SUB_DASH;

  if (video_idx >= 0) {
    r->media_ctx[r->n_media].owner = r;
    r->media_ctx[r->n_media].stream_idx = r->n_media;
    r->dash[r->n_media] = dash_live_new(mpd_url, o->user_agent, o->insecure, 0, "tssource", (unsigned)video_idx, NULL, dash_fmp4_segment_feed, &r->media_ctx[r->n_media]);
    if (!r->dash[r->n_media]) {
      tssrc_close(r);
      if (reason_out) *reason_out = NET_ERR_OTHER;
      return 0;
    }
    r->n_media++;
  }
  if (audio_idx >= 0) {
    r->media_ctx[r->n_media].owner = r;
    r->media_ctx[r->n_media].stream_idx = r->n_media;
    r->dash[r->n_media] = dash_live_new(mpd_url, o->user_agent, o->insecure, 0, "tssource", (unsigned)audio_idx, NULL, dash_fmp4_segment_feed, &r->media_ctx[r->n_media]);
    if (!r->dash[r->n_media]) {
      tssrc_close(r);
      if (reason_out) *reason_out = NET_ERR_OTHER;
      return 0;
    }
    r->n_media++;
  }
  o->result = r;
  return 1;
}

static int build_hls_passthrough_result(tssrc_open_t *o, net_err_reason_t *reason_out) {
  tssrc_t *r = calloc(1, sizeof *r);
  if (!r) {
    if (reason_out) *reason_out = NET_ERR_OTHER;
    return 0;
  }
  r->kind = TSSRC_HTTP;
  r->http_sub = HTTP_SUB_HLS;
  r->hls[0] = hls_live_new(&o->media_url[0], o->user_agent, o->insecure, 0, "tssource", NULL, hls_ts_passthrough_feed, &r->ts_pass);
  if (!r->hls[0]) {
    free(r);
    if (reason_out) *reason_out = NET_ERR_OTHER;
    return 0;
  }
  r->n_media = 1;
  o->result = r;
  return 1;
}

tssrc_open_t *tssrc_open_async_start(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out) {
  tssrc_open_t *o = calloc(1, sizeof *o);
  if (!o) return NULL;
  o->kind = cfg->kind;
  if (cfg->kind == TSSRC_HTTP) {
    const char *ua = cfg->user_agent ? cfg->user_agent : "dvbipitools";
    bufcpy(o->user_agent, sizeof o->user_agent, ua);
    o->insecure = cfg->insecure_tls;
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
  if (o->ha) return http_async_poll_fd(o->ha);
  if (o->hf) return http_fetch_poll_fd(o->hf);
  if (o->h) return http_fd(o->h);
  return -1;
}

short tssrc_open_async_poll_events(const tssrc_open_t *o) {
  if (o->ha) return http_async_poll_events(o->ha);
  if (o->hf) return http_fetch_poll_events(o->hf);
  if (o->h) return POLLIN;
  return 0;
}

typedef enum { HOPEN_STEP_CONTINUE, HOPEN_STEP_PENDING, HOPEN_STEP_ERROR, HOPEN_STEP_DONE } http_open_step_t;

static http_open_step_t step_sniff_classify(tssrc_open_t *o, net_err_reason_t *reason_out) {
  int rc = http_body_read_step(o->h, o->buf, TSSRC_HTTP_SNIFF_CAP, &o->buf_len, reason_out);
  if (rc == 0) return HOPEN_STEP_PENDING;
  if (rc == -1) {
    http_close(o->h);
    o->h = NULL;
    return HOPEN_STEP_ERROR;
  }
  if (rc == 1 && o->buf_len < TSSRC_HTTP_SNIFF_CAP) return HOPEN_STEP_CONTINUE;
  o->content = classify_http_content(o->buf, o->buf_len);
  if (o->content == HTTP_CONTENT_UNKNOWN) {
    http_close(o->h);
    o->h = NULL;
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return HOPEN_STEP_ERROR;
  }
  if (o->content == HTTP_CONTENT_TS) {
    o->result = calloc(1, sizeof *o->result);
    if (!o->result) {
      http_close(o->h);
      o->h = NULL;
      if (reason_out) *reason_out = NET_ERR_OTHER;
      return HOPEN_STEP_ERROR;
    }
    o->result->kind = TSSRC_HTTP;
    o->result->http_sub = HTTP_SUB_TS;
    o->result->h = o->h;
    memcpy(o->result->http_sniff_buf, o->buf, o->buf_len);
    o->result->http_sniff_len = o->buf_len;
    o->h = NULL;
    o->done = 1;
    return HOPEN_STEP_DONE;
  }
  o->phase = HOPEN_SNIFF_MANIFEST;
  return HOPEN_STEP_CONTINUE;
}

static http_open_step_t step_sniff_manifest(tssrc_open_t *o, net_err_reason_t *reason_out) {
  int rc = http_body_read_step(o->h, o->buf, TSSRC_HTTP_MANIFEST_CAP, &o->buf_len, reason_out);
  size_t term;
  http_url_t base;
  if (rc == 0) return HOPEN_STEP_PENDING;
  if (rc == -1) {
    http_close(o->h);
    o->h = NULL;
    return HOPEN_STEP_ERROR;
  }
  if (rc == 1) return HOPEN_STEP_CONTINUE;
  term = o->buf_len < TSSRC_HTTP_MANIFEST_CAP ? o->buf_len : TSSRC_HTTP_MANIFEST_CAP - 1;
  base = *http_final_url(o->h);
  o->buf[term] = '\0';

  if (o->content == HTTP_CONTENT_DASH) {
    dash_mpd_t mpd;
    int ok = dash_mpd_parse((char *)o->buf, &base, &mpd) && mpd.n_periods > 0 && build_dash_result(o, &mpd, &base, reason_out);
    http_close(o->h);
    o->h = NULL;
    if (!ok) {
      if (reason_out && *reason_out == NET_ERR_OTHER) *reason_out = NET_ERR_FORMAT;
      return HOPEN_STEP_ERROR;
    }
    o->done = 1;
    return HOPEN_STEP_DONE;
  }

  if (hls_body_is_master((char *)o->buf)) {
    hls_master_t hm;
    int best;
    const hls_audio_rendition_t *ar;
    if (!hls_master_parse((char *)o->buf, &base, &hm) || (best = hls_master_pick_highest(&hm)) < 0) {
      http_close(o->h);
      o->h = NULL;
      if (reason_out) *reason_out = NET_ERR_FORMAT;
      return HOPEN_STEP_ERROR;
    }
    if (http_url_parse(hm.variants[(unsigned)best].url, &o->media_url[0])) {
      http_close(o->h);
      o->h = NULL;
      if (reason_out) *reason_out = NET_ERR_FORMAT;
      return HOPEN_STEP_ERROR;
    }
    o->n_media_url = 1;
    ar = hls_master_find_audio(&hm, hm.variants[(unsigned)best].audio_group_id);
    if (ar && !http_url_parse(ar->url, &o->media_url[1])) o->n_media_url = 2;
    http_close(o->h);
    o->h = NULL;
    o->fetch_idx = 0;
    o->phase = HOPEN_HLS_VARIANT_FETCH;
    return HOPEN_STEP_CONTINUE;
  }

  o->media_url[0] = base;
  o->n_media_url = 1;
  if (!hls_playlist_parse((char *)o->buf, &base, &o->hls_pl[0])) {
    http_close(o->h);
    o->h = NULL;
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return HOPEN_STEP_ERROR;
  }
  http_close(o->h);
  o->h = NULL;
  if (!o->hls_pl[0].map_uri[0]) {
    if (!build_hls_passthrough_result(o, reason_out)) return HOPEN_STEP_ERROR;
    o->done = 1;
    return HOPEN_STEP_DONE;
  }
  o->fetch_idx = 0;
  o->phase = HOPEN_HLS_INIT_FETCH;
  return HOPEN_STEP_CONTINUE;
}

static http_open_step_t step_hls_variant_fetch(tssrc_open_t *o, net_err_reason_t *reason_out) {
  http_fetch_state_t st;
  size_t len;
  http_t *reuse;
  if (!o->hf) {
    o->hf = http_fetch_start(&o->media_url[o->fetch_idx], o->user_agent, o->insecure, NULL, NULL, o->fetch_buf, sizeof o->fetch_buf - 1, NULL, reason_out);
    if (!o->hf) return HOPEN_STEP_ERROR;
  }
  st = http_fetch_step(o->hf, reason_out);
  if (st == HTTP_FETCH_PENDING) return HOPEN_STEP_PENDING;
  if (st == HTTP_FETCH_ERROR) {
    http_fetch_free(o->hf);
    o->hf = NULL;
    return HOPEN_STEP_ERROR;
  }
  http_fetch_take(o->hf, &len, NULL, NULL, 0, NULL, &reuse);
  http_fetch_free(o->hf);
  o->hf = NULL;
  if (reuse) http_close(reuse);
  o->fetch_buf[len] = '\0';
  if (!hls_playlist_parse((char *)o->fetch_buf, &o->media_url[o->fetch_idx], &o->hls_pl[o->fetch_idx])) {
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return HOPEN_STEP_ERROR;
  }
  o->fetch_idx++;
  if (o->fetch_idx < o->n_media_url) return HOPEN_STEP_CONTINUE;

  if (o->n_media_url == 2 && !(o->hls_pl[0].map_uri[0] && o->hls_pl[1].map_uri[0])) o->n_media_url = 1;
  if (!o->hls_pl[0].map_uri[0]) {
    if (!build_hls_passthrough_result(o, reason_out)) return HOPEN_STEP_ERROR;
    o->done = 1;
    return HOPEN_STEP_DONE;
  }
  o->fetch_idx = 0;
  o->phase = HOPEN_HLS_INIT_FETCH;
  return HOPEN_STEP_CONTINUE;
}

static http_open_step_t step_hls_init_fetch(tssrc_open_t *o, net_err_reason_t *reason_out) {
  http_fetch_state_t st;
  size_t len;
  http_t *reuse;
  if (!o->hf) {
    http_url_t init_url;
    if (http_url_parse(o->hls_pl[o->fetch_idx].map_uri, &init_url)) return HOPEN_STEP_ERROR;
    o->hf = http_fetch_start(&init_url, o->user_agent, o->insecure, NULL, NULL, o->fetch_buf, sizeof o->fetch_buf, NULL, reason_out);
    if (!o->hf) return HOPEN_STEP_ERROR;
  }
  st = http_fetch_step(o->hf, reason_out);
  if (st == HTTP_FETCH_PENDING) return HOPEN_STEP_PENDING;
  if (st == HTTP_FETCH_ERROR) {
    http_fetch_free(o->hf);
    o->hf = NULL;
    return HOPEN_STEP_ERROR;
  }
  http_fetch_take(o->hf, &len, NULL, NULL, 0, NULL, &reuse);
  http_fetch_free(o->hf);
  o->hf = NULL;
  if (reuse) http_close(reuse);

  if (!o->result) {
    o->result = calloc(1, sizeof *o->result);
    if (!o->result) {
      if (reason_out) *reason_out = NET_ERR_OTHER;
      return HOPEN_STEP_ERROR;
    }
    o->result->kind = TSSRC_HTTP;
    o->result->http_sub = HTTP_SUB_HLS;
  }
  if (!esbuild_remux_add_init(&o->result->remux, o->fetch_idx, o->fetch_buf, len)) {
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return HOPEN_STEP_ERROR;
  }
  o->result->media_ctx[o->fetch_idx].owner = o->result;
  o->result->media_ctx[o->fetch_idx].stream_idx = o->fetch_idx;
  o->result->hls[o->fetch_idx] = hls_live_new(&o->media_url[o->fetch_idx], o->user_agent, o->insecure, 0, "tssource", NULL, hls_fmp4_segment_feed, &o->result->media_ctx[o->fetch_idx]);
  if (!o->result->hls[o->fetch_idx]) {
    if (reason_out) *reason_out = NET_ERR_OTHER;
    return HOPEN_STEP_ERROR;
  }
  o->result->n_media++;
  o->fetch_idx++;
  if (o->fetch_idx < o->n_media_url) return HOPEN_STEP_CONTINUE;
  o->done = 1;
  return HOPEN_STEP_DONE;
}

tssrc_open_state_t tssrc_open_async_step(tssrc_open_t *o, net_err_reason_t *reason_out) {
  if (o->done) return TSSRC_OPEN_DONE;

  if (o->ha) {
    http_async_state_t st = http_async_step(o->ha, reason_out);
    if (st == HTTP_ASYNC_PENDING) return TSSRC_OPEN_PENDING;
    if (st == HTTP_ASYNC_ERROR) {
      http_async_free(o->ha);
      o->ha = NULL;
      return TSSRC_OPEN_ERROR;
    }
    o->h = http_async_take(o->ha);
    o->ha = NULL;
    o->phase = HOPEN_SNIFF_CLASSIFY;
  }

  if (o->kind != TSSRC_HTTP) return TSSRC_OPEN_DONE;

  for (;;) {
    http_open_step_t r;
    switch (o->phase) {
      case HOPEN_SNIFF_CLASSIFY:    r = step_sniff_classify(o, reason_out); break;
      case HOPEN_SNIFF_MANIFEST:    r = step_sniff_manifest(o, reason_out); break;
      case HOPEN_HLS_VARIANT_FETCH: r = step_hls_variant_fetch(o, reason_out); break;
      case HOPEN_HLS_INIT_FETCH:    r = step_hls_init_fetch(o, reason_out); break;
      default: return TSSRC_OPEN_ERROR;
    }
    switch (r) {
      case HOPEN_STEP_CONTINUE: continue;
      case HOPEN_STEP_PENDING:  return TSSRC_OPEN_PENDING;
      case HOPEN_STEP_ERROR:    return TSSRC_OPEN_ERROR;
      case HOPEN_STEP_DONE:     return TSSRC_OPEN_DONE;
    }
  }
}

tssrc_t *tssrc_open_async_take(tssrc_open_t *o) {
  tssrc_t *r = o->result;
  free(o);
  return r;
}

void tssrc_open_async_free(tssrc_open_t *o) {
  if (!o) return;
  if (o->ha) http_async_free(o->ha);
  if (o->h) http_close(o->h);
  if (o->hf) http_fetch_free(o->hf);
  if (o->result) tssrc_close(o->result);
  free(o);
}
