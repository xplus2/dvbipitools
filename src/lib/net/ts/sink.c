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
#include "lib/sys/signal.h"

#include "../multicast.h"
#include "sink.h"

#define TS_PER_DGRAM 7 /* 7*188 = 1316B, fits one Ethernet MTU with RTP/UDP/IP headroom */
#define AL_FEC_PT 96
#define DGRAM_MAX (TS_PER_DGRAM * 188)
#define PACK_AGE_S 0.02
#define TS_SYNC 0x47

struct tssink {
  tssink_kind_t kind;
  mcast_t *mc;
  rtpheader_t *rtph;
  mcast_t *fec_mc;
  fec2022_enc_t *fec_enc;
  int fd;
  int pack;
  unsigned char pend[DGRAM_MAX];
  size_t pendlen;
  double pend_since;
};

tssink_t *tssink_open(const tssink_cfg_t *cfg) {
  tssink_t *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->kind = cfg->kind;
  s->fd = -1;
  s->pack = cfg->pack;
  switch (cfg->kind) {
    case TSSINK_UDP:
    case TSSINK_RTP:
      s->mc = mcast_open_send(cfg->family, cfg->group, cfg->port, cfg->iface, cfg->ttl);
      if (!s->mc) goto fail;
      if (cfg->kind == TSSINK_RTP) {
        s->rtph = rtpheader_new();
        if (!s->rtph) goto fail;
        if (cfg->al_fec_l) {
          s->fec_mc = mcast_open_send(cfg->family, cfg->group, cfg->al_fec_port, cfg->iface, cfg->ttl);
          s->fec_enc = s->fec_mc ? fec2022_enc_new(cfg->al_fec_l, cfg->al_fec_d, AL_FEC_PT) : NULL;
          if (!s->fec_mc || !s->fec_enc) goto fail;
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
        goto fail;
      }
      break;
  }
  return s;

fail:
  if (s->fec_mc) mcast_close(s->fec_mc);
  if (s->rtph) rtpheader_free(s->rtph);
  if (s->mc) mcast_close(s->mc);
  free(s);
  return NULL;
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
  unsigned char hdr[12];
  unsigned char dgram[12 + TS_PER_DGRAM * 188];
  unsigned char repair[FEC2022_MAX_REPAIR];
  while (n) {
    size_t chunk = n < TS_PER_DGRAM * 188 ? n : TS_PER_DGRAM * 188;
    size_t rlen;
    if (s->rtph) {
      uint32_t ts90k = (uint32_t)(mono_seconds() * 90000.0);
      if (s->fec_enc) {
        rtpheader_build(s->rtph, ts90k, dgram, 12);
        memcpy(dgram + 12, buf, chunk);
        if (mcast_send(s->mc, dgram, 12 + chunk) < 0) return -1;
        rlen = fec2022_enc_feed(s->fec_enc, dgram, 12 + chunk, ts90k, repair, sizeof repair);
        if (rlen && mcast_send(s->fec_mc, repair, rlen) < 0) return -1;
      } else {
        rtpheader_build(s->rtph, ts90k, hdr, 12);
        if (mcast_sendv(s->mc, hdr, 12, buf, chunk) < 0) return -1;
      }
    } else if (mcast_send(s->mc, buf, chunk) < 0) {
      return -1;
    }
    buf += chunk;
    n -= chunk;
  }
  return 0;
}

static void pend_resync(tssink_t *s) {
  for (;;) {
    size_t k = 0;
    size_t p;
    while (k < s->pendlen && s->pend[k] != TS_SYNC) k++;
    if (k) {
      memmove(s->pend, s->pend + k, s->pendlen - k);
      s->pendlen -= k;
    }
    for (p = 188; p < s->pendlen; p += 188)
      if (s->pend[p] != TS_SYNC) break;
    if (p >= s->pendlen) return;
    memmove(s->pend, s->pend + 1, --s->pendlen);
  }
}

int tssink_flush(tssink_t *s) {
  size_t whole;
  int rc = 0;
  if (!s->pack || !s->pendlen) return 0;
  pend_resync(s);
  whole = s->pendlen / 188 * 188;
  if (whole) {
    rc = write_net(s, s->pend, whole);
    memmove(s->pend, s->pend + whole, s->pendlen - whole);
    s->pendlen -= whole;
  }
  s->pend_since = mono_seconds();
  return rc;
}

static int write_packed(tssink_t *s, const unsigned char *buf, size_t n) {
  while (n) {
    size_t take = DGRAM_MAX - s->pendlen;
    if (take > n) take = n;
    if (!s->pendlen) s->pend_since = mono_seconds();
    memcpy(s->pend + s->pendlen, buf, take);
    s->pendlen += take;
    buf += take;
    n -= take;
    pend_resync(s);
    if (s->pendlen == DGRAM_MAX) {
      int rc = write_net(s, s->pend, DGRAM_MAX);
      s->pendlen = 0;
      if (rc < 0) return -1;
    }
  }
  if (s->pendlen >= 188 && mono_seconds() - s->pend_since >= PACK_AGE_S) return tssink_flush(s);
  return 0;
}

int tssink_write(tssink_t *s, const unsigned char *buf, size_t n) {
  switch (s->kind) {
    case TSSINK_UDP:
    case TSSINK_RTP:
      return s->pack ? write_packed(s, buf, n) : write_net(s, buf, n);
    case TSSINK_STDOUT:
    case TSSINK_FILE:
      return write_all(s->fd, buf, n);
  }
  return write_all(s->fd, buf, n);
}

void tssink_close(tssink_t *s) {
  if (!s) return;
  tssink_flush(s);
  if (s->rtph) rtpheader_free(s->rtph);
  if (s->fec_enc) fec2022_enc_free(s->fec_enc);
  if (s->fec_mc) mcast_close(s->fec_mc);
  if (s->mc) mcast_close(s->mc);
  if (s->fd >= 0 && s->fd != STDOUT_FILENO) close(s->fd);
  free(s);
}
