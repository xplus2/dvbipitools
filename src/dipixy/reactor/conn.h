/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* per-conn state + non-blocking output queue for epoll reactor */

#ifndef DIPIXY_CONN_H
#define DIPIXY_CONN_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <time.h>

/* reactor conn lifecycle states */
typedef enum {
  CONN_TLS_HANDSHAKE, /* implicit handshake completing on first SSL_read/write */
  CONN_READING,       /* accumulating an HTTP request */
  CONN_DISPATCH,      /* complete request ready for handlers */
  CONN_WRITING,       /* draining output queue */
  CONN_WS,            /* upgraded WebSocket, reading frames */
  CONN_H2,            /* HTTP/2 async: nghttp2 session */
  CONN_TSPUSH,        /* MPEG-TS push: output-only. packets arrive via conn_send_buffered */
  CONN_CLOSING
} conn_state;

/* grow-only byte buffer. out: [off,len) unsent region. in: off = parse cursor */
typedef struct {
  unsigned char *buf;
  size_t cap;
  size_t len;
  size_t off;
} conn_buf;

/* zc release callback, runs exactly once */
typedef void (*conn_zc_release_fn)(void *arg);

/* external send: caller buf, no copy. off = bytes sent, unconfirmed */
typedef struct {
  const unsigned char *data;
  size_t len, off;
  conn_zc_release_fn release;
  void *release_arg;
  int active;
  int any_zc;      /* 1 if any byte of this chunk went via MSG_ZEROCOPY */
  int last_zc_id;  /* highest zc id used by this chunk, -1 = none yet */
} conn_zc_t;

/* sent, awaiting errqueue confirm */
typedef struct {
  int zc_id_hi;
  conn_zc_release_fn release;
  void *release_arg;
} conn_zc_await_t;

#define CONN_ZC_AWAIT_MAX 8

/* hard ceiling, see conn_table_capacity() */
#define CONN_TABLE_MAX (1 << 20)

typedef struct conn_t {
  int fd;
  conn_state state;
  conn_buf in;
  conn_buf out;
  void *ssl;               /* SSL* when TLS, NULL for plain */
  /* owner-thread bits: only owning reactor touches, never under out_lock.
     must not share a word with want_write/dead below */
  unsigned close_after_flush
      : 1;                      /* handler "closed": reactor closes on drain */
  unsigned become_ws : 1;       /* WS upgrade: after 101 drains, switch conn to CONN_WS frame reading */
  unsigned become_tspush : 1;   /* MPEG-TS push: after headers drain, keep as output-only CONN_TSPUSH */
  unsigned requested_close : 1; /* other thread asked owner to close: flush, then quiet teardown (no slot/room cleanup, requester already did it) */
  unsigned keep_alive : 1;      /* after flush: recycle for next HTTP/1.1 request */
  int slot;                     /* owning user[] slot, or 0 if none */
  int reactor_tid;              /* owning reactor thread index (-1 until published as WS/STREAM) */
  void *ws;                     /* ws_conn_state* once CONN_WS (reactor-owned) */
  void *h2;                     /* h2_conn_t* once CONN_H2 (reactor-owned) */
  char client_ip[64];           /* peer IP, restored into t_req per dispatch */
  time_t last_active;
  size_t req_bytes; /* request bytes consumed from in.buf, incl trailing blank
                        line. reactor_keepalive() preserves pipelined bytes past it */
  _Alignas(64) _Atomic unsigned char want_write; /* EPOLLOUT currently armed for this fd */
  unsigned char dead;                      /* backpressure tripped: owner must close */
  unsigned char read_done;                 /* recv()==0: blocks EPOLLIN re-arm */
  int epfd;
  pthread_mutex_t out_lock;
  /* MSG_ZEROCOPY state, owner-thread-only */
  conn_zc_t zc;
  conn_zc_await_t zc_await[CONN_ZC_AWAIT_MAX];
  int zc_await_count;
  int zc_next_id;      /* shadows kernel per-fd zc send count */
  int zc_confirmed_id; /* highest confirmed zc id, -1 = none */
} conn_t;

enum {
  CONN_FLUSH_DONE = 0,  /* output queue fully drained */
  CONN_FLUSH_MORE = 1,  /* partial write, EPOLLOUT armed */
  CONN_FLUSH_ERROR = -1 /* fatal socket error, caller closes */
};

/* NULL return: OOM */
conn_t *conn_new(int fd, void *ssl);
/* does not close fd or free ssl, caller's job */
void conn_free(conn_t *c);

/* never touches socket, never blocks */
int conn_queue(conn_t *c, const void *data, size_t len);

/* zc send, no memcpy. falls back to conn_queue()+release: TLS, busy chunk, full await ring */
int conn_queue_zc(conn_t *c, const void *data, size_t len, conn_zc_release_fn release, void *release_arg);

/* arms EPOLLOUT on epfd if socket blocks with data pending, disarms once drained */
int conn_flush(conn_t *c, int epfd);

/* drains errqueue, releases matured zc_await[]. no-op without zerocopy */
void conn_zc_drain(conn_t *c);

/* EPOLLIN, +EPOLLOUT if want_out. return value: epoll_ctl's result */
int conn_epoll_mod(conn_t *c, int epfd, int want_out);

/* claims teardown of c under out_lock: 1 if this call set dead (caller proceeds), 0=sth else already
   did (caller returns, no-op). must run first on conn/stream/ws/tspush close.
   races a live broadcaster's epoll_ctl safely, no-op double teardown */
static inline int conn_claim_teardown(conn_t *c) {
  pthread_mutex_lock(&c->out_lock);
  if (c->dead) {
    pthread_mutex_unlock(&c->out_lock);
    return 0;
  }
  c->dead = 1;
  pthread_mutex_unlock(&c->out_lock);
  return 1;
}

int conn_in_reserve(conn_t *c, size_t need);

/* not RLIMIT_NOFILE-sized, see reactor_raise_nofile_limit(). headroom for non-stream conns (status/ws/dlna) */
int conn_table_capacity(int max_clients);

/* call once before reactor threads run. no-op if already sized or calloc fails */
int conn_table_init(int maxfd);
/* owning reactor thread calls conn_publish/conn_unpublish.
   conn_for_fd() safe from any thread (single-writer, atomic ptr) */
void conn_publish(conn_t *c);
void conn_unpublish(const conn_t *c);
conn_t *conn_for_fd(int fd);

/* callable from any thread. over backpressure cap marks conn dead, returns -1, else 0 */
int conn_send_buffered(conn_t *c, const void *a, size_t alen, const void *b, size_t blen);

/* safe from any thread, flushes pending output first. caller handles slot_reset/room cleanup, owner only closes fd + frees conn */
void conn_request_close(conn_t *c);

#endif /* DIPIXY_CONN_H */

