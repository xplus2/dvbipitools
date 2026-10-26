/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#ifdef HAVE_HTTP3

#include "http3_udp.h"

#include <errno.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>

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

#define CTL_SZ (CMSG_SPACE(sizeof(uint16_t)) + CMSG_SPACE(sizeof(struct in6_pktinfo)) + CMSG_SPACE(sizeof(int)))

typedef union {
  char buf[CTL_SZ];
  struct cmsghdr align;
} ctl_t;

int h3_udp_pktinfo(int fd, int family) {
  int on = 1;
  if (family == AF_INET6) {
    (void)setsockopt(fd, IPPROTO_IPV6, IPV6_RECVTCLASS, &on, sizeof on);
    (void)setsockopt(fd, IPPROTO_IP, IP_RECVTOS, &on, sizeof on);
    return setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof on);
  }
  (void)setsockopt(fd, IPPROTO_IP, IP_RECVTOS, &on, sizeof on);
  return setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &on, sizeof on);
}

static void rx_local(struct msghdr *mh, h3_rx_t *out) {
  out->locallen = 0;
  out->ecn = 0;
  memset(&out->local, 0, sizeof out->local);
  for (struct cmsghdr *c = CMSG_FIRSTHDR(mh); c; c = CMSG_NXTHDR(mh, c)) {
    if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_TOS && c->cmsg_len >= CMSG_LEN(sizeof(uint8_t))) {
      out->ecn = *CMSG_DATA(c) & 3;
    } else if (c->cmsg_level == IPPROTO_IPV6 && c->cmsg_type == IPV6_TCLASS && c->cmsg_len >= CMSG_LEN(sizeof(int))) {
      int tc;
      memcpy(&tc, CMSG_DATA(c), sizeof tc);
      out->ecn = (uint8_t)(tc & 3);
    } else if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_PKTINFO) {
      struct in_pktinfo pi;
      struct sockaddr_in *a = (struct sockaddr_in *)&out->local;
      memcpy(&pi, CMSG_DATA(c), sizeof pi);
      a->sin_family = AF_INET;
      a->sin_addr = pi.ipi_addr;
      out->locallen = sizeof *a;
    } else if (c->cmsg_level == IPPROTO_IPV6 && c->cmsg_type == IPV6_PKTINFO) {
      struct in6_pktinfo pi;
      struct sockaddr_in6 *a = (struct sockaddr_in6 *)&out->local;
      memcpy(&pi, CMSG_DATA(c), sizeof pi);
      a->sin6_family = AF_INET6;
      a->sin6_addr = pi.ipi6_addr;
      a->sin6_scope_id = (uint32_t)pi.ipi6_ifindex;
      out->locallen = sizeof *a;
    }
  }
}

int h3_udp_recv(int fd, h3_rx_t *out, int max) {
  struct mmsghdr msgs[H3_RX_BATCH];
  struct iovec iov[H3_RX_BATCH];
  struct sockaddr_storage peers[H3_RX_BATCH];
  ctl_t ctl[H3_RX_BATCH];
  if (max > H3_RX_BATCH) max = H3_RX_BATCH;
  memset(msgs, 0, sizeof msgs[0] * (size_t)max);
  memset(ctl, 0, sizeof ctl[0] * (size_t)max);
  for (int i = 0; i < max; i++) {
    iov[i].iov_base = t_rx_buf + (size_t)i * t_rx_payload;
    iov[i].iov_len = t_rx_payload;
    msgs[i].msg_hdr.msg_iov = &iov[i];
    msgs[i].msg_hdr.msg_iovlen = 1;
    msgs[i].msg_hdr.msg_name = &peers[i];
    msgs[i].msg_hdr.msg_namelen = sizeof peers[i];
    msgs[i].msg_hdr.msg_control = ctl[i].buf;
    msgs[i].msg_hdr.msg_controllen = sizeof ctl[i].buf;
  }
  int n = recvmmsg(fd, msgs, (unsigned)max, MSG_DONTWAIT, NULL);
  if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
  for (int i = 0; i < n; i++) {
    out[i].data = t_rx_buf + (size_t)i * t_rx_payload;
    out[i].len = (msgs[i].msg_hdr.msg_flags & MSG_TRUNC) ? 0 : msgs[i].msg_len;
    out[i].peer = peers[i];
    out[i].peerlen = msgs[i].msg_hdr.msg_namelen;
    rx_local(&msgs[i].msg_hdr, &out[i]);
  }
  return n;
}

static int src_usable(const struct sockaddr_storage *dst, const struct sockaddr_storage *src) {
  if (!src || src->ss_family != dst->ss_family) return 0;
  if (src->ss_family == AF_INET) return ((const struct sockaddr_in *)src)->sin_addr.s_addr != htonl(INADDR_ANY);
  if (src->ss_family == AF_INET6) return !IN6_IS_ADDR_UNSPECIFIED(&((const struct sockaddr_in6 *)src)->sin6_addr);
  return 0;
}

static size_t cmsg_put(struct msghdr *msg, struct cmsghdr **cm, int level, int type, const void *data, size_t len) {
  (*cm)->cmsg_level = level;
  (*cm)->cmsg_type = type;
  (*cm)->cmsg_len = CMSG_LEN(len);
  memcpy(CMSG_DATA(*cm), data, len);
  *cm = CMSG_NXTHDR(msg, *cm);
  return CMSG_SPACE(len);
}

/* fills ctl with source pktinfo and optional GSO size. sets msg control fields */
static void tx_ctl(struct msghdr *msg, ctl_t *ctl, const struct sockaddr_storage *dst, const struct sockaddr_storage *src, size_t gso, uint8_t ecn) {
  size_t used = 0;
  struct cmsghdr *cm;
  memset(ctl, 0, sizeof *ctl);
  msg->msg_control = ctl->buf;
  msg->msg_controllen = sizeof ctl->buf;
  cm = CMSG_FIRSTHDR(msg);
  if (gso) {
    uint16_t v = (uint16_t)gso;
    used += cmsg_put(msg, &cm, SOL_UDP, UDP_SEGMENT, &v, sizeof v);
  }
  if (src_usable(dst, src)) {
    if (src->ss_family == AF_INET) {
      struct in_pktinfo pi;
      memset(&pi, 0, sizeof pi);
      pi.ipi_spec_dst = ((const struct sockaddr_in *)src)->sin_addr;
      used += cmsg_put(msg, &cm, IPPROTO_IP, IP_PKTINFO, &pi, sizeof pi);
    } else {
      struct in6_pktinfo pi;
      memset(&pi, 0, sizeof pi);
      pi.ipi6_addr = ((const struct sockaddr_in6 *)src)->sin6_addr;
      pi.ipi6_ifindex = ((const struct sockaddr_in6 *)src)->sin6_scope_id;
      used += cmsg_put(msg, &cm, IPPROTO_IPV6, IPV6_PKTINFO, &pi, sizeof pi);
    }
  }
  if (ecn) {
    int v = ecn;
    if (dst->ss_family == AF_INET6) used += cmsg_put(msg, &cm, IPPROTO_IPV6, IPV6_TCLASS, &v, sizeof v);
    else used += cmsg_put(msg, &cm, IPPROTO_IP, IP_TOS, &v, sizeof v);
  }
  msg->msg_controllen = used;
  if (!used) msg->msg_control = NULL;
}

static void send_each(int fd, const struct sockaddr_storage *dst, socklen_t dstlen, const struct sockaddr_storage *src, const uint8_t *buf, size_t total, size_t seg, uint8_t ecn) {
  struct mmsghdr msgs[H3_GSO_MAX_SEGS];
  struct iovec iov[H3_GSO_MAX_SEGS];
  ctl_t ctl;
  struct msghdr tmpl;
  memset(&tmpl, 0, sizeof tmpl);
  tx_ctl(&tmpl, &ctl, dst, src, 0, ecn);
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
    msgs[n].msg_hdr.msg_control = tmpl.msg_control;
    msgs[n].msg_hdr.msg_controllen = tmpl.msg_controllen;
    n++;
    off += len;
  }
  if (n > 0) (void)sendmmsg(fd, msgs, (unsigned)n, 0);
}

static int send_gso(int fd, const struct sockaddr_storage *dst, socklen_t dstlen, const struct sockaddr_storage *src, const uint8_t *buf, size_t total, size_t seg, uint8_t ecn) {
  ctl_t ctl;
  struct iovec iov = {.iov_base = (void *)buf, .iov_len = total};
  struct msghdr msg;
  memset(&msg, 0, sizeof msg);
  msg.msg_name = (void *)dst;
  msg.msg_namelen = dstlen;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  tx_ctl(&msg, &ctl, dst, src, seg, ecn);
  return sendmsg(fd, &msg, 0) < 0 ? -1 : 0;
}

void h3_udp_send(int fd, const struct sockaddr_storage *dst, socklen_t dstlen, const struct sockaddr_storage *src, const uint8_t *buf, size_t total, size_t seg, uint8_t ecn) {
  if (total == 0) return;
  if (t_gso && total > seg && seg > 0) {
    if (send_gso(fd, dst, dstlen, src, buf, total, seg, ecn) == 0) return;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS) return;
    t_gso = 0;
  }
  send_each(fd, dst, dstlen, src, buf, total, seg > 0 ? seg : total, ecn);
}

#endif /* HAVE_HTTP3 */
