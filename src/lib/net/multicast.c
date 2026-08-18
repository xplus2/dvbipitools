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
#include "netconnect.h"

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
};

/* socket+SO_REUSEADDR+SO_RCVBUF+iface resolve+bind, shared by ASM and SSM open.
   -1 on fail, *ifidx_out set from iface (0 if iface is NULL) */
static int open_bound_recv_socket(int family, unsigned port, const char *iface, unsigned *ifidx_out) {
  int fd, on = 1;
  int rcvbuf = 4 * 1024 * 1024; /* absorb brief stalls, e.g. slow -v terminal */

  *ifidx_out = 0;
  fd = socket(family, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    log_line("socket: %s", strerror(errno));
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
  if (iface) {
    *ifidx_out = if_nametoindex(iface);
    if (!*ifidx_out) {
      log_line("unknown interface: %s", iface);
      close(fd);
      return -1;
    }
  }
  if (family == AF_INET) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
      log_line("bind: %s", strerror(errno));
      close(fd);
      return -1;
    }
  } else {
    struct sockaddr_in6 a;
    memset(&a, 0, sizeof a);
    a.sin6_family = AF_INET6;
    a.sin6_port = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
      log_line("bind: %s", strerror(errno));
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
  m->fd = open_bound_recv_socket(family, port, iface, &ifidx);
  if (m->fd < 0) {
    free(m);
    return NULL;
  }

  if (family == AF_INET) {
    inet_pton(AF_INET, group, &m->mreq.v4.imr_multiaddr);
    m->mreq.v4.imr_address.s_addr = htonl(INADDR_ANY);
    m->mreq.v4.imr_ifindex = (int)ifidx;
    if (setsockopt(m->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m->mreq.v4, sizeof m->mreq.v4) < 0) {
      log_line("join %s: %s", group, strerror(errno));
      goto fail;
    }
  } else {
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
  close(m->fd);
  free(m);
  return NULL;
}

mcast_t *mcast_open_ssm(int family, const char *group, unsigned port, const char *source_addr, const char *iface, int recv_timeout_ms) {
  mcast_t *m = calloc(1, sizeof *m);
  unsigned ifidx;
  int level = (family == AF_INET) ? IPPROTO_IP : IPPROTO_IPV6;
  struct timeval tv;

  tv.tv_sec = recv_timeout_ms / 1000;
  tv.tv_usec = (recv_timeout_ms % 1000) * 1000;

  if (!m)
    return NULL;
  m->family = family;
  m->ssm = 1;
  m->fd = open_bound_recv_socket(family, port, iface, &ifidx);
  if (m->fd < 0) {
    free(m);
    return NULL;
  }

  memset(&m->gsr, 0, sizeof m->gsr);
  m->gsr.gsr_interface = ifidx;
  {
    socklen_t sslen;
    if (netaddr_fill(family, group, port, &m->gsr.gsr_group, &sslen)) {
      log_line("bad group address: %s", group);
      goto fail;
    }
    if (netaddr_fill(family, source_addr, 0, &m->gsr.gsr_source, &sslen)) {
      log_line("bad source address: %s", source_addr);
      goto fail;
    }
  }
  if (setsockopt(m->fd, level, MCAST_JOIN_SOURCE_GROUP, &m->gsr, sizeof m->gsr) < 0) {
    log_line("join %s from %s: %s", group, source_addr, strerror(errno));
    goto fail;
  }
  setsockopt(m->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  return m;

fail:
  close(m->fd);
  free(m);
  return NULL;
}

ssize_t mcast_recv(mcast_t *m, void *buf, size_t cap, net_err_reason_t *reason_out) {
  ssize_t n = recv(m->fd, buf, cap, 0);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return 0;
    log_line("recv: %s", strerror(errno));
    if (reason_out)
      *reason_out = NET_ERR_READ;
    return -1;
  }
  return n;
}

int mcast_fd(const mcast_t *m) { return m->fd; }

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
  /* no SO_SNDBUF bump: blocking socket + small backlog caps burst size below
     policer drop threshold. deliberate. */
  if (iface) {
    ifidx = if_nametoindex(iface);
    if (!ifidx) {
      log_line("unknown interface: %s", iface);
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
      log_line("set outgoing interface: %s", strerror(errno));
      goto fail;
    }
    if (ttl > 0 && setsockopt(m->fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl) < 0) {
      log_line("set ttl: %s", strerror(errno));
      goto fail;
    }
    if (netaddr_fill(AF_INET, group, port, &ss, &sslen)) {
      log_line("bad group address: %s", group);
      goto fail;
    }
    memcpy(&m->dest.v4, &ss, sslen);
  } else {
    struct sockaddr_storage ss;
    socklen_t sslen;
    if (ifidx && setsockopt(m->fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifidx, sizeof ifidx) < 0) {
      log_line("set outgoing interface: %s", strerror(errno));
      goto fail;
    }
    if (ttl > 0 && setsockopt(m->fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl, sizeof ttl) < 0) {
      log_line("set hop limit: %s", strerror(errno));
      goto fail;
    }
    if (netaddr_fill(AF_INET6, group, port, &ss, &sslen)) {
      log_line("bad group address: %s", group);
      goto fail;
    }
    memcpy(&m->dest.v6, &ss, sslen);
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

int mcast_set_tos(mcast_t *m, int tos) {
  return net_set_dscp(m->fd, m->family, tos);
}

void mcast_close(mcast_t *m) {
  if (!m)
    return;
  if (m->fd >= 0) {
    if (m->ssm) {
      int level = (m->family == AF_INET) ? IPPROTO_IP : IPPROTO_IPV6;
      setsockopt(m->fd, level, MCAST_LEAVE_SOURCE_GROUP, &m->gsr, sizeof m->gsr);
    } else if (!m->sender) {
      if (m->family == AF_INET)
        setsockopt(m->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &m->mreq.v4, sizeof m->mreq.v4);
      else
        setsockopt(m->fd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &m->mreq.v6, sizeof m->mreq.v6);
    }
    close(m->fd);
  }
  free(m);
}
