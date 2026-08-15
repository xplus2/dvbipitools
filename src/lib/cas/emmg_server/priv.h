/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_EMMG_SERVER_PRIV_H
#define DVBIPITOOLS_LIB_CAS_EMMG_SERVER_PRIV_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>

#include "emmg_server.h"

#define EMMG_MAX_CONNS 8
#define EMMG_QUEUE_CAP 256
#define EMMG_POLL_INTERVAL_MS 150
#define EMMG_SEND_TIMEOUT_MS 3000

typedef struct {
  unsigned char data[EMMG_MAX_DATAGRAM_LEN];
  size_t len;
} emmg_queued_datagram_t;

struct emmg_server {
  int listen_fd;
  atomic_int stop;
  pthread_t accept_thread;

  atomic_int worker_active[EMMG_MAX_CONNS];
  pthread_t worker_thread[EMMG_MAX_CONNS];
  int worker_thread_joinable[EMMG_MAX_CONNS];

  pthread_mutex_t queue_lock;
  emmg_queued_datagram_t queue[EMMG_QUEUE_CAP];
  size_t queue_head;
  atomic_size_t queue_len; /* mutex-protected writes, lock-free read for dequeue's empty pre-check */

  atomic_ulong emm_total;
  atomic_ulong emm_dropped;
};

typedef struct {
  int have_channel;
  unsigned client_id;
  unsigned data_channel_id;
  int have_stream;
  unsigned data_stream_id;
  unsigned data_id;
  unsigned data_type;
} emmg_conn_state_t;

typedef struct {
  emmg_server_t *s;
  int fd;
  int slot;
} worker_arg_t;

/* protocol.c */
int find_u16(const unsigned char *body, size_t body_len, unsigned short tag, unsigned *out);
int find_u32(const unsigned char *body, size_t body_len, unsigned short tag, unsigned *out);
int find_u8(const unsigned char *body, size_t body_len, unsigned short tag, unsigned *out);

/* emmg_server.c */
void publish_datagram_cb(const unsigned char *data, unsigned short len, void *user);

/* worker.c */
void *accept_main(void *arg);

#endif
