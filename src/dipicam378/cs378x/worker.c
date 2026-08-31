/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include "../version.h"
#include "priv.h"

static void reap_worker_slot(cs378x_server_t *s, int slot) {
  if (!s->worker_thread_joinable[slot])
    return;
  pthread_join(s->worker_thread[slot], NULL);
  s->worker_thread_joinable[slot] = 0;
}

static void log_unknown_command(const unsigned char *body, size_t buflen, int slot) {
  static const char hex_nib[] = "0123456789ABCDEF";
  size_t dumplen = buflen + 20 > 32 ? 32 : buflen + 20;
  char hex[32 * 3 + 1];
  for (size_t i = 0; i < dumplen; i++) {
    hex[i * 3] = hex_nib[(body[i] >> 4) & 0xF];
    hex[i * 3 + 1] = hex_nib[body[i] & 0xF];
    hex[i * 3 + 2] = ' ';
  }
  hex[dumplen * 3] = '\0';
  log_line(TOOL_NAME ": unknown command %d, len=%zu, bytes=%s(slot %d)", body[0], buflen, hex, slot);
}

/* read n bytes, honoring stop flag between recv() timeouts. 1 ok, 0 stopped, -1 closed/error */
static int read_exact(int fd, unsigned char *buf, size_t n, atomic_int *stop) {
  size_t got = 0;
  while (got < n) {
    ssize_t r;
    if (atomic_load_explicit(stop, memory_order_relaxed) || signal_stop_requested())
      return 0;
    r = recv(fd, buf + got, n - got, 0);
    if (r > 0) {
      got += (size_t)r;
      continue;
    }
    if (r == 0)
      return -1;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      continue;
    return -1;
  }
  return 1;
}

typedef enum { FRAME_OK, FRAME_STOP, FRAME_AUTH_FAIL } frame_status_t;

/* read+decrypt+validate one frame. auth/size/crc failures logged+counted here.
   FRAME_OK: body_out/buflen_out set. FRAME_STOP: read/decrypt failed, no log.
   FRAME_AUTH_FAIL: already logged */
static frame_status_t read_frame(cs378x_server_t *s, int fd, int slot, unsigned char *buf, unsigned char **body_out,
                                  size_t *buflen_out, unsigned char conn_ucrc[4], int *have_ucrc) {
  unsigned char *body = buf + 4;
  size_t buflen, total;
  int rc;

  rc = read_exact(fd, buf, CS378X_MIN_FRAME, &s->stop);
  if (rc <= 0)
    return FRAME_STOP;

  if (!*have_ucrc) {
    if (s->check_ucrc && memcmp(buf, s->expected_ucrc, 4) != 0) {
      log_line(TOOL_NAME ": username mismatch, closing (slot %d)", slot);
      atomic_fetch_add_explicit(&s->auth_errors_total[CAM_AUTH_USER], 1, memory_order_relaxed);
      return FRAME_AUTH_FAIL;
    }
    memcpy(conn_ucrc, buf, 4);
    *have_ucrc = 1;
  } else if (memcmp(buf, conn_ucrc, 4) != 0) {
    log_line(TOOL_NAME ": connection id changed mid-stream, closing (slot %d)", slot);
    atomic_fetch_add_explicit(&s->auth_errors_total[CAM_AUTH_CONNID], 1, memory_order_relaxed);
    return FRAME_AUTH_FAIL;
  }

  if (cs378x_aes128_ecb(s->aes_key, body, 32, 0) != 0)
    return FRAME_STOP;

  if (body[0] == CMD_ECM_REQUEST)
    buflen = (size_t)(3 + ((body[21] & 0x0F) << 8) + body[22]);
  else
    buflen = body[1];

  total = cs378x_frame_boundary(20 + buflen);
  if (total > CS378X_BUF_CAP - 4) {
    log_line(TOOL_NAME ": oversized request (%zu), closing (slot %d)", total, slot);
    atomic_fetch_add_explicit(&s->auth_errors_total[CAM_AUTH_OVERSIZED], 1, memory_order_relaxed);
    return FRAME_AUTH_FAIL;
  }

  if (total > 32) {
    rc = read_exact(fd, body + 32, total - 32, &s->stop);
    if (rc <= 0)
      return FRAME_STOP;
    if (cs378x_aes128_ecb(s->aes_key, body + 32, total - 32, 0) != 0)
      return FRAME_STOP;
  }

  if (cs378x_crc32(body + 20, buflen) != (((uint32_t)body[4] << 24) | ((uint32_t)body[5] << 16) | ((uint32_t)body[6] << 8) | body[7])) {
    log_line(TOOL_NAME ": checksum error, wrong password? (slot %d)", slot);
    atomic_fetch_add_explicit(&s->auth_errors_total[CAM_AUTH_CHECKSUM], 1, memory_order_relaxed);
    return FRAME_AUTH_FAIL;
  }

  *body_out = body;
  *buflen_out = buflen;
  return FRAME_OK;
}

static void *worker_main(void *arg) {
  worker_arg_t *wa = arg;
  cs378x_server_t *s = wa->s;
  int fd = wa->fd;
  int slot = wa->slot;
  struct timeval tv;
  unsigned char conn_ucrc[4];
  int have_ucrc = 0;

  tv.tv_sec = 0;
  tv.tv_usec = CS378X_RECV_TIMEOUT_MS * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  log_line(TOOL_NAME ": connection accepted (slot %d)", slot);
  atomic_fetch_add_explicit(&s->connections_total, 1, memory_order_relaxed);

  for (;;) {
    unsigned char buf[CS378X_BUF_CAP];
    unsigned char *body;
    size_t buflen;

    if (read_frame(s, fd, slot, buf, &body, &buflen, conn_ucrc, &have_ucrc) != FRAME_OK)
      break;

    switch (body[0]) {
      case CMD_ECM_REQUEST:
        handle_ecm(s, fd, conn_ucrc, body, buflen);
        break;
      case CMD_EMM:
      case CMD_EMM_1830:
        handle_emm(s, body, buflen);
        break;
      case CMD_KEEPALIVE:
        send_keepalive_answer(s, fd, conn_ucrc);
        break;
      default:
        if (s->verbose)
          log_unknown_command(body, buflen, slot);
        break;
    }
  }

  log_line(TOOL_NAME ": connection closed (slot %d)", slot);
  atomic_store_explicit(&s->worker_fd[slot], -1, memory_order_release);
  close(fd);
  atomic_store_explicit(&s->worker_active[slot], 0, memory_order_release);
  return NULL;
}

int tcp_listen_dualstack(unsigned port) {
  struct sockaddr_in6 addr;
  int fd, on = 1, off = 0, flags;

  fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0) {
    log_line(TOOL_NAME ": socket: %s", strerror(errno));
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);

  memset(&addr, 0, sizeof addr);
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = in6addr_any;
  addr.sin6_port = htons((unsigned short)port);
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    log_line(TOOL_NAME ": bind :%u: %s", port, strerror(errno));
    close(fd);
    return -1;
  }
  if (listen(fd, 8) < 0) {
    log_line(TOOL_NAME ": listen: %s", strerror(errno));
    close(fd);
    return -1;
  }
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    log_line(TOOL_NAME ": fcntl O_NONBLOCK: %s", strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

void *accept_main(void *arg) {
  cs378x_server_t *s = arg;

  while (!atomic_load_explicit(&s->stop, memory_order_relaxed) && !signal_stop_requested()) {
    struct pollfd pfd;
    int pret, fd, slot;

    pfd.fd = s->listen_fd;
    pfd.events = POLLIN;
    pret = poll(&pfd, 1, CS378X_POLL_INTERVAL_MS);
    if (pret <= 0)
      continue;

    fd = accept(s->listen_fd, NULL, NULL);
    if (fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        continue;
      log_line(TOOL_NAME ": accept: %s", strerror(errno));
      continue;
    }

    slot = -1;
    for (int i = 0; i < CS378X_MAX_CONNS; i++) {
      int expected = 0;
      if (atomic_compare_exchange_strong_explicit(&s->worker_active[i], &expected, 1, memory_order_acq_rel, memory_order_relaxed)) {
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      log_line(TOOL_NAME ": connection limit (%d) reached, rejecting", CS378X_MAX_CONNS);
      close(fd);
      continue;
    }

    {
      worker_arg_t *wa = &s->worker_args[slot];
      pthread_t th;
      reap_worker_slot(s, slot);
      wa->s = s;
      wa->fd = fd;
      wa->slot = slot;
      atomic_store_explicit(&s->worker_fd[slot], fd, memory_order_release);
      if (pthread_create(&th, NULL, worker_main, wa) != 0) {
        log_line(TOOL_NAME ": pthread_create: %s", strerror(errno));
        close(fd);
        atomic_store_explicit(&s->worker_fd[slot], -1, memory_order_release);
        atomic_store_explicit(&s->worker_active[slot], 0, memory_order_release);
      } else {
        s->worker_thread[slot] = th;
        s->worker_thread_joinable[slot] = 1;
      }
    }
  }
  return NULL;
}
