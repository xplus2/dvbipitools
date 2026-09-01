/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/demux/rtp.h"
#include "lib/demux/rtx.h"
#include "lib/helper/log.h"
#include "lib/helper/signal.h"
#include "lib/mux/rtcp_build.h"
#include "lib/net/netconnect.h"
#include "fcc_client.h"

#define FCC_RTCP_PT_MIN 192 /* RFC 5761 mux range, distinguishes RAMS-I from burst RTP on one socket */
#define FCC_RTCP_PT_MAX 223
#define FCC_PENDING_CAP 65536

struct fcc_client {
  int uni_fd;
  unsigned char rtx_pt;
  uint32_t sender_ssrc;

  int done;
  int rejected;

  unsigned char pending[FCC_PENDING_CAP];
  size_t pending_len;
};

static void rams_i_cb(const rtcp_rams_i_t *info, void *user) {
  fcc_client_t *r = user;
  if (info->response != 100 && info->response != 200) {
    log_line("fcc: burst request rejected, response %u", (unsigned)info->response);
    r->rejected = 1;
    r->done = 1;
  }
}

void fcc_on_uni(fcc_client_t *r, const unsigned char *pkt, size_t len, double now) {
  (void)now;
  if (len < 2)
    return;
  if (pkt[1] >= FCC_RTCP_PT_MIN && pkt[1] <= FCC_RTCP_PT_MAX) {
    rtcp_parse(pkt, len, NULL, NULL, rams_i_cb, NULL, NULL, NULL, r);
    return;
  }
  if (r->done)
    return;
  {
    rtx_pkt_t rx;
    if (!rtx_parse(pkt, len, r->rtx_pt, &rx))
      return;
    if (rx.payload_len > sizeof r->pending)
      return;
    memcpy(r->pending, rx.payload, rx.payload_len);
    r->pending_len = rx.payload_len;
  }
}

void fcc_on_multicast(fcc_client_t *r, const rtp_hdr_t *hdr, const unsigned char *payload, size_t len, double now) {
  (void)now;
  if (r->done)
    return;
  {
    rtcp_rams_t_t term;
    unsigned char pkt[32];
    size_t n;
    memset(&term, 0, sizeof term);
    term.sender_ssrc = r->sender_ssrc;
    term.media_ssrc = hdr->ssrc;
    term.has_first_mc_seqnum = 1;
    term.first_mc_seqnum = hdr->seq;
    n = rtcp_build_rams_t(&term, pkt, sizeof pkt);
    if (n)
      send(r->uni_fd, pkt, n, 0);
  }
  r->done = 1;
  if (len > sizeof r->pending)
    len = sizeof r->pending;
  memcpy(r->pending, payload, len);
  r->pending_len = len;
}

int fcc_client_done(const fcc_client_t *r) { return r->done; }

static int uni_socket_open(const fcc_client_cfg_t *cfg) {
  int fd;
  struct sockaddr_storage ss;
  socklen_t sslen;

  fd = socket(cfg->family, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    log_line("fcc: socket: %s", strerror(errno));
    return -1;
  }
  if (netaddr_fill(cfg->family, cfg->addr, cfg->port, &ss, &sslen)) {
    log_line("fcc: bad server address: %s", cfg->addr);
    close(fd);
    return -1;
  }
  if (connect(fd, (struct sockaddr *)&ss, sslen) < 0) {
    log_line("fcc: connect: %s", strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

static void send_rams_r(const fcc_client_t *r, const fcc_client_cfg_t *cfg) {
  rtcp_rams_r_t req;
  unsigned char pkt[32];
  size_t n;

  memset(&req, 0, sizeof req);
  req.sender_ssrc = r->sender_ssrc;
  req.media_ssrc = 0;
  req.ignore_media_ssrc = 1;
  if (cfg->min_buffer_fill_ms) {
    req.has_min_buffer_fill = 1;
    req.min_buffer_fill_ms = cfg->min_buffer_fill_ms;
  }
  if (cfg->max_buffer_fill_ms) {
    req.has_max_buffer_fill = 1;
    req.max_buffer_fill_ms = cfg->max_buffer_fill_ms;
  }
  n = rtcp_build_rams_r(&req, pkt, sizeof pkt);
  if (n)
    send(r->uni_fd, pkt, n, 0);
}

fcc_client_t *fcc_client_open(const fcc_client_cfg_t *cfg) {
  fcc_client_t *r = calloc(1, sizeof *r);
  if (!r)
    return NULL;

  r->uni_fd = uni_socket_open(cfg);
  if (r->uni_fd < 0) {
    free(r);
    return NULL;
  }
  r->rtx_pt = cfg->rtx_pt;
  srand((unsigned)(time(NULL) ^ getpid()));
  r->sender_ssrc = ((uint32_t)rand() << 16) ^ (uint32_t)rand();

  send_rams_r(r, cfg);
  return r;
}

ssize_t fcc_client_read(fcc_client_t *r, mcast_t *main, unsigned char *buf, size_t cap) {
  struct pollfd pfd[2];
  int nfds = 0, mi, ui;
  double now;
  size_t n;

  if (r->pending_len) {
    n = r->pending_len < cap ? r->pending_len : cap;
    memcpy(buf, r->pending, n);
    r->pending_len = 0;
    return (ssize_t)n;
  }

  mi = nfds; pfd[nfds].fd = mcast_fd(main); pfd[nfds].events = POLLIN; nfds++;
  ui = nfds; pfd[nfds].fd = r->uni_fd; pfd[nfds].events = POLLIN; nfds++;

  if (poll(pfd, (nfds_t)nfds, 1000) < 0) {
    if (errno == EINTR)
      return 0;
    log_line("fcc: poll: %s", strerror(errno));
    return -1;
  }

  now = mono_seconds();

  if (pfd[mi].revents & POLLIN) {
    unsigned char raw[65536];
    ssize_t rn = mcast_recv(main, raw, sizeof raw, NULL);
    if (rn < 0)
      return -1;
    if (rn > 0) {
      rtp_hdr_t hdr;
      if (rtp_parse_header(raw, (size_t)rn, &hdr) && (size_t)rn > hdr.payload_off)
        fcc_on_multicast(r, &hdr, raw + hdr.payload_off, (size_t)rn - hdr.payload_off, now);
    }
  }

  if (pfd[ui].revents & POLLIN) {
    unsigned char raw[65536];
    ssize_t rn = recv(r->uni_fd, raw, sizeof raw, 0);
    if (rn > 0)
      fcc_on_uni(r, raw, (size_t)rn, now);
  }

  if (r->pending_len) {
    n = r->pending_len < cap ? r->pending_len : cap;
    memcpy(buf, r->pending, n);
    r->pending_len = 0;
    return (ssize_t)n;
  }
  return 0;
}

void fcc_client_close(fcc_client_t *r) {
  if (!r)
    return;
  close(r->uni_fd);
  free(r);
}
