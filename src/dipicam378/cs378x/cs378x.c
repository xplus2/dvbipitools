/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
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
  int waited_ms = 0;
  if (!s)
    return;
  atomic_store_explicit(&s->stop, 1, memory_order_relaxed);
  pthread_join(s->accept_thread, NULL);
  close(s->listen_fd);

  for (;;) {
    int i, any_active = 0;
    for (i = 0; i < CS378X_MAX_CONNS; i++)
      if (atomic_load_explicit(&s->worker_active[i], memory_order_acquire))
        any_active = 1;
    if (!any_active || waited_ms > 5000)
      break;
    usleep(50 * 1000);
    waited_ms += 50;
  }
  free(s);
}
