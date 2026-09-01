/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "reactor_tls_int.h"

#include <errno.h>
#include <linux/errqueue.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

ssize_t tls_net_send(int fd, const void *buf, size_t len) {
  if (fd < 0 || fd >= g_tls_fd_max) return send(fd, buf, len, MSG_NOSIGNAL);

  SSL *ssl = __atomic_load_n(&fd_ssl[fd], __ATOMIC_ACQUIRE);
  if (!ssl) return send(fd, buf, len, MSG_NOSIGNAL);
  int n = SSL_write(ssl, buf, (int)len);
  if (n <= 0) {
    int ssl_err = SSL_get_error(ssl, n);
    if (ssl_err == SSL_ERROR_WANT_WRITE || ssl_err == SSL_ERROR_WANT_READ) {
      errno = EAGAIN;
      t_tls_want_write = (ssl_err == SSL_ERROR_WANT_WRITE);
    } else {
      errno = ECONNRESET; /* fatal: don't leave a stale EAGAIN from an earlier WANT_READ/WRITE */
    }
    return -1;
  }
  return (ssize_t)n;
}

ssize_t tls_net_send_zc(int fd, const void *buf, size_t len, int *used_zc) {
  *used_zc = 0;
  if (fd >= 0 && fd < g_tls_fd_max && __atomic_load_n(&fd_ssl[fd], __ATOMIC_ACQUIRE))
    return tls_net_send(fd, buf, len); /* defensive: callers gate TLS out already */

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
      const struct sock_extended_err *serr = (const struct sock_extended_err *)CMSG_DATA(cm);
      if (serr->ee_errno != 0 || serr->ee_origin != SO_EE_ORIGIN_ZEROCOPY)
        continue;
      found = 1;
      if ((int)serr->ee_data > *hi_out)
        *hi_out = (int)serr->ee_data;
    }
  }
  return found;
}

ssize_t tls_net_recv(int fd, void *buf, size_t len) {
  if (fd < 0 || fd >= g_tls_fd_max) return recv(fd, buf, len, 0);
  SSL *ssl = __atomic_load_n(&fd_ssl[fd], __ATOMIC_ACQUIRE);
  if (!ssl) return recv(fd, buf, len, 0);
  int n = SSL_read(ssl, buf, (int)len);
  if (n <= 0) {
    int ssl_err = SSL_get_error(ssl, n);
    if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
      errno = EAGAIN;
      t_tls_want_write = (ssl_err == SSL_ERROR_WANT_WRITE);
      return -1;
    }
    if (ssl_err == SSL_ERROR_ZERO_RETURN) return 0;
    errno = ECONNRESET; /* fatal: don't leave stale EAGAIN from earlier WANT_READ/WRITE */
    return -1;
  }
  return (ssize_t)n;
}
