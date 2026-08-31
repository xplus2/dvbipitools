/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* reactor_tls.h with no OpenSSL: tls_is_running() stays 0. reactor.c never
   binds --listen-tls, handshake.c never calls tls_accept/tls_handshake.
   tls_net_send/recv fall through to plain send/recv, fd is always plain */

#include "reactor_tls.h"

#include <errno.h>
#include <linux/errqueue.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

void tls_set_http2_enabled(int enabled) { (void)enabled; }

int tls_init(const char *cert_path, const char *key_path, int fd_table_cap) {
  (void)cert_path;
  (void)key_path;
  (void)fd_table_cap;
  return 0;
}

void tls_gc_init(int nshards) { (void)nshards; }

void tls_cleanup(void) {}

int tls_create_listen_sock(int port, const char *host) {
  (void)port;
  (void)host;
  return -1;
}

int tls_create_listen_sock6(int port, const char *host6) {
  (void)port;
  (void)host6;
  return -1;
}

int tls_is_running(void) { return 0; }

int tls_accept(int fd) {
  (void)fd;
  return -1;
}

int tls_handshake(int fd) {
  (void)fd;
  return -1;
}

void tls_close_fd(int fd) { (void)fd; }

void tls_gc_sweep(void) {}

ssize_t tls_net_send(int fd, const void *buf, size_t len) { return send(fd, buf, len, MSG_NOSIGNAL); }

ssize_t tls_net_send_zc(int fd, const void *buf, size_t len, int *used_zc) {
  *used_zc = 0;
  ssize_t r = send(fd, buf, len, MSG_NOSIGNAL | MSG_ZEROCOPY);
  if (r < 0 && (errno == EINVAL || errno == ENOBUFS))
    return send(fd, buf, len, MSG_NOSIGNAL); /* zerocopy unavailable/exhausted, used_zc stays 0 */
  if (r > 0)
    *used_zc = 1;
  return r;
}

int tls_zc_drain(int fd, int *hi_out) {
  char cbuf[128];
  struct msghdr msg;
  int found = 0;
  *hi_out = -1;
  for (;;) {
    memset(&msg, 0, sizeof msg);
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof cbuf;
    if (recvmsg(fd, &msg, MSG_ERRQUEUE | MSG_DONTWAIT) < 0)
      break;
    for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
      if (!((cm->cmsg_level == SOL_IP && cm->cmsg_type == IP_RECVERR) ||
            (cm->cmsg_level == SOL_IPV6 && cm->cmsg_type == IPV6_RECVERR)))
        continue;
      struct sock_extended_err *serr = (struct sock_extended_err *)CMSG_DATA(cm);
      if (serr->ee_errno != 0 || serr->ee_origin != SO_EE_ORIGIN_ZEROCOPY)
        continue;
      found = 1;
      if ((int)serr->ee_data > *hi_out)
        *hi_out = (int)serr->ee_data;
    }
  }
  return found;
}

ssize_t tls_net_recv(int fd, void *buf, size_t len) { return recv(fd, buf, len, 0); }

int tls_is_tls(int fd) {
  (void)fd;
  return 0;
}

int tls_client_cert_verified(int fd) {
  (void)fd;
  return 0;
}

void tls_get_client_cert_cn(int fd, char *buf, size_t bufsz) {
  (void)fd;
  if (bufsz)
    buf[0] = '\0';
}

int reload_tls(void) { return -1; }

void tls_cert_info(char *buf, size_t sz, const char *path, int from_file) {
  (void)path;
  (void)from_file;
  if (sz)
    buf[0] = '\0';
}

int tls_has_pending(int fd) {
  (void)fd;
  return 0;
}

int tls_alpn_is_h2(int fd) {
  (void)fd;
  return 0;
}

int tls_cert_detail(const char *path, int from_file, tls_cert_detail_t *out) {
  (void)path;
  (void)from_file;
  memset(out, 0, sizeof *out);
  return 0;
}
