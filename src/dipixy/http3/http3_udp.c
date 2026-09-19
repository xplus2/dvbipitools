/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#ifdef HAVE_HTTP3

#include "http3_udp.h"

#include <errno.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

#ifndef UDP_SEGMENT
#define UDP_SEGMENT 103
#endif
#ifndef SOL_UDP
#define SOL_UDP 17
#endif

static _Thread_local uint8_t *t_rx_buf;
static _Thread_local size_t t_rx_payload;
static _Thread_local uint8_t *t_tx_buf;
static _Thread_local size_t t_tx_cap;
static _Thread_local int t_gso;
static _Thread_local int t_gso_probed;

int h3_udp_init(size_t payload) {
  if (t_rx_buf) return 0;
  t_rx_payload = payload;
  t_rx_buf = malloc(payload * H3_RX_BATCH);
  t_tx_cap = H3_UDP_MAX_PAYLOAD;
  t_tx_buf = malloc(t_tx_cap);
  if (!t_rx_buf || !t_tx_buf) {
    h3_udp_free();
    return -1;
  }
  return 0;
}

void h3_udp_free(void) {
  free(t_rx_buf);
  free(t_tx_buf);
  t_rx_buf = NULL;
  t_tx_buf = NULL;
  t_rx_payload = 0;
  t_tx_cap = 0;
}

size_t h3_udp_tx_cap(void) {
  return t_tx_cap;
}

uint8_t *h3_udp_tx_buf(void) {
  return t_tx_buf;
}

int h3_udp_gso_probe(int fd) {
  int seg = 0;
  socklen_t len = sizeof seg;
  int ok = getsockopt(fd, SOL_UDP, UDP_SEGMENT, &seg, &len) == 0;
  t_gso = t_gso_probed ? (t_gso && ok) : ok;
  t_gso_probed = 1;
  return t_gso;
}

void h3_udp_set_gso(int on) {
  t_gso = on;
}

int h3_udp_recv(int fd, h3_rx_t *out, int max) {
  struct mmsghdr msgs[H3_RX_BATCH];
  struct iovec iov[H3_RX_BATCH];
  struct sockaddr_storage peers[H3_RX_BATCH];
  if (max > H3_RX_BATCH) max = H3_RX_BATCH;
  memset(msgs, 0, sizeof msgs[0] * (size_t)max);
  for (int i = 0; i < max; i++) {
    iov[i].iov_base = t_rx_buf + (size_t)i * t_rx_payload;
    iov[i].iov_len = t_rx_payload;
    msgs[i].msg_hdr.msg_iov = &iov[i];
    msgs[i].msg_hdr.msg_iovlen = 1;
    msgs[i].msg_hdr.msg_name = &peers[i];
    msgs[i].msg_hdr.msg_namelen = sizeof peers[i];
  }
  int n = recvmmsg(fd, msgs, (unsigned)max, MSG_DONTWAIT, NULL);
  if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
  for (int i = 0; i < n; i++) {
    out[i].data = t_rx_buf + (size_t)i * t_rx_payload;
    out[i].len = (msgs[i].msg_hdr.msg_flags & MSG_TRUNC) ? 0 : msgs[i].msg_len;
    out[i].peer = peers[i];
    out[i].peerlen = msgs[i].msg_hdr.msg_namelen;
  }
  return n;
}

static void send_each(int fd, const struct sockaddr_storage *dst, socklen_t dstlen, const uint8_t *buf, size_t total, size_t seg) {
  struct mmsghdr msgs[H3_GSO_MAX_SEGS];
  struct iovec iov[H3_GSO_MAX_SEGS];
  size_t off = 0;
  int n = 0;
  while (off < total) {
    size_t len = total - off < seg ? total - off : seg;
    if (n == H3_GSO_MAX_SEGS) {
      (void)sendmmsg(fd, msgs, (unsigned)n, 0);
      n = 0;
    }
    iov[n].iov_base = (void *)(buf + off);
    iov[n].iov_len = len;
    memset(&msgs[n], 0, sizeof msgs[n]);
    msgs[n].msg_hdr.msg_name = (void *)dst;
    msgs[n].msg_hdr.msg_namelen = dstlen;
    msgs[n].msg_hdr.msg_iov = &iov[n];
    msgs[n].msg_hdr.msg_iovlen = 1;
    n++;
    off += len;
  }
  if (n > 0) (void)sendmmsg(fd, msgs, (unsigned)n, 0);
}

static int send_gso(int fd, const struct sockaddr_storage *dst, socklen_t dstlen, const uint8_t *buf, size_t total, size_t seg) {
  union {
    char buf[CMSG_SPACE(sizeof(uint16_t))];
    struct cmsghdr align;
  } ctl;
  struct iovec iov = {.iov_base = (void *)buf, .iov_len = total};
  struct msghdr msg;
  memset(&msg, 0, sizeof msg);
  memset(&ctl, 0, sizeof ctl);
  msg.msg_name = (void *)dst;
  msg.msg_namelen = dstlen;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = ctl.buf;
  msg.msg_controllen = sizeof ctl.buf;
  struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
  cm->cmsg_level = SOL_UDP;
  cm->cmsg_type = UDP_SEGMENT;
  cm->cmsg_len = CMSG_LEN(sizeof(uint16_t));
  uint16_t gso = (uint16_t)seg;
  memcpy(CMSG_DATA(cm), &gso, sizeof gso);
  return sendmsg(fd, &msg, 0) < 0 ? -1 : 0;
}

void h3_udp_send(int fd, const struct sockaddr_storage *dst, socklen_t dstlen, const uint8_t *buf, size_t total, size_t seg) {
  if (total == 0) return;
  if (t_gso && total > seg && seg > 0) {
    if (send_gso(fd, dst, dstlen, buf, total, seg) == 0) return;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS) return;
    t_gso = 0;
  }
  send_each(fd, dst, dstlen, buf, total, seg > 0 ? seg : total);
}

#endif /* HAVE_HTTP3 */
