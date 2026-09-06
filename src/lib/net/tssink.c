/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"
#include "lib/mux/rtpheader.h"
#include "lib/helper/signal.h"

#include "multicast.h"
#include "tssink.h"

#define TS_PER_DGRAM 7 /* 7*188 = 1316B, fits one Ethernet MTU with RTP/UDP/IP headroom */
#define AL_FEC_PT 96

struct tssink {
  tssink_kind_t kind;
  mcast_t *mc;
  rtpheader_t *rtph;
  mcast_t *fec_mc;
  fec2022_enc_t *fec_enc;
  int fd;
};

tssink_t *tssink_open(const tssink_cfg_t *cfg) {
  tssink_t *s = calloc(1, sizeof *s);

  if (!s)
    return NULL;
  s->kind = cfg->kind;
  s->fd = -1;

  switch (cfg->kind) {
  case TSSINK_UDP:
  case TSSINK_RTP:
    s->mc = mcast_open_send(cfg->family, cfg->group, cfg->port, cfg->iface, cfg->ttl);
    if (!s->mc) {
      free(s);
      return NULL;
    }
    if (cfg->kind == TSSINK_RTP) {
      s->rtph = rtpheader_new();
      if (!s->rtph) {
        mcast_close(s->mc);
        free(s);
        return NULL;
      }
      if (cfg->al_fec_l) {
        s->fec_mc = mcast_open_send(cfg->family, cfg->group, cfg->al_fec_port, cfg->iface, cfg->ttl);
        s->fec_enc = s->fec_mc ? fec2022_enc_new(cfg->al_fec_l, cfg->al_fec_d, AL_FEC_PT) : NULL;
        if (!s->fec_mc || !s->fec_enc) {
          if (s->fec_mc) mcast_close(s->fec_mc);
          rtpheader_free(s->rtph);
          mcast_close(s->mc);
          free(s);
          return NULL;
        }
      }
    }
    break;
  case TSSINK_STDOUT:
    s->fd = STDOUT_FILENO;
    break;
  case TSSINK_FILE:
    s->fd = open(cfg->file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (s->fd < 0) {
      log_line("open %s: %s", cfg->file_path, strerror(errno));
      free(s);
      return NULL;
    }
    break;
  }
  return s;
}

static int write_all(int fd, const unsigned char *p, size_t n) {
  while (n) {
    ssize_t w = write(fd, p, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      log_line("w:%s", strerror(errno));
      return -1;
    }
    p += w;
    n -= (size_t)w;
  }
  return 0;
}

static int write_net(tssink_t *s, const unsigned char *buf, size_t n) {
  unsigned char dgram[12 + TS_PER_DGRAM * 188];
  unsigned char repair[FEC2022_MAX_REPAIR];
  while (n) {
    size_t chunk = n < TS_PER_DGRAM * 188 ? n : TS_PER_DGRAM * 188;

    if (s->rtph) {
      rtpheader_build(s->rtph, (uint32_t)(mono_seconds() * 90000.0), dgram, 12);
      memcpy(dgram + 12, buf, chunk);
      if (mcast_send(s->mc, dgram, 12 + chunk) < 0) return -1;
      if (s->fec_enc) {
        size_t rlen = fec2022_enc_feed(s->fec_enc, dgram, 12 + chunk, (uint32_t)(mono_seconds() * 90000.0), repair, sizeof repair);
        if (rlen && mcast_send(s->fec_mc, repair, rlen) < 0) return -1;
      }
    } else if (mcast_send(s->mc, buf, chunk) < 0) {
      return -1;
    }
    buf += chunk;
    n -= chunk;
  }
  return 0;
}

int tssink_write(tssink_t *s, const unsigned char *buf, size_t n) {
  if (s->kind == TSSINK_UDP || s->kind == TSSINK_RTP) return write_net(s, buf, n);
  return write_all(s->fd, buf, n);
}

void tssink_close(tssink_t *s) {
  if (!s) return;
  if (s->rtph) rtpheader_free(s->rtph);
  if (s->fec_enc) fec2022_enc_free(s->fec_enc);
  if (s->fec_mc) mcast_close(s->fec_mc);
  if (s->mc) mcast_close(s->mc);
  if (s->fd >= 0 && s->fd != STDOUT_FILENO) close(s->fd);
  free(s);
}
