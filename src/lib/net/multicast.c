/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../helper/log.h"
#include "multicast.h"
#include "netconnect.h"

#define MCAST_BATCH_MAX 32
#define MCAST_BATCH_SLOT_MIN 2048
#define MCAST_BATCH_SLOT_MAX 65536

typedef struct {
  int n;
  int head;
  int count;
  size_t slot_sz;
  size_t want_sz;
  unsigned char *slots;
  size_t len[MCAST_BATCH_MAX];
  uint64_t ts[MCAST_BATCH_MAX];
  struct mmsghdr msg[MCAST_BATCH_MAX];
  struct iovec iov[MCAST_BATCH_MAX];
  union {
    struct cmsghdr align;
    char space[CMSG_SPACE(sizeof(struct timespec))];
  } ctl[MCAST_BATCH_MAX];
} mcast_batch_t;

struct mcast {
  int fd;
  int family;
  int sender; /* nonzero: send-side, mreq unused, no leave on close */
  int ssm; /* nonzero: joined via gsr below, not mreq */
  union {
    struct ip_mreqn v4;
    struct ipv6_mreq v6;
  } mreq; /* for leave on close, ASM join */
  struct group_source_req gsr; /* for leave on close, SSM join */
  union {
    struct sockaddr_in v4;
    struct sockaddr_in6 v6;
  } dest; /* send-side only */
  int ts_on;
  uint64_t last_rx_ns;
  mcast_batch_t *batch;
  int no_gso;
  log_throttle_t send_fail_throttle;
  log_throttle_t trunc_throttle;
};

/* socket+SO_REUSEADDR+SO_RCVBUF+iface resolve+bind, shared ASM/SSM open. binds group: same port same process inputs else all
   eligible for each other's traffic (IP_MULTICAST_ALL), kernel picks winner per packet not per destination.
   -1 fail, *ifidx_out from iface (0 if NULL) */
static int open_bound_recv_socket(int family, const char *group, unsigned port, const char *iface, unsigned *ifidx_out) {
  int fd;
  int on = 1;
  int off = 0;
  int rcvbuf = 4 * 1024 * 1024; /* absorb brief stalls, e.g. slow -v terminal */

  *ifidx_out = 0;
  fd = socket(family, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    log_code("LE500103", "socket: %s", strerror(errno));
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf) < 0) {
    log_code("LE500146", "setsockopt SO_RCVBUF: %s", strerror(errno));
  } else {
    int actual = 0;
    socklen_t actual_len = sizeof actual;
    if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &actual, &actual_len) == 0 && actual < rcvbuf)
      log_code("LW500147", "SO_RCVBUF capped to %d bytes (wanted %d; raise net.core.rmem_max)", actual, rcvbuf);
  }
  if (iface) {
    *ifidx_out = if_nametoindex(iface);
    if (!*ifidx_out) {
      log_code("LE000311", "unknown interface: %s", iface);
      close(fd);
      return -1;
    }
  }
  if (family == AF_INET) {
    struct sockaddr_in a;
    /* belt-and-suspenders w/ group bind below: wildcard bind else sees every
       group any host socket joined, not own (ip(7)) */
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_ALL, &off, sizeof off);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, group, &a.sin_addr) != 1) {
      log_code("LE000312", "bad group address: %s", group);
      close(fd);
      return -1;
    }
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
      log_code("LE500148", "bind: %s", strerror(errno));
      close(fd);
      return -1;
    }
  } else {
    struct sockaddr_in6 a;
    memset(&a, 0, sizeof a);
    a.sin6_family = AF_INET6;
    a.sin6_port = htons((unsigned short)port);
    if (inet_pton(AF_INET6, group, &a.sin6_addr) != 1) {
      log_code("LE000312", "bad group address: %s", group);
      close(fd);
      return -1;
    }
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
      log_code("LE500148", "bind: %s", strerror(errno));
      close(fd);
      return -1;
    }
  }
  return fd;
}

mcast_t *mcast_open(int family, const char *group, unsigned port, const char *iface, int recv_timeout_ms) {
  mcast_t *m = calloc(1, sizeof *m);
  unsigned ifidx;
  struct timeval tv; /* recv timeout, caller polls deadline */

  tv.tv_sec = recv_timeout_ms / 1000;
  tv.tv_usec = (recv_timeout_ms % 1000) * 1000;

  if (!m)
    return NULL;
  m->family = family;
  m->fd = open_bound_recv_socket(family, group, port, iface, &ifidx);
  if (m->fd < 0) {
    free(m);
    return NULL;
  }

  if (family == AF_INET) {
    inet_pton(AF_INET, group, &m->mreq.v4.imr_multiaddr);
    m->mreq.v4.imr_address.s_addr = htonl(INADDR_ANY);
    m->mreq.v4.imr_ifindex = (int)ifidx;
    if (setsockopt(m->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m->mreq.v4, sizeof m->mreq.v4) < 0) {
      log_code("LE500149", "join %s: %s", group, strerror(errno));
      goto fail;
    }
  } else {
    inet_pton(AF_INET6, group, &m->mreq.v6.ipv6mr_multiaddr);
    m->mreq.v6.ipv6mr_interface = ifidx;
    if (setsockopt(m->fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &m->mreq.v6, sizeof m->mreq.v6) < 0) {
      log_code("LE500149", "join %s: %s", group, strerror(errno));
      goto fail;
    }
  }
  setsockopt(m->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  return m;

fail:
  close(m->fd);
  free(m);
  return NULL;
}

mcast_t *mcast_open_ssm(int family, const char *group, unsigned port, const char *source_addr, const char *iface, int recv_timeout_ms) {
  mcast_t *m = calloc(1, sizeof *m);
  unsigned ifidx;
  int level = (family == AF_INET) ? IPPROTO_IP : IPPROTO_IPV6;
  struct timeval tv;
  socklen_t sslen;

  tv.tv_sec = recv_timeout_ms / 1000;
  tv.tv_usec = (recv_timeout_ms % 1000) * 1000;

  if (!m)
    return NULL;
  m->family = family;
  m->ssm = 1;
  m->fd = open_bound_recv_socket(family, group, port, iface, &ifidx);
  if (m->fd < 0) {
    free(m);
    return NULL;
  }

  memset(&m->gsr, 0, sizeof m->gsr);
  m->gsr.gsr_interface = ifidx;
  if (netaddr_fill(family, group, port, &m->gsr.gsr_group, &sslen)) {
    log_code("LE000312", "bad group address: %s", group);
    goto fail;
  }
  if (netaddr_fill(family, source_addr, 0, &m->gsr.gsr_source, &sslen)) {
    log_code("LE000313", "bad source address: %s", source_addr);
    goto fail;
  }
  if (setsockopt(m->fd, level, MCAST_JOIN_SOURCE_GROUP, &m->gsr, sizeof m->gsr) < 0) {
    log_code("LE500150", "join %s from %s: %s", group, source_addr, strerror(errno));
    goto fail;
  }
  setsockopt(m->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  return m;

fail:
  close(m->fd);
  free(m);
  return NULL;
}

mcast_t *mcast_open_src(int family, const char *group, unsigned port, const char *source, const char *iface, int recv_timeout_ms) {
  if (source && source[0]) return mcast_open_ssm(family, group, port, source, iface, recv_timeout_ms);
  return mcast_open(family, group, port, iface, recv_timeout_ms);
}

int mcast_enable_rx_timestamps(mcast_t *m) {
  int on = 1;
  if (setsockopt(m->fd, SOL_SOCKET, SO_TIMESTAMPNS, &on, sizeof on) < 0) return -1;
  m->ts_on = 1;
  return 0;
}

uint64_t mcast_last_rx_ns(const mcast_t *m) {
  return m->last_rx_ns;
}

int mcast_set_batch(mcast_t *m, int n) {
  mcast_batch_t *b;
  if (m->batch) return 0;
  if (n < 2) return 0;
  if (n > MCAST_BATCH_MAX) n = MCAST_BATCH_MAX;
  b = calloc(1, sizeof *b);
  if (!b) return -1;
  b->slots = malloc((size_t)(n - 1) * MCAST_BATCH_SLOT_MIN);
  if (!b->slots) {
    free(b);
    return -1;
  }
  b->n = n;
  b->slot_sz = MCAST_BATCH_SLOT_MIN;
  b->want_sz = MCAST_BATCH_SLOT_MIN;
  m->batch = b;
  return 0;
}

static uint64_t cmsg_rx_ns(struct msghdr *mh) {
  for (struct cmsghdr *c = CMSG_FIRSTHDR(mh); c; c = CMSG_NXTHDR(mh, c)) {
    if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_TIMESTAMPNS && c->cmsg_len >= CMSG_LEN(sizeof(struct timespec))) {
      struct timespec ts;
      memcpy(&ts, CMSG_DATA(c), sizeof ts);
      return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
  }
  return 0;
}

static void batch_grow(mcast_batch_t *b) {
  unsigned char *p;
  if (b->want_sz <= b->slot_sz) return;
  p = realloc(b->slots, (size_t)(b->n - 1) * b->want_sz);
  if (!p) return;
  b->slots = p;
  b->slot_sz = b->want_sz;
}

static ssize_t batch_recv(mcast_t *m, void *buf, size_t cap, net_err_reason_t *reason_out) {
  mcast_batch_t *b = m->batch;
  int r;
  for (;;) {
    while (b->head < b->count) {
      int i = b->head++;
      if (b->len[i] > cap || b->len[i] > b->slot_sz) {
        log_throttled(&m->trunc_throttle, LOG_THROTTLE_WINDOW_S, "LW500148", "datagram larger than %zu byte buffer dropped", b->len[i] > cap ? cap : b->slot_sz);
        continue;
      }
      memcpy(buf, b->slots + (size_t)(i - 1) * b->slot_sz, b->len[i]);
      m->last_rx_ns = b->ts[i];
      return (ssize_t)b->len[i];
    }
    batch_grow(b);
    for (int i = 0; i < b->n; i++) {
      memset(&b->msg[i].msg_hdr, 0, sizeof b->msg[i].msg_hdr);
      b->iov[i].iov_base = i ? b->slots + (size_t)(i - 1) * b->slot_sz : buf;
      b->iov[i].iov_len = i ? b->slot_sz : cap;
      b->msg[i].msg_hdr.msg_iov = &b->iov[i];
      b->msg[i].msg_hdr.msg_iovlen = 1;
      if (m->ts_on) {
        b->msg[i].msg_hdr.msg_control = b->ctl[i].space;
        b->msg[i].msg_hdr.msg_controllen = sizeof b->ctl[i].space;
      }
    }
    r = recvmmsg(m->fd, b->msg, (unsigned)b->n, MSG_WAITFORONE | MSG_TRUNC, NULL);
    b->head = 1;
    b->count = r > 0 ? r : 0;
    if (r < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
      log_code("LE500145", "recv: %s", strerror(errno));
      if (reason_out) *reason_out = NET_ERR_READ;
      return -1;
    }
    for (int i = 0; i < r; i++) {
      b->len[i] = b->msg[i].msg_len;
      b->ts[i] = m->ts_on ? cmsg_rx_ns(&b->msg[i].msg_hdr) : 0;
      if (i && b->len[i] > b->slot_sz && b->len[i] <= MCAST_BATCH_SLOT_MAX && b->len[i] > b->want_sz) b->want_sz = b->len[i];
    }
    if (b->len[0] > cap) {
      log_throttled(&m->trunc_throttle, LOG_THROTTLE_WINDOW_S, "LW500148", "datagram larger than %zu byte buffer dropped", cap);
      continue;
    }
    m->last_rx_ns = b->ts[0];
    return (ssize_t)b->len[0];
  }
}

static ssize_t recv_stamped(mcast_t *m, void *buf, size_t cap) {
  struct iovec iov = {buf, cap};
  union {
    struct cmsghdr align;
    char space[CMSG_SPACE(sizeof(struct timespec))];
  } ctl;
  struct msghdr mh;
  ssize_t n;

  memset(&mh, 0, sizeof mh);
  mh.msg_iov = &iov;
  mh.msg_iovlen = 1;
  mh.msg_control = ctl.space;
  mh.msg_controllen = sizeof ctl.space;
  n = recvmsg(m->fd, &mh, 0);
  if (n < 0) return n;
  if (mh.msg_flags & MSG_TRUNC) return -2;
  m->last_rx_ns = 0;
  for (struct cmsghdr *c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c)) {
    if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_TIMESTAMPNS && c->cmsg_len >= CMSG_LEN(sizeof(struct timespec))) {
      struct timespec ts;
      memcpy(&ts, CMSG_DATA(c), sizeof ts);
      m->last_rx_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
  }
  return n;
}

ssize_t mcast_recv(mcast_t *m, void *buf, size_t cap, net_err_reason_t *reason_out) {
  ssize_t n;
  if (m->batch) return batch_recv(m, buf, cap, reason_out);
  n = m->ts_on ? recv_stamped(m, buf, cap) : recv(m->fd, buf, cap, MSG_TRUNC);
  if (n == -2 || n > (ssize_t)cap) {
    log_throttled(&m->trunc_throttle, LOG_THROTTLE_WINDOW_S, "LW500148", "datagram larger than %zu byte buffer dropped", cap);
    return 0;
  }
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    log_code("LE500145", "recv: %s", strerror(errno));
    if (reason_out) *reason_out = NET_ERR_READ;
    return -1;
  }
  return n;
}

int mcast_fd(const mcast_t *m) { return m->fd; }

int mcast_family(const mcast_t *m) { return m->family; }

int mcast_set_nonblock(const mcast_t *m) {
  int flags = fcntl(m->fd, F_GETFL, 0);
  if (flags < 0) return -1;
  return fcntl(m->fd, F_SETFL, flags | O_NONBLOCK);
}

mcast_t *mcast_open_send(int family, const char *group, unsigned port, const char *iface, int ttl) {
  mcast_t *m = calloc(1, sizeof *m);
  unsigned ifidx = 0;

  if (!m) return NULL;
  m->family = family;
  m->sender = 1;
  m->fd = socket(family, SOCK_DGRAM, IPPROTO_UDP);
  if (m->fd < 0) {
    log_code("LE500103", "socket: %s", strerror(errno));
    free(m);
    return NULL;
  }
  /* no SO_SNDBUF bump: blocking socket + small backlog caps burst size below
     policer drop threshold. deliberate. */
  if (iface) {
    ifidx = if_nametoindex(iface);
    if (!ifidx) {
      log_code("LE000311", "unknown interface: %s", iface);
      goto fail;
    }
  }

  if (family == AF_INET) {
    struct ip_mreqn mif;
    struct sockaddr_storage ss;
    socklen_t sslen;
    memset(&mif, 0, sizeof mif);
    mif.imr_ifindex = (int)ifidx;
    if (ifidx && setsockopt(m->fd, IPPROTO_IP, IP_MULTICAST_IF, &mif, sizeof mif) < 0) {
      log_code("LE500151", "set outgoing interface: %s", strerror(errno));
      goto fail;
    }
    if (ttl > 0 && setsockopt(m->fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl) < 0) {
      log_code("LE500152", "set ttl: %s", strerror(errno));
      goto fail;
    }
    if (netaddr_fill(AF_INET, group, port, &ss, &sslen)) {
      log_code("LE000312", "bad group address: %s", group);
      goto fail;
    }
    memcpy(&m->dest.v4, &ss, sslen);
  } else {
    struct sockaddr_storage ss;
    socklen_t sslen;
    if (ifidx && setsockopt(m->fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifidx, sizeof ifidx) < 0) {
      log_code("LE500151", "set outgoing interface: %s", strerror(errno));
      goto fail;
    }
    if (ttl > 0 && setsockopt(m->fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl, sizeof ttl) < 0) {
      log_code("LE500153", "set hop limit: %s", strerror(errno));
      goto fail;
    }
    if (netaddr_fill(AF_INET6, group, port, &ss, &sslen)) {
      log_code("LE000312", "bad group address: %s", group);
      goto fail;
    }
    memcpy(&m->dest.v6, &ss, sslen);
  }
  return m;

fail:
  if (m->fd >= 0) close(m->fd);
  free(m);
  return NULL;
}

static const struct sockaddr *dest_sa(const mcast_t *m, socklen_t *salen) {
  if (m->family == AF_INET) {
    *salen = sizeof m->dest.v4;
    return (const struct sockaddr *)&m->dest.v4;
  }
  *salen = sizeof m->dest.v6;
  return (const struct sockaddr *)&m->dest.v6;
}

ssize_t mcast_send(mcast_t *m, const void *buf, size_t len) {
  const struct sockaddr *sa;
  socklen_t salen;
  ssize_t n;

  sa = dest_sa(m, &salen);
  n = sendto(m->fd, buf, len, 0, sa, salen);
  if (n < 0) {
    log_throttled(&m->send_fail_throttle, LOG_THROTTLE_WINDOW_S, "LE100127", "sendto: %s", strerror(errno));
    return -1;
  }
  return n;
}

ssize_t mcast_send_tos(mcast_t *m, const void *buf, size_t len, int tos) {
  const struct sockaddr *sa;
  socklen_t salen;
  ssize_t n;

  sa = dest_sa(m, &salen);
  n = net_sendto_tos(m->fd, buf, len, sa, salen, tos);
  if (n < 0) {
    log_throttled(&m->send_fail_throttle, LOG_THROTTLE_WINDOW_S, "LE100128", "sendmsg: %s", strerror(errno));
    return -1;
  }
  return n;
}

int mcast_set_tos(mcast_t *m, int tos) {
  return net_set_dscp(m->fd, m->family, tos);
}

void mcast_close(mcast_t *m) {
  if (!m) return;
  if (m->fd >= 0) {
    if (m->ssm) {
      int level = (m->family == AF_INET) ? IPPROTO_IP : IPPROTO_IPV6;
      setsockopt(m->fd, level, MCAST_LEAVE_SOURCE_GROUP, &m->gsr, sizeof m->gsr);
    } else if (!m->sender) {
      if (m->family == AF_INET) setsockopt(m->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &m->mreq.v4, sizeof m->mreq.v4);
      else                      setsockopt(m->fd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &m->mreq.v6, sizeof m->mreq.v6);
    }
    close(m->fd);
  }
  if (m->batch) free(m->batch->slots);
  free(m->batch);
  free(m);
}

static int gso_send(mcast_t *m, const void *const *hdr, size_t hdr_len, const void *const *payload, const size_t *len, int n) {
  struct iovec iov[2 * MCAST_SEND_BATCH_MAX];
  union {
    struct cmsghdr align;
    char space[CMSG_SPACE(sizeof(uint16_t))];
  } ctl;
  struct msghdr msg;
  struct cmsghdr *c;
  uint16_t seg = (uint16_t)(hdr_len + len[0]);
  socklen_t salen;
  int k = 0;
  memset(&msg, 0, sizeof msg);
  msg.msg_name = (void *)dest_sa(m, &salen);
  msg.msg_namelen = salen;
  for (int i = 0; i < n; i++) {
    if (hdr_len) {
      iov[k].iov_base = (void *)hdr[i];
      iov[k++].iov_len = hdr_len;
    }
    iov[k].iov_base = (void *)payload[i];
    iov[k++].iov_len = len[i];
  }
  msg.msg_iov = iov;
  msg.msg_iovlen = (size_t)k;
  msg.msg_control = ctl.space;
  msg.msg_controllen = sizeof ctl.space;
  c = CMSG_FIRSTHDR(&msg);
  c->cmsg_level = IPPROTO_UDP;
  c->cmsg_type = UDP_SEGMENT;
  c->cmsg_len = CMSG_LEN(sizeof seg);
  memcpy(CMSG_DATA(c), &seg, sizeof seg);
  return sendmsg(m->fd, &msg, 0) < 0 ? -1 : 0;
}

static int mmsg_send(mcast_t *m, const void *const *hdr, size_t hdr_len, const void *const *payload, const size_t *len, int n) {
  struct iovec iov[MCAST_SEND_BATCH_MAX][2];
  struct mmsghdr msgs[MCAST_SEND_BATCH_MAX];
  socklen_t salen;
  const struct sockaddr *sa = dest_sa(m, &salen);
  int done = 0;
  memset(msgs, 0, sizeof msgs[0] * (size_t)n);
  for (int i = 0; i < n; i++) {
    int k = 0;
    if (hdr_len) {
      iov[i][k].iov_base = (void *)hdr[i];
      iov[i][k++].iov_len = hdr_len;
    }
    iov[i][k].iov_base = (void *)payload[i];
    iov[i][k++].iov_len = len[i];
    msgs[i].msg_hdr.msg_name = (void *)sa;
    msgs[i].msg_hdr.msg_namelen = salen;
    msgs[i].msg_hdr.msg_iov = iov[i];
    msgs[i].msg_hdr.msg_iovlen = (size_t)k;
  }
  while (done < n) {
    int r = sendmmsg(m->fd, msgs + done, (unsigned)(n - done), 0);
    if (r < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    done += r;
  }
  return 0;
}

int mcast_send_batch(mcast_t *m, const void *const *hdr, size_t hdr_len, const void *const *payload, const size_t *len, int n) {
  int uniform = 1;
  size_t total = 0;
  if (n <= 0) return 0;
  if (n > MCAST_SEND_BATCH_MAX) return -1;
  for (int i = 0; i < n; i++) {
    total += hdr_len + len[i];
    if (i + 1 < n ? len[i] != len[0] : len[i] > len[0]) uniform = 0;
  }
  if (n > 1 && uniform && !m->no_gso && total <= 65000 && hdr_len + len[0] <= UINT16_MAX) {
    if (!gso_send(m, hdr, hdr_len, payload, len, n)) return 0;
    if (errno != EIO && errno != EINVAL && errno != ENOPROTOOPT && errno != EOPNOTSUPP) {
      log_throttled(&m->send_fail_throttle, LOG_THROTTLE_WINDOW_S, "LE100128", "sendmsg: %s", strerror(errno));
      return -1;
    }
    m->no_gso = 1;
  }
  if (mmsg_send(m, hdr, hdr_len, payload, len, n)) {
    log_throttled(&m->send_fail_throttle, LOG_THROTTLE_WINDOW_S, "LE100128", "sendmsg: %s", strerror(errno));
    return -1;
  }
  return 0;
}
