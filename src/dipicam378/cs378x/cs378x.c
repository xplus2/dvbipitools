/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/log.h"

#include "../version.h"
#include "priv.h"

cs378x_server_t *cs378x_server_start(const cs378x_cfg_t *cfg, cs378x_ecm_cb ecm_cb, cs378x_emm_cb emm_cb, void *user) {
  cs378x_server_t *s;

  cs378x_crc32_init_table();

  s = calloc(1, sizeof *s);
  if (!s)
    return NULL;
  for (int i = 0; i < CS378X_MAX_CONNS; i++)
    atomic_init(&s->worker_fd[i], -1);

  if (cs378x_md5((const unsigned char *)cfg->password, strlen(cfg->password), s->aes_key) != 0) {
    free(s);
    return NULL;
  }
  if (cfg->username && cfg->username[0]) {
    unsigned char md5tmp[16];
    uint32_t crc;
    if (cs378x_md5((const unsigned char *)cfg->username, strlen(cfg->username), md5tmp) != 0) {
      free(s);
      return NULL;
    }
    crc = cs378x_crc32(md5tmp, sizeof md5tmp);
    s->expected_ucrc[0] = (unsigned char)(crc >> 24);
    s->expected_ucrc[1] = (unsigned char)(crc >> 16);
    s->expected_ucrc[2] = (unsigned char)(crc >> 8);
    s->expected_ucrc[3] = (unsigned char)crc;
    s->check_ucrc = 1;
  }
  s->verbose = cfg->verbose;
  s->ecm_cb = ecm_cb;
  s->emm_cb = emm_cb;
  s->user = user;

  s->listen_fd = tcp_listen_dualstack(cfg->port);
  if (s->listen_fd < 0) {
    free(s);
    return NULL;
  }

  if (pthread_create(&s->accept_thread, NULL, accept_main, s) != 0) {
    log_line(TOOL_NAME ": pthread_create: %s", strerror(errno));
    close(s->listen_fd);
    free(s);
    return NULL;
  }
  return s;
}

void cs378x_server_stop(cs378x_server_t *s) {
  if (!s)
    return;
  atomic_store_explicit(&s->stop, 1, memory_order_relaxed);
  pthread_join(s->accept_thread, NULL);
  close(s->listen_fd);

  for (int i = 0; i < CS378X_MAX_CONNS; i++) {
    int fd = atomic_load_explicit(&s->worker_fd[i], memory_order_acquire);
    if (fd >= 0)
      shutdown(fd, SHUT_RDWR);
  }
  for (int i = 0; i < CS378X_MAX_CONNS; i++)
    if (s->worker_thread_joinable[i]) {
      pthread_join(s->worker_thread[i], NULL);
      s->worker_thread_joinable[i] = 0;
    }
  free(s);
}

const char *cs378x_auth_reason_name(cam_auth_reason_t r) {
  switch (r) {
  case CAM_AUTH_USER:
    return "user";
  case CAM_AUTH_CONNID:
    return "connid";
  case CAM_AUTH_CHECKSUM:
    return "checksum";
  case CAM_AUTH_OVERSIZED:
    return "oversized";
  default:
    return "unknown";
  }
}

void cs378x_server_get_metrics(cs378x_server_t *s, cs378x_metrics_t *out) {
  memset(out, 0, sizeof *out);
  for (int i = 0; i < CS378X_MAX_CONNS; i++)
    out->connections_active += (unsigned)atomic_load_explicit(&s->worker_active[i], memory_order_relaxed);
  out->connections_total = atomic_load_explicit(&s->connections_total, memory_order_relaxed);
  for (int i = 0; i < CAM_AUTH_REASON_COUNT; i++)
    out->auth_errors_total[i] = atomic_load_explicit(&s->auth_errors_total[i], memory_order_relaxed);
  out->ecm_total = atomic_load_explicit(&s->ecm_total, memory_order_relaxed);
  out->ecm_errors_total = atomic_load_explicit(&s->ecm_errors_total, memory_order_relaxed);
  out->emm_total = atomic_load_explicit(&s->emm_total, memory_order_relaxed);
}
