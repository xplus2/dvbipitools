/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../helper/argutil.h"
#include "../sys/ioutil.h"
#include "../helper/log.h"
#include "../sys/signal.h"
#include "netconnect.h"

#define NETCONNECT_POLL_INTERVAL_MS 150

const char *net_err_reason_name(net_err_reason_t reason) {
  static const char *const names[NET_ERR_COUNT] = {"dns", "connect", "timeout", "tls", "http", "format", "read", "eof", "other"};
  if ((unsigned)reason >= NET_ERR_COUNT) return "other";
  return names[reason];
}

static void set_reason(net_err_reason_t *out, net_err_reason_t v) {
  if (out) *out = v;
}

/* 1 connected, -1 refused/error, 0 timed out or stop requested */
static int wait_connect(int fd, int timeout_ms) {
  int elapsed = 0;

  while (elapsed < timeout_ms) {
    struct pollfd pfd;
    int step = NETCONNECT_POLL_INTERVAL_MS;
    int pret;

    if (signal_stop_requested()) return 0;
    if (step > timeout_ms - elapsed) step = timeout_ms - elapsed;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    pret = poll(&pfd, 1, step);
    if (pret > 0) {
      int soerr = 0;
      socklen_t sl = sizeof soerr;
      getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
      if (soerr == 0) return 1;
      errno = soerr;
      return -1;
    }
    if (pret < 0 && errno != EINTR) return -1;
    elapsed += step;
  }
  return 0;
}

int netconnect_tcp(const char *host, unsigned port, int timeout_ms, net_err_reason_t *reason_out) {
  struct addrinfo hints;
  struct addrinfo *res;
  struct addrinfo *ai;
  char portstr[6];
  int fd = -1;
  int e;
  int save_errno = 0;

  if (port > 65535) {
    log_code("LE600128", "connect %s:%u: %s", host, port, strerror(EINVAL));
    set_reason(reason_out, NET_ERR_CONNECT);
    errno = EINVAL;
    return -1;
  }
  uint_to_str(portstr, port);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  e = getaddrinfo(host, portstr, &hints, &res);
  if (e) {
    log_code("LE600131", "resolve %s: %s", host, gai_strerror(e));
    set_reason(reason_out, NET_ERR_DNS);
    return -1;
  }
  for (ai = res; ai; ai = ai->ai_next) {
    int flags;
    int cr;
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
      save_errno = errno;
      continue;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      save_errno = errno;
      close(fd);
      fd = -1;
      continue;
    }
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
      break;
    if (errno != EINPROGRESS) {
      save_errno = errno;
      close(fd);
      fd = -1;
      continue;
    }
    cr = wait_connect(fd, timeout_ms);
    if (cr == 1)
      break;
    save_errno = (cr == -1) ? errno : ETIMEDOUT;
    close(fd);
    fd = -1;
    if (signal_stop_requested())
      break;
  }
  freeaddrinfo(res);
  if (fd < 0) {
    if (!signal_stop_requested()) log_code("LE600128", "connect %s:%u: %s", host, port, strerror(save_errno));
    set_reason(reason_out, (save_errno == ETIMEDOUT) ? NET_ERR_TIMEOUT : NET_ERR_CONNECT);
    return -1;
  }
  return fd; /* already O_NONBLOCK, set before connect() */
}

#define NC_MAX_CAND 16

typedef struct {
  int family;
  int socktype;
  int protocol;
  socklen_t len;
  struct sockaddr_storage ss;
} nc_cand_t;

typedef struct {
  int err; /* getaddrinfo code, 0 ok */
  unsigned count;
  nc_cand_t cand[NC_MAX_CAND];
} nc_resolved_t;

struct netconnect_pending {
  nc_resolved_t r;
  unsigned cur; /* candidate fd is connecting on */
  int resolving; /* lookup thread running, 0 once resolved */
  char host[256];
  unsigned port;
};

static void cands_from_ai(nc_resolved_t *r, const struct addrinfo *ai) {
  r->count = 0;
  for (; ai && r->count < NC_MAX_CAND; ai = ai->ai_next) {
    nc_cand_t *c = &r->cand[r->count];
    if (ai->ai_addrlen > sizeof c->ss) continue;
    c->family = ai->ai_family;
    c->socktype = ai->ai_socktype;
    c->protocol = ai->ai_protocol;
    c->len = ai->ai_addrlen;
    memcpy(&c->ss, ai->ai_addr, ai->ai_addrlen);
    r->count++;
  }
}

/* tries candidates from idx onward, first usable wins. -1 if none work (save_errno set) */
static int try_addrs(const nc_resolved_t *r, unsigned idx, unsigned *used, int *save_errno) {
  for (; idx < r->count; idx++) {
    const nc_cand_t *c = &r->cand[idx];
    int fd;
    int flags;
    fd = socket(c->family, c->socktype, c->protocol);
    if (fd < 0) {
      *save_errno = errno;
      continue;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      *save_errno = errno;
      close(fd);
      continue;
    }
    if (connect(fd, (const struct sockaddr *)&c->ss, c->len) == 0) {
      *used = idx;
      return fd; /* rare, e.g. loopback: already connected */
    }
    if (errno == EINPROGRESS) {
      *used = idx;
      return fd; /* caller polls POLLOUT, then netconnect_tcp_finish() */
    }
    *save_errno = errno;
    close(fd);
  }
  return -1;
}

typedef struct {
  char host[256];
  char portstr[6];
  int wfd;
} nc_resolver_arg_t;

/* getaddrinfo not fork-safe in threaded process */
static void *resolver_thread(void *arg) {
  nc_resolver_arg_t *a = arg;
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  nc_resolved_t r;

  memset(&r, 0, sizeof r);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  r.err = getaddrinfo(a->host, a->portstr, &hints, &res);
  if (!r.err) {
    cands_from_ai(&r, res);
    freeaddrinfo(res);
  }
  if (!r.err && !r.count) r.err = EAI_NONAME;
  (void)send(a->wfd, &r, sizeof r, MSG_NOSIGNAL); /* reader may be gone */
  close(a->wfd);
  free(a);
  return NULL;
}

static void pending_free(netconnect_pending_t *p) {
  free(p);
}

/* starts the lookup thread, *rfd_out = nonblocking read end. 0 ok, -1 failed (errno set) */
static int resolver_spawn(netconnect_pending_t *p, const char *portstr, int *rfd_out) {
  int sv[2];
  int flags;
  int e;
  pthread_t tid;
  pthread_attr_t attr;
  nc_resolver_arg_t *a;

  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) return -1;
  flags = fcntl(sv[0], F_GETFL, 0);
  if (flags < 0 || fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) < 0) {
    e = errno;
    close(sv[0]);
    close(sv[1]);
    errno = e;
    return -1;
  }
  a = malloc(sizeof *a);
  if (!a) {
    close(sv[0]);
    close(sv[1]);
    errno = ENOMEM;
    return -1;
  }
  bufcpy(a->host, sizeof a->host, p->host);
  bufcpy(a->portstr, sizeof a->portstr, portstr);
  a->wfd = sv[1];
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  e = pthread_create(&tid, &attr, resolver_thread, a);
  pthread_attr_destroy(&attr);
  if (e) {
    free(a);
    close(sv[0]);
    close(sv[1]);
    errno = e;
    return -1;
  }
  p->resolving = 1;
  *rfd_out = sv[0];
  return 0;
}

int netconnect_tcp_start(const char *host, unsigned port, netconnect_pending_t **pending_out, net_err_reason_t *reason_out) {
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  char portstr[6];
  int fd;
  int save_errno = 0;
  unsigned used = 0;
  netconnect_pending_t *p;

  if (port > 65535) {
    log_code("LE600128", "connect %s:%u: %s", host, port, strerror(EINVAL));
    set_reason(reason_out, NET_ERR_CONNECT);
    errno = EINVAL;
    return -1;
  }
  p = calloc(1, sizeof *p);
  if (!p) {
    set_reason(reason_out, NET_ERR_OTHER);
    return -1;
  }
  bufcpy(p->host, sizeof p->host, host);
  p->port = port;
  uint_to_str(portstr, port);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICHOST;
  if (getaddrinfo(host, portstr, &hints, &res) != 0) {
    if (resolver_spawn(p, portstr, &fd) != 0) {
      save_errno = errno;
      free(p);
      log_code("LE600131", "resolve %s: %s", host, strerror(save_errno));
      set_reason(reason_out, NET_ERR_DNS);
      return -1;
    }
    *pending_out = p;
    return fd;
  }
  cands_from_ai(&p->r, res);
  freeaddrinfo(res);
  fd = try_addrs(&p->r, 0, &used, &save_errno);
  if (fd < 0) {
    free(p);
    log_code("LE600128", "connect %s:%u: %s", host, port, strerror(save_errno));
    set_reason(reason_out, NET_ERR_CONNECT);
    return -1;
  }
  p->cur = used;
  *pending_out = p;
  return fd;
}

short netconnect_tcp_events(const netconnect_pending_t *pending) {
  return (pending && pending->resolving) ? POLLIN : POLLOUT;
}

/* resolver result readable. 1 candidate connecting on *fd, 0 not ready yet, -1 failed */
static int finish_resolve(netconnect_pending_t *p, int *fd, net_err_reason_t *reason_out) {
  nc_resolved_t r;
  ssize_t n = read(*fd, &r, sizeof r);
  unsigned used;
  int save_errno = 0;
  int nfd;

  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
  close(*fd);
  *fd = -1;
  p->resolving = 0;
  if (n != (ssize_t)sizeof r || r.err || !r.count) {
    log_code("LE600131", "resolve %s: %s", p->host, (n == (ssize_t)sizeof r && r.err) ? gai_strerror(r.err) : "resolver failed");
    set_reason(reason_out, NET_ERR_DNS);
    errno = 0;
    return -1;
  }
  p->r = r;
  nfd = try_addrs(&p->r, 0, &used, &save_errno);
  if (nfd < 0) {
    set_reason(reason_out, NET_ERR_CONNECT);
    errno = save_errno;
    return -1;
  }
  p->cur = used;
  *fd = nfd;
  return 1;
}

int netconnect_tcp_finish(netconnect_pending_t **pending, int *fd, net_err_reason_t *reason_out) {
  netconnect_pending_t *p = *pending;
  int soerr = 0;
  int save_errno;
  socklen_t sl = sizeof soerr;
  unsigned used;
  int nfd;

  if (p->resolving) {
    int rr = finish_resolve(p, fd, reason_out);
    if (rr < 0) {
      save_errno = errno;
      pending_free(p);
      *pending = NULL;
      errno = save_errno;
      return -1;
    }
    return 0;
  }
  if (getsockopt(*fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) < 0) {
    save_errno = errno;
    close(*fd);
    *fd = -1;
    pending_free(p);
    *pending = NULL;
    errno = save_errno;
    set_reason(reason_out, NET_ERR_OTHER);
    return -1;
  }
  if (soerr == 0) {
    pending_free(p);
    *pending = NULL;
    return 1;
  }
  close(*fd);
  save_errno = soerr;
  nfd = try_addrs(&p->r, p->cur + 1, &used, &save_errno);
  if (nfd >= 0) {
    p->cur = used;
    *fd = nfd;
    return 0;
  }
  *fd = -1;
  pending_free(p);
  *pending = NULL;
  errno = save_errno;
  set_reason(reason_out, NET_ERR_CONNECT);
  return -1;
}

void netconnect_tcp_abort(netconnect_pending_t *pending) {
  if (!pending) return;
  pending_free(pending);
}

int netaddr_fill(int family, const char *addr, unsigned port, struct sockaddr_storage *ss, socklen_t *sslen) {
  memset(ss, 0, sizeof *ss);
  if (family == AF_INET) {
    struct sockaddr_in *a = (struct sockaddr_in *)ss;
    a->sin_family = AF_INET;
    a->sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, addr, &a->sin_addr) != 1) return -1;
    *sslen = sizeof *a;
  } else {
    struct sockaddr_in6 *a = (struct sockaddr_in6 *)ss;
    a->sin6_family = AF_INET6;
    a->sin6_port = htons((unsigned short)port);
    if (inet_pton(AF_INET6, addr, &a->sin6_addr) != 1) return -1;
    *sslen = sizeof *a;
  }
  return 0;
}

int net_set_dscp(int fd, int family, int tos) {
  if (family == AF_INET) return setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof tos);
  return setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof tos);
}

ssize_t net_sendto_tos(int fd, const void *buf, size_t len, const struct sockaddr *to, socklen_t tolen, int tos) {
  union {
    struct cmsghdr align;
    char buf[CMSG_SPACE(sizeof(int))];
  } ctl;
  struct iovec iov;
  struct msghdr msg;
  struct cmsghdr *cm;
  struct sockaddr_storage dst;

  if (tolen > sizeof dst) return -1;
  memcpy(&dst, to, tolen);
  iov.iov_base = (void *)buf;
  iov.iov_len = len;
  memset(&msg, 0, sizeof msg);
  memset(&ctl, 0, sizeof ctl);
  msg.msg_name = &dst;
  msg.msg_namelen = tolen;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = ctl.buf;
  msg.msg_controllen = sizeof ctl.buf;
  cm = CMSG_FIRSTHDR(&msg);
  cm->cmsg_len = CMSG_LEN(sizeof(int));
  if (to->sa_family == AF_INET6) {
    cm->cmsg_level = IPPROTO_IPV6;
    cm->cmsg_type = IPV6_TCLASS;
  } else {
    cm->cmsg_level = IPPROTO_IP;
    cm->cmsg_type = IP_TOS;
  }
  memcpy(CMSG_DATA(cm), &tos, sizeof tos);
  return sendmsg(fd, &msg, 0);
}

int net_dscp_parse(const char *s, int *tos_out) {
  static const enum_map_t map[] = {
    {"video-high", NET_DSCP_VIDEO_HIGH},
    {"video-low", NET_DSCP_VIDEO_LOW},
    {"voice", NET_DSCP_VOICE_BEARER},
    {"signalling", NET_DSCP_SIGNALLING},
    {"best-effort", NET_DSCP_BEST_EFFORT},
  };
  unsigned v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, tos_out) == 0) return 0;
  if (argutil_uint_range(s, 0, 63, &v) == 0) {
    *tos_out = (int)(v << 2);
    return 0;
  }
  return -1;
}
