/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "../log.h"
#include "multicast.h"

struct mcast {
  int fd;
  int family;
  int sender; /* nonzero: send-side, mreq unused, no leave on close */
  union {
    struct ip_mreqn v4;
    struct ipv6_mreq v6;
  } mreq; /* for leave on close */
  union {
    struct sockaddr_in v4;
    struct sockaddr_in6 v6;
  } dest; /* send-side only */
};

mcast_t *mcast_open(int family, const char *group, unsigned port, const char *iface, int recv_timeout_ms) {
  mcast_t *m = calloc(1, sizeof *m);
  unsigned ifidx = 0;
  int on = 1;
  struct timeval tv; /* recv timeout; caller polls deadline */

  tv.tv_sec = recv_timeout_ms / 1000;
  tv.tv_usec = (recv_timeout_ms % 1000) * 1000;

  if (!m)
    return NULL;
  m->family = family;
  m->fd = socket(family, SOCK_DGRAM, IPPROTO_UDP);
  if (m->fd < 0) {
    log_line("socket: %s", strerror(errno));
    free(m);
    return NULL;
  }
  setsockopt(m->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  {
    int rcvbuf = 4 * 1024 * 1024;   /* absorb brief stalls, e.g. slow -v terminal */
    setsockopt(m->fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
  }
  if (iface) {
    ifidx = if_nametoindex(iface);
    if (!ifidx) {
      log_line("unknown interface: %s", iface);
      goto fail;
    }
  }

  if (family == AF_INET) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(m->fd, (struct sockaddr *)&a, sizeof a) < 0) {
      log_line("bind: %s", strerror(errno));
      goto fail;
    }
    inet_pton(AF_INET, group, &m->mreq.v4.imr_multiaddr);
    m->mreq.v4.imr_address.s_addr = htonl(INADDR_ANY);
    m->mreq.v4.imr_ifindex = (int)ifidx;
    if (setsockopt(m->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m->mreq.v4, sizeof m->mreq.v4) < 0) {
      log_line("join %s: %s", group, strerror(errno));
      goto fail;
    }
  } else {
    struct sockaddr_in6 a;
    memset(&a, 0, sizeof a);
    a.sin6_family = AF_INET6;
    a.sin6_port = htons((unsigned short)port);
    if (bind(m->fd, (struct sockaddr *)&a, sizeof a) < 0) {
      log_line("bind: %s", strerror(errno));
      goto fail;
    }
    inet_pton(AF_INET6, group, &m->mreq.v6.ipv6mr_multiaddr);
    m->mreq.v6.ipv6mr_interface = ifidx;
    if (setsockopt(m->fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &m->mreq.v6, sizeof m->mreq.v6) < 0) {
      log_line("join %s: %s", group, strerror(errno));
      goto fail;
    }
  }
  setsockopt(m->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  return m;

fail:
  if (m->fd >= 0)
    close(m->fd);
  free(m);
  return NULL;
}

ssize_t mcast_recv(mcast_t *m, void *buf, size_t cap) {
  ssize_t n = recv(m->fd, buf, cap, 0);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return 0;
    log_line("recv: %s", strerror(errno));
    return -1;
  }
  return n;
}

mcast_t *mcast_open_send(int family, const char *group, unsigned port, const char *iface, int ttl) {
  mcast_t *m = calloc(1, sizeof *m);
  unsigned ifidx = 0;

  if (!m)
    return NULL;
  m->family = family;
  m->sender = 1;
  m->fd = socket(family, SOCK_DGRAM, IPPROTO_UDP);
  if (m->fd < 0) {
    log_line("socket: %s", strerror(errno));
    free(m);
    return NULL;
  }
  if (iface) {
    ifidx = if_nametoindex(iface);
    if (!ifidx) {
      log_line("unknown interface: %s", iface);
      goto fail;
    }
  }

  if (family == AF_INET) {
    struct ip_mreqn mif;
    memset(&mif, 0, sizeof mif);
    mif.imr_ifindex = (int)ifidx;
    if (ifidx && setsockopt(m->fd, IPPROTO_IP, IP_MULTICAST_IF, &mif, sizeof mif) < 0) {
      log_line("set outgoing interface: %s", strerror(errno));
      goto fail;
    }
    if (ttl > 0 && setsockopt(m->fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl) < 0) {
      log_line("set ttl: %s", strerror(errno));
      goto fail;
    }
    memset(&m->dest.v4, 0, sizeof m->dest.v4);
    m->dest.v4.sin_family = AF_INET;
    m->dest.v4.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, group, &m->dest.v4.sin_addr) != 1) {
      log_line("bad group address: %s", group);
      goto fail;
    }
  } else {
    if (ifidx && setsockopt(m->fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifidx, sizeof ifidx) < 0) {
      log_line("set outgoing interface: %s", strerror(errno));
      goto fail;
    }
    if (ttl > 0 && setsockopt(m->fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl, sizeof ttl) < 0) {
      log_line("set hop limit: %s", strerror(errno));
      goto fail;
    }
    memset(&m->dest.v6, 0, sizeof m->dest.v6);
    m->dest.v6.sin6_family = AF_INET6;
    m->dest.v6.sin6_port = htons((unsigned short)port);
    if (inet_pton(AF_INET6, group, &m->dest.v6.sin6_addr) != 1) {
      log_line("bad group address: %s", group);
      goto fail;
    }
  }
  return m;

fail:
  if (m->fd >= 0)
    close(m->fd);
  free(m);
  return NULL;
}

ssize_t mcast_send(mcast_t *m, const void *buf, size_t len) {
  const struct sockaddr *sa;
  socklen_t salen;
  ssize_t n;

  if (m->family == AF_INET) {
    sa = (const struct sockaddr *)&m->dest.v4;
    salen = sizeof m->dest.v4;
  } else {
    sa = (const struct sockaddr *)&m->dest.v6;
    salen = sizeof m->dest.v6;
  }
  n = sendto(m->fd, buf, len, 0, sa, salen);
  if (n < 0) {
    log_line("sendto: %s", strerror(errno));
    return -1;
  }
  return n;
}

void mcast_close(mcast_t *m) {
  if (!m)
    return;
  if (m->fd >= 0) {
    if (!m->sender) {
      if (m->family == AF_INET)
        setsockopt(m->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &m->mreq.v4, sizeof m->mreq.v4);
      else
        setsockopt(m->fd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &m->mreq.v6, sizeof m->mreq.v6);
    }
    close(m->fd);
  }
  free(m);
}
