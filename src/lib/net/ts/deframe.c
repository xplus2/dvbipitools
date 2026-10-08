/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "../../demux/rtp.h"

#include "source_priv.h"

ssize_t tssrc_raw_fd_read(int fd, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
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
  for (size_t k = 0; k < (size_t)nn; k++) {
    if (b[12 + 188 * k] != 0x47) return 0;
  }
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

ssize_t tssrc_deframe_read(tssrc_t *s, int fd, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  net_err_reason_t r;
  size_t out;

  if (s->deframe_state == DEFRAME_DETECT) {
    ssize_t got = tssrc_raw_fd_read(fd, s->rawbuf + s->raw_len, TSSRC_DETECT_CAP - s->raw_len, &r);
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
    got = tssrc_raw_fd_read(fd, buf, cap, &r);
    if (got < 0 && reason_out) *reason_out = r;
    return got;
  }

  out = rtp_deframe_step(s, buf, cap);
  if (out == 0) {
    ssize_t got = tssrc_raw_fd_read(fd, s->rawbuf + s->raw_len, sizeof s->rawbuf - s->raw_len, &r);
    if (got < 0) {
      if (reason_out) *reason_out = r;
      return -1;
    }
    s->raw_len += (size_t)got;
    out = rtp_deframe_step(s, buf, cap);
  }
  return (ssize_t)out;
}

ssize_t tssrc_rtp_strip(unsigned char *buf, size_t n) {
  size_t off = rtp_payload_offset(buf, n);
  if (off) {
    memmove(buf, buf + off, n - off);
    n -= off;
  }
  return (ssize_t)n;
}

int tssrc_is_rtp_framed(const tssrc_t *s) { return s->deframe_state == DEFRAME_RTP; }
uint32_t tssrc_last_rtp_ts(const tssrc_t *s) { return s->last_rtp_ts; }
