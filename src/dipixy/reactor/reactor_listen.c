/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include "internal.h"
#include "reactor.h"
#include "reactor_tls.h"

#include "../dash/lldash.h"
#include "../ts/ts_push.h"
#include "../version.h"
#ifdef HAVE_HTTP3
#include "../http3/http3.h"
#endif

#include "lib/helper/log.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#define CONN_TABLE_FALLBACK 65536

/* SO_REUSEPORT: every worker thread binds its own socket to shared port, kernel load-balances accept() */
static int create_listen_sock(int family, const char *addr, listen_scope_t scope, unsigned port) {
  int fd, yes = 1;
  fd = socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) return -1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
  setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof yes);
  if (family == AF_INET) {
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1) {
      close(fd);
      return -1;
    }
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) || listen(fd, 4096)) {
      close(fd);
      return -1;
    }
  } else {
    struct sockaddr_in6 sa;
    memset(&sa, 0, sizeof sa);
    sa.sin6_family = AF_INET6;
    sa.sin6_port = htons((uint16_t)port);
    if (scope == LISTEN_ANY) {
      sa.sin6_addr = in6addr_any;
      { int no = 0; setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof no); }
    } else if (inet_pton(AF_INET6, addr, &sa.sin6_addr) != 1) {
      close(fd);
      return -1;
    }
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) || listen(fd, 4096)) {
      close(fd);
      return -1;
    }
  }
  return fd;
}

static int create_listen_sock_for_spec(const listen_spec_t *spec) {
  int family = spec->scope == LISTEN_V4 ? AF_INET : AF_INET6;
  return create_listen_sock(family, spec->addr, spec->scope, spec->port);
}

void reactor_setup_listeners(reactor_listeners_t *rl, int epfd, int tid) {
  struct epoll_event ev;
  rl->nL = 0;

  {
    int fd = create_listen_sock_for_spec(&reactor_cfg()->listen);
    if (fd >= 0)
      rl->L[rl->nL++] = (reactor_listener){fd, 0, RL_ACCEPT};
  }
  if (tls_is_running()) {
    int fd = create_listen_sock_for_spec(&reactor_cfg()->listen_tls);
    if (fd >= 0)
      rl->L[rl->nL++] = (reactor_listener){fd, 1, RL_ACCEPT};
  }

#ifdef HAVE_HTTP3
  if (!reactor_cfg()->no_http3) {
    const listen_spec_t *lt = &reactor_cfg()->listen_tls;
    if (lt->scope != LISTEN_V6) {
      t_h3_udp4 = h3_create_udp_sock((int)lt->port, lt->scope == LISTEN_ANY ? "0.0.0.0" : lt->addr);
      if (t_h3_udp4 >= 0)
        rl->L[rl->nL++] = (reactor_listener){t_h3_udp4, 0, RL_H3_UDP};
    }
    if (lt->scope != LISTEN_V4) {
      t_h3_udp6 = h3_create_udp_sock6((int)lt->port, lt->scope == LISTEN_ANY ? "::" : lt->addr);
      if (t_h3_udp6 >= 0)
        rl->L[rl->nL++] = (reactor_listener){t_h3_udp6, 0, RL_H3_UDP};
    }
  }
#endif

  rl->tspush_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (rl->tspush_efd >= 0) {
    ts_push_register_reactor_efd(tid, rl->tspush_efd);
    rl->L[rl->nL++] = (reactor_listener){rl->tspush_efd, 0, RL_TSPUSH_EFD};
  }

  rl->dashchunk_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (rl->dashchunk_efd >= 0) {
    dash_lldash_register_reactor_efd(tid, rl->dashchunk_efd);
    rl->L[rl->nL++] = (reactor_listener){rl->dashchunk_efd, 0, RL_DASHCHUNK_EFD};
  }

  for (int i = 0; i < rl->nL; i++) {
    memset(&ev, 0, sizeof ev);
    ev.events = EPOLLIN;
    ev.data.ptr = &rl->L[i];
    epoll_ctl(epfd, EPOLL_CTL_ADD, rl->L[i].fd, &ev);
  }
}

void reactor_teardown_listeners(const reactor_listeners_t *rl, int tid) {
  if (rl->tspush_efd >= 0) {
    ts_push_register_reactor_efd(tid, -1);
    close(rl->tspush_efd);
  }
  if (rl->dashchunk_efd >= 0) {
    dash_lldash_register_reactor_efd(tid, -1);
    close(rl->dashchunk_efd);
  }
  for (int i = 0; i < rl->nL; i++) if (rl->L[i].kind == RL_ACCEPT || rl->L[i].kind == RL_H3_UDP) close(rl->L[i].fd);
#ifdef HAVE_HTTP3
  h3_thread_cleanup();
#endif
}

/* RLIMIT_NOFILE soft->hard, accept() only. table sizing: conn_table_capacity() */
void reactor_raise_nofile_limit(void) {
  struct rlimit rl;
  if (getrlimit(RLIMIT_NOFILE, &rl) != 0) return;
  if (rl.rlim_cur != RLIM_INFINITY && (rl.rlim_max == RLIM_INFINITY || rl.rlim_cur < rl.rlim_max)) {
    rl.rlim_cur = rl.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) getrlimit(RLIMIT_NOFILE, &rl);
  }
  log_line(TOOL_NAME ": rlimit_nofile=%ld", rl.rlim_cur == RLIM_INFINITY ? -1L : (long)rl.rlim_cur);
}
