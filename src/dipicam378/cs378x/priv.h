/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPICAM378_CS378X_PRIV_H
#define DIPICAM378_CS378X_PRIV_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>

#include "cs378x.h"

#define CS378X_MAX_CONNS 4
#define CS378X_POLL_INTERVAL_MS 150
#define CS378X_RECV_TIMEOUT_MS 200
#define CS378X_SEND_TIMEOUT_MS 3000
#define CS378X_BUF_CAP 2048
#define CS378X_MIN_FRAME 36 /* 4-byte ucrc + 2 AES blocks */

/* command bytes, camd35/cs378x family */
#define CMD_ECM_REQUEST 0
#define CMD_ECM_RESPONSE 1
#define CMD_EMM 6
#define CMD_EMM_1830 19
#define CMD_KEEPALIVE 55
#define CMD_INVALID 8 /* "stop sending requests for srvid/prvid/caid" */

typedef struct {
  cs378x_server_t *s;
  int fd;
  int slot;
} worker_arg_t;

struct cs378x_server {
  int listen_fd;
  atomic_int stop;
  pthread_t accept_thread;
  atomic_int worker_active[CS378X_MAX_CONNS];
  pthread_t worker_thread[CS378X_MAX_CONNS];
  int worker_thread_joinable[CS378X_MAX_CONNS];
  atomic_int worker_fd[CS378X_MAX_CONNS];
  worker_arg_t worker_args[CS378X_MAX_CONNS]; /* no per-connection malloc, reuse gated by worker_active[] */

  unsigned char aes_key[16]; /* MD5(password) */
  unsigned char expected_ucrc[4]; /* crc32(MD5(username)); only checked if check_ucrc */
  int check_ucrc;
  int verbose;

  cs378x_ecm_cb ecm_cb;
  cs378x_emm_cb emm_cb;
  void *user;

  atomic_ullong connections_total;
  atomic_ullong auth_errors_total[CAM_AUTH_REASON_COUNT];
  atomic_ullong ecm_total;
  atomic_ullong ecm_errors_total;
  atomic_ullong emm_total;
};

/* protocol.c */
void handle_ecm(cs378x_server_t *s, int fd, const unsigned char conn_ucrc[4], unsigned char *body, size_t buflen);
void handle_emm(cs378x_server_t *s, unsigned char *body, size_t buflen);
void send_keepalive_answer(cs378x_server_t *s, int fd, const unsigned char conn_ucrc[4]);

/* worker.c */
int tcp_listen_dualstack(unsigned port);
void *accept_main(void *arg);

#endif
