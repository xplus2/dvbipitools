/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>

#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include "../version.h"
#include "priv.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* -1: timeout or poll error (errno set). 0: fd writable again */
static int wait_writable(int fd, double deadline) {
  double remain = (deadline - mono_seconds()) * 1000.0;
  struct pollfd pfd = {fd, POLLOUT, 0};
  int prc;
  if (remain <= 0) {
    errno = ETIMEDOUT;
    return -1;
  }
  prc = poll(&pfd, 1, (int)remain);
  if (prc == 0) {
    errno = ETIMEDOUT;
    return -1;
  }
  return prc < 0 ? -1 : 0;
}

static int send_all(int fd, const unsigned char *buf, size_t n, atomic_int *stop) {
  size_t sent = 0;
  double deadline = mono_seconds() + (double)CS378X_SEND_TIMEOUT_MS / 1000.0;

  while (sent < n) {
    ssize_t w;

    if (atomic_load_explicit(stop, memory_order_relaxed) || signal_stop_requested()) {
      errno = ECANCELED;
      return -1;
    }

    w = send(fd, buf + sent, n - sent, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (w <= 0) {
      if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && wait_writable(fd, deadline) == 0)
        continue;
      if (w < 0 && errno == EINTR)
        continue;
      return -1;
    }
    sent += (size_t)w;
  }
  return 0;
}

/* patches CRC over body[20:20+crc_len) into body[4:8), AES-encrypts body[0:total),
   frames as conn_ucrc+body, sends. what_for names it in error logs */
static void finalize_and_send(cs378x_server_t *s, int fd, const unsigned char conn_ucrc[4],
                               unsigned char *body, size_t total, size_t crc_len, const char *what_for) {
  uint32_t crc = cs378x_crc32(body + 20, crc_len);
  body[4] = (unsigned char)(crc >> 24);
  body[5] = (unsigned char)(crc >> 16);
  body[6] = (unsigned char)(crc >> 8);
  body[7] = (unsigned char)crc;

  if (cs378x_aes128_ecb(s->aes_key, body, total, 1) != 0) {
    log_line(TOOL_NAME ": encrypt failed building %s", what_for);
    return;
  }
  {
    unsigned char frame[4 + CS378X_BUF_CAP];
    memcpy(frame, conn_ucrc, 4);
    memcpy(frame + 4, body, total);
    if (send_all(fd, frame, 4 + total, &s->stop) < 0)
      log_line(TOOL_NAME ": send %s failed: %s", what_for, strerror(errno));
  }
}

/* body decrypted in place, mutated into CMD01 answer. conn_ucrc echoed verbatim */
static void send_ecm_response(cs378x_server_t *s, int fd, const unsigned char conn_ucrc[4], unsigned char *body, const unsigned char cw[16]) {
  size_t total;

  body[0] = CMD_ECM_RESPONSE;
  body[1] = 16;
  memcpy(body + 20, cw, 16);
  total = cs378x_frame_boundary(20 + 16);
  memset(body + 36, 0xFF, total - 36);
  finalize_and_send(s, fd, conn_ucrc, body, total, 16, "ECM response");
}

/* body decrypted in place. E_INVALID only, no sleep-retry variant: client stops asking
   this srvid/prid/caid (still at body[8:20)). framed like send_ecm_response */
static void send_cmd08(cs378x_server_t *s, int fd, const unsigned char conn_ucrc[4], unsigned char *body) {
  size_t total;

  body[0] = CMD_INVALID;
  body[1] = 2;
  body[20] = 0;
  body[21] = 0;
  total = cs378x_frame_boundary(20 + 2);
  memset(body + 22, 0xFF, total - 22);
  finalize_and_send(s, fd, conn_ucrc, body, total, 2, "CMD08");
}

void send_keepalive_answer(cs378x_server_t *s, int fd, const unsigned char conn_ucrc[4]) {
  unsigned char body[32];
  memset(body, 0, sizeof body);
  body[0] = CMD_KEEPALIVE;
  body[1] = 1;
  body[2] = 0;
  finalize_and_send(s, fd, conn_ucrc, body, sizeof body, 1, "keepalive answer");
}

void handle_ecm(cs378x_server_t *s, int fd, const unsigned char conn_ucrc[4], unsigned char *body, size_t buflen) {
  unsigned srvid, caid, prid;
  unsigned char cw[16];
  int rc;

  srvid = ((unsigned)body[8] << 8) | body[9];
  caid = ((unsigned)body[10] << 8) | body[11];
  prid = ((unsigned)body[12] << 24) | ((unsigned)body[13] << 16) | ((unsigned)body[14] << 8) | body[15];

  if (s->verbose)
    log_line(TOOL_NAME ": ECM request srvid=%04X caid=%04X prid=%08X len=%zu", srvid, caid, prid, buflen);
  atomic_fetch_add_explicit(&s->ecm_total, 1, memory_order_relaxed);

  rc = s->ecm_cb ? s->ecm_cb(body + 20, buflen, srvid, caid, prid, cw, s->user) : -1;
  if (rc == 0) {
    send_ecm_response(s, fd, conn_ucrc, body, cw);
    return;
  }
  atomic_fetch_add_explicit(&s->ecm_errors_total, 1, memory_order_relaxed);
  if (rc == -2) {
    log_line(TOOL_NAME ": caid %04X not supported, sending CMD08 for srvid=%04X", caid, srvid);
    send_cmd08(s, fd, conn_ucrc, body);
    return;
  }
  if (s->verbose)
    log_line(TOOL_NAME ": no CW available yet, dropping request");
}

void handle_emm(cs378x_server_t *s, unsigned char *body, size_t buflen) {
  unsigned caid, provid;

  caid = ((unsigned)body[10] << 8) | body[11];
  provid = ((unsigned)body[12] << 24) | ((unsigned)body[13] << 16) | ((unsigned)body[14] << 8) | body[15];

  if (s->verbose)
    log_line(TOOL_NAME ": EMM caid=%04X provid=%08X len=%zu", caid, provid, buflen);
  atomic_fetch_add_explicit(&s->emm_total, 1, memory_order_relaxed);

  if (s->emm_cb)
    s->emm_cb(body + 20, buflen, caid, provid, s->user);
}
