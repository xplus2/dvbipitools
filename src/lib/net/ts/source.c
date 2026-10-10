/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../demux/fec2022.h"

#include "source_priv.h"

#define FEC_POLL_MAX 64

static tssrc_t *tssrc_open_base(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out) {
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
    s->m = mcast_open_src(cfg->family, cfg->group, cfg->port, cfg->source, cfg->iface, 1000);
    if (!s->m) {
      free(s);
      if (reason_out) *reason_out = NET_ERR_CONNECT;
      return NULL;
    }
    if (cfg->kind == TSSRC_RTP && cfg->al_fec_l) {
      s->fec_m = mcast_open_src(cfg->family, cfg->group, cfg->al_fec_port, cfg->source, cfg->iface, 0);
      s->fec_dec = s->fec_m ? fec2022_dec_new(cfg->al_fec_l, cfg->al_fec_d) : NULL;
      if (!s->fec_m || !s->fec_dec || mcast_set_nonblock(s->fec_m) || mcast_set_nonblock(s->m)) {
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
    rc.key_size = cfg->rist_key_size;
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

int tssrc_enable_rx_timestamps(tssrc_t *s) {
  if ((s->kind != TSSRC_UDP && s->kind != TSSRC_RTP) || !s->m) return -1;
  return mcast_enable_rx_timestamps(s->m);
}

uint64_t tssrc_last_rx_ns(const tssrc_t *s) {
  return s->m ? mcast_last_rx_ns(s->m) : 0;
}


static ssize_t stream_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  switch (s->kind) {
    case TSSRC_HTTP:
      return tssrc_http_read(s, buf, cap, reason_out);
    case TSSRC_STDIN:
      return tssrc_deframe_read(s, STDIN_FILENO, buf, cap, reason_out);
    default:
      return tssrc_deframe_read(s, s->fd, buf, cap, reason_out);
  }
}

/* byte-stream kinds: whole 188 multiples only, remainder waits for next read. partial at EOF dropped */
static ssize_t stream_read_aligned(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  size_t have;
  size_t out;
  ssize_t n;

  if (cap < 2 * 188) return stream_read(s, buf, cap, reason_out);
  memcpy(buf, s->align_tail, s->align_len);
  n = stream_read(s, buf + s->align_len, cap - s->align_len, reason_out);
  if (n < 0) return n;
  have = s->align_len + (size_t)n;
  out = have - have % 188;
  s->align_len = have - out;
  memcpy(s->align_tail, buf + out, s->align_len);
  return (ssize_t)out;
}

ssize_t tssrc_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  if (s->jb) return tssrc_jitter_read(s, buf, cap, reason_out);
  switch (s->kind) {
    case TSSRC_RTP:
    case TSSRC_UDP: {
      ssize_t n;
      if (s->fec_dec) {
        n = mcast_recv(s->m, buf, cap, reason_out);
        if (n < 0) return n;
        if (n > 0) fec2022_dec_source(s->fec_dec, buf, (size_t)n);
        tssrc_fec_poll(s);
        n = (ssize_t)fec2022_dec_drain(s->fec_dec, buf, cap);
        if (n == 0) return 0;
        return tssrc_rtp_strip(buf, (size_t)n);
      }
      n = mcast_recv(s->m, buf, cap, reason_out);
      if (n <= 0) return n;
      return tssrc_rtp_strip(buf, (size_t)n);
    }
    case TSSRC_HTTP:
    case TSSRC_STDIN:
    case TSSRC_FILE:
      return stream_read_aligned(s, buf, cap, reason_out);
    case TSSRC_RIST:
      return tssrc_raw_fd_read(ristin_fd(s->rist), buf, cap, reason_out);
    case TSSRC_SRT:
      return tssrc_raw_fd_read(srtsrc_fd(s->srt), buf, cap, reason_out);
  }
  return -1;
}

void tssrc_rewind(tssrc_t *s) {
  int fd;
  if (s->kind != TSSRC_FILE && s->kind != TSSRC_STDIN) return;
  fd = (s->kind == TSSRC_FILE) ? s->fd : STDIN_FILENO;
  if (lseek(fd, 0, SEEK_SET) < 0) return;
  s->raw_len = 0;
  s->rtp_pos = 0;
  s->align_len = 0;
}

mcast_t *tssrc_mcast(const tssrc_t *s) { return s->m; }

int tssrc_fec_fd(const tssrc_t *s) {
  return s->fec_dec ? mcast_fd(s->fec_m) : -1;
}

void tssrc_fec_poll(tssrc_t *s) {
  unsigned char repair_buf[FEC2022_MAX_REPAIR];
  if (!s->fec_dec) return;
  for (unsigned i = 0; i < FEC_POLL_MAX; i++) {
    ssize_t rn = mcast_recv(s->fec_m, repair_buf, sizeof repair_buf, NULL);
    if (rn <= 0) break;
    fec2022_dec_repair(s->fec_dec, repair_buf, (size_t)rn);
  }
}

int tssrc_fd(const tssrc_t *s) { return s->jb ? s->epfd : tssrc_src_fd(s); }

int tssrc_src_fd(const tssrc_t *s) {
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

tssrc_t *tssrc_open(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out) {
  tssrc_t *s = tssrc_open_base(cfg, reason_out);
  int capable = cfg->kind == TSSRC_RTP || cfg->kind == TSSRC_UDP || cfg->kind == TSSRC_RIST || cfg->kind == TSSRC_SRT;

  if (!s || !cfg->jitter_ms || !capable) return s;
  if (tssrc_jitter_attach(s, cfg->jitter_ms) < 0) {
    tssrc_close(s);
    if (reason_out) *reason_out = NET_ERR_OTHER;
    return NULL;
  }
  return s;
}

void tssrc_close(tssrc_t *s) {
  if (!s) return;
  tssrc_jitter_free(s);
  if (s->fec_dec) fec2022_dec_free(s->fec_dec);
  if (s->fec_m) mcast_close(s->fec_m);
  if (s->m) mcast_close(s->m);
  if (s->h) http_close(s->h);
  if (s->rist) ristin_close(s->rist);
  if (s->srt) srtsrc_close(s->srt);
  if (s->kind == TSSRC_FILE) close(s->fd);
  tssrc_http_free_media(s);
  free(s);
}
