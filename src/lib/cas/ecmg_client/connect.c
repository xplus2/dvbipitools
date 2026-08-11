/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/log.h"

#include "priv.h"

/* 1 connected, -1 refused/error, 0 timed out or stop requested */
static int wait_connect(ecmg_client_t *c, int fd) {
  int elapsed = 0;
  while (elapsed < ECMG_HANDSHAKE_TIMEOUT_MS) {
    struct pollfd pfd;
    int step = ECMG_POLL_INTERVAL_MS;
    int pret;
    if (ecmg_stopping(c))
      return 0;
    if (step > ECMG_HANDSHAKE_TIMEOUT_MS - elapsed)
      step = ECMG_HANDSHAKE_TIMEOUT_MS - elapsed;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    pret = poll(&pfd, 1, step);
    if (pret > 0) {
      int soerr = 0;
      socklen_t sl = sizeof soerr;
      getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
      if (soerr == 0)
        return 1;
      errno = soerr;
      return -1;
    }
    if (pret < 0 && errno != EINTR)
      return -1;
    elapsed += step;
  }
  return 0;
}

/* nonblocking connect, poll-with-timeout: a blocking connect() to an unreachable
   host can hang for minutes at the OS level, blocking pthread_join() at shutdown */
static int tcp_dial(ecmg_client_t *c, const char *host, unsigned port) {
  struct addrinfo hints, *res, *ai;
  char portstr[6];
  int fd = -1, e, save_errno = 0;

  snprintf(portstr, sizeof portstr, "%u", port);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  e = getaddrinfo(host, portstr, &hints, &res);
  if (e) {
    log_line("ecmg: resolve %s: %s", host, gai_strerror(e));
    return -1;
  }
  for (ai = res; ai; ai = ai->ai_next) {
    int flags, cr;
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0)
      continue;
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
    cr = wait_connect(c, fd);
    if (cr == 1)
      break;
    save_errno = (cr == -1) ? errno : ETIMEDOUT;
    close(fd);
    fd = -1;
    if (ecmg_stopping(c))
      break;
  }
  freeaddrinfo(res);
  if (fd < 0) {
    if (!ecmg_stopping(c))
      log_line("ecmg: connect %s:%u: %s", host, port, strerror(save_errno));
    return -1;
  }
  return fd; /* already O_NONBLOCK, set before connect() */
}

int wait_for_message(ecmg_client_t *c, simulcrypt_reader_t *rd, int fd, int total_timeout_ms, simulcrypt_hdr_t *hdr, const unsigned char **payload) {
  int elapsed = 0;
  while (elapsed < total_timeout_ms) {
    int step = ECMG_POLL_INTERVAL_MS;
    int rc;
    if (ecmg_stopping(c))
      return 0;
    if (step > total_timeout_ms - elapsed)
      step = total_timeout_ms - elapsed;
    rc = simulcrypt_reader_poll(rd, fd, step, hdr, payload);
    if (rc != 0)
      return rc;
    elapsed += step;
  }
  return 0;
}

/* dials, negotiates protocol_version, opens channel+1 stream.
   0 ok (fills lead_cw/cw_per_msg/max_comp_time_ms, fd left open), -1 err (fd closed) */
int connect_and_setup(ecmg_client_t *c, int *out_fd, unsigned char *out_version, unsigned *out_lead_cw, unsigned *out_cw_per_msg, unsigned *out_max_comp_time_ms) {
  unsigned char version = c->cfg.version_max;
  int tried_min = (version == c->cfg.version_min);

  for (;;) {
    unsigned char msg[SIMULCRYPT_MAX_FRAME];
    size_t len;
    simulcrypt_reader_t rd;
    simulcrypt_hdr_t hdr;
    const unsigned char *payload;
    int fd = tcp_dial(c, c->cfg.host, c->cfg.port);
    if (fd < 0)
      return -1;

    len = ecmg_build_channel_setup(msg, sizeof msg, version, c->cfg.super_cas_id);
    if (!len || simulcrypt_send_all(fd, msg, len, ECMG_HANDSHAKE_TIMEOUT_MS) < 0) {
      close(fd);
      return -1;
    }
    simulcrypt_reader_init(&rd);
    if (wait_for_message(c, &rd, fd, ECMG_HANDSHAKE_TIMEOUT_MS, &hdr, &payload) != 1) {
      if (!ecmg_stopping(c))
        log_line("ecmg: no channel_status/channel_error reply");
      close(fd);
      return -1;
    }
    if (hdr.type == ECMG_MSG_CHANNEL_ERROR) {
      unsigned short err = 0;
      ecmg_find_error_status(payload, hdr.payload_len, &err);
      close(fd);
      if (err == ECMG_ERR_UNSUPPORTED_PROTOCOL_VERSION && !tried_min) {
        version = c->cfg.version_min;
        tried_min = 1;
        continue;
      }
      log_line("ecmg: channel_setup rejected, error_status=0x%04x", err);
      return -1;
    }
    if (hdr.type != ECMG_MSG_CHANNEL_STATUS) {
      log_line("ecmg: unexpected reply 0x%04x to channel_setup", hdr.type);
      close(fd);
      return -1;
    }

    {
      unsigned lead_cw, cw_per_msg, max_comp_time_ms, min_cp_100ms, ecm_rep_period_ms;
      if (ecmg_parse_channel_status(payload, hdr.payload_len, &lead_cw, &cw_per_msg, &max_comp_time_ms, &min_cp_100ms, &ecm_rep_period_ms) < 0) {
        log_line("ecmg: malformed or unusable channel_status");
        close(fd);
        return -1;
      }
      if (min_cp_100ms && c->cfg.cp_duration_ms < min_cp_100ms * 100)
        log_line("ecmg: --cas-cp-duration %ums is below the ECMG's min_CP_duration %ums, using it anyway", c->cfg.cp_duration_ms, min_cp_100ms * 100);
      *out_lead_cw = lead_cw;
      *out_cw_per_msg = cw_per_msg;
      *out_max_comp_time_ms = max_comp_time_ms ? max_comp_time_ms : 1000;
      atomic_store_explicit(&c->ecm_rep_period_ms, ecm_rep_period_ms, memory_order_relaxed);
    }

    len = ecmg_build_stream_setup(msg, sizeof msg, version, c->cfg.ecm_id, c->cfg.cp_duration_ms / 100);
    if (!len || simulcrypt_send_all(fd, msg, len, ECMG_HANDSHAKE_TIMEOUT_MS) < 0) {
      close(fd);
      return -1;
    }
    if (wait_for_message(c, &rd, fd, ECMG_HANDSHAKE_TIMEOUT_MS, &hdr, &payload) != 1) {
      if (!ecmg_stopping(c))
        log_line("ecmg: no stream_status/stream_error reply");
      close(fd);
      return -1;
    }
    if (hdr.type != ECMG_MSG_STREAM_STATUS) {
      unsigned short err = 0;
      if (hdr.type == ECMG_MSG_STREAM_ERROR)
        ecmg_find_error_status(payload, hdr.payload_len, &err);
      log_line("ecmg: stream_setup rejected, reply=0x%04x error_status=0x%04x", hdr.type, err);
      close(fd);
      return -1;
    }

    *out_fd = fd;
    *out_version = version;
    log_line("ecmg: channel+stream established (protocol_version=0x%02x, lead_cw=%u, cw_per_msg=%u)", version, *out_lead_cw, *out_cw_per_msg);
    return 0;
  }
}
