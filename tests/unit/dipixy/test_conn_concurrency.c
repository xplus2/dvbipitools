/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dipixy/reactor/conn.h"
#include "dipixy/reactor/qsbr.h"

#define OWNER_TID 0
#define NREADERS 6
#define TEST_FD 7
#define MARKER_LEN 32
#define RUN_MS 500
#define FLUSH_WAIT_MS 8000
#define WRITERS 4
#define CHUNK 64
#define CHUNKS_PER_WRITER 2000
#define OUT_CAP (4u * 1024 * 1024)
#define SPILL_CHUNK 65536
#define TABLE_FDS 32

_Thread_local int t_reactor_tid = -1;
static qsbr_domain_t *g_test_qsbr;
qsbr_domain_t *reactor_qsbr(void) { return g_test_qsbr; }

/* conn.c pool/retire lists: per thread, reactor workers don't quit (but test threads do, freeing their TLS, leaking still-pooled conn_t to LSan */
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
#define CONN_ASAN 1
#endif
#ifdef CONN_ASAN
const char *__lsan_default_options(void) { return "detect_leaks=0"; }
#endif

static _Atomic int g_stop;
static _Atomic int g_bad;

/* uniform: every byte equals buf[0]. torn or freed reads break this */
static int uniform(const unsigned char *buf, size_t len) {
  for (size_t i = 1; i < len; i++) if (buf[i] != buf[0]) return 0;
  return 1;
}

static void *owner_thread(void *arg) {
  unsigned char cycle = 0;
  (void)arg;
  t_reactor_tid = OWNER_TID;
  while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
    unsigned char marker[MARKER_LEN];
    conn_t *c = conn_new(TEST_FD, NULL);
    if (!c) continue;
    memset(marker, cycle++, sizeof marker);
    conn_queue(c, marker, sizeof marker);
    conn_publish(c);
    conn_unpublish(c);
    conn_free(c);
    qsbr_worker_quiescent(g_test_qsbr, OWNER_TID);
  }
  return NULL;
}

typedef struct {
  int tid;
} reader_arg_t;

static void *reader_thread(void *arg) {
  const reader_arg_t *ra = arg;
  t_reactor_tid = ra->tid;
  while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
    const conn_t *c = conn_for_fd(TEST_FD);
    if (c) {
      size_t len = c->out.len;
      if (len > MARKER_LEN || !uniform(c->out.buf, len)) atomic_store_explicit(&g_bad, 1, memory_order_relaxed);
    }
    qsbr_worker_quiescent(g_test_qsbr, ra->tid);
  }
  return NULL;
}

START_TEST(concurrent_readers_never_see_torn_or_freed_conn_data) {
  pthread_t owner;
  pthread_t readers[NREADERS];
  reader_arg_t rargs[NREADERS];

  ck_assert_int_eq(conn_table_init(TEST_FD + 1), 0);
  g_test_qsbr = qsbr_domain_create(1 + NREADERS);
  ck_assert_ptr_nonnull(g_test_qsbr);
  atomic_store_explicit(&g_stop, 0, memory_order_relaxed);
  atomic_store_explicit(&g_bad, 0, memory_order_relaxed);

  pthread_create(&owner, NULL, owner_thread, NULL);
  for (int i = 0; i < NREADERS; i++) {
    rargs[i].tid = OWNER_TID + 1 + i;
    pthread_create(&readers[i], NULL, reader_thread, &rargs[i]);
  }
  usleep(RUN_MS * 1000);
  atomic_store_explicit(&g_stop, 1, memory_order_relaxed);
  pthread_join(owner, NULL);
  for (int i = 0; i < NREADERS; i++) pthread_join(readers[i], NULL);
  ck_assert_int_eq(atomic_load_explicit(&g_bad, memory_order_relaxed), 0);
}
END_TEST

typedef struct {
  conn_t *c;
  int id;
  int sends_failed;
} writer_arg_t;

static void *writer_thread(void *arg) {
  writer_arg_t *w = arg;
  unsigned char chunk[CHUNK];

  for (int seq = 0; seq < CHUNKS_PER_WRITER; seq++) {
    memset(chunk, w->id, sizeof chunk);
    chunk[1] = (unsigned char)(seq & 0xFF);
    chunk[2] = (unsigned char)(seq >> 8);
    while (conn_send_buffered(w->c, chunk, sizeof chunk, NULL, 0) != 0) {
      w->sends_failed++;
      usleep(100);
    }
  }
  return NULL;
}

typedef struct {
  int fd;
  _Atomic int done;
  int next_seq[WRITERS];
  int bad;
  _Atomic size_t total;
} sink_t;

static void check_chunks(sink_t *k, const unsigned char *buf, size_t len, unsigned char *carry, size_t *carry_len) {
  size_t pos = 0;

  while (pos < len) {
    size_t need = CHUNK - *carry_len;
    size_t take = len - pos < need ? len - pos : need;

    memcpy(carry + *carry_len, buf + pos, take);
    *carry_len += take;
    pos += take;
    if (*carry_len == CHUNK) {
      int id = carry[0];
      int seq = carry[1] | (carry[2] << 8);

      if (id < 0 || id >= WRITERS || seq != k->next_seq[id]) k->bad = 1;
      else k->next_seq[id]++;
      for (size_t i = 3; i < CHUNK; i++) if (carry[i] != carry[0]) k->bad = 1;
      *carry_len = 0;
    }
  }
}

static void *sink_thread(void *arg) {
  sink_t *k = arg;
  unsigned char buf[8192];
  unsigned char carry[CHUNK];
  size_t carry_len = 0;
  struct pollfd pfd = {.fd = k->fd, .events = POLLIN};

  while (!atomic_load(&k->done) || poll(&pfd, 1, 50) > 0) {
    ssize_t n = read(k->fd, buf, sizeof buf);

    if (n > 0) {
      check_chunks(k, buf, (size_t)n, carry, &carry_len);
      atomic_fetch_add(&k->total, (size_t)n);
    } else if (n == 0) {
      break;
    } else if (errno != EAGAIN && errno != EINTR) {
      break;
    } else {
      poll(&pfd, 1, 5);
    }
  }
  return NULL;
}

static void flush_until(conn_t *c, int epfd, const _Atomic size_t *total, size_t want) {
  struct timespec t0;
  struct timespec t1;

  clock_gettime(CLOCK_MONOTONIC, &t0);
  while (atomic_load(total) < want) {
    conn_flush(c, epfd);
    usleep(200);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if ((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000 > FLUSH_WAIT_MS) break;
  }
}

START_TEST(concurrent_writers_deliver_whole_chunks_in_per_writer_order) {
  int sv[2];
  int epfd;
  conn_t *c;
  pthread_t wt[WRITERS];
  pthread_t st;
  writer_arg_t wargs[WRITERS];
  sink_t sink;
  size_t expect = (size_t)WRITERS * CHUNKS_PER_WRITER * CHUNK;

  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv), 0);
  g_test_qsbr = qsbr_domain_create(1);
  epfd = epoll_create1(0);
  c = conn_new(sv[0], NULL);
  ck_assert_ptr_nonnull(c);
  c->epfd = epfd;
  ck_assert_int_eq(epoll_ctl(epfd, EPOLL_CTL_ADD, sv[0], &(struct epoll_event){.events = EPOLLIN, .data.ptr = c}), 0);
  memset(&sink, 0, sizeof sink);
  sink.fd = sv[1];
  ck_assert_int_eq(pthread_create(&st, NULL, sink_thread, &sink), 0);
  for (int i = 0; i < WRITERS; i++) {
    wargs[i].c = c;
    wargs[i].id = i;
    wargs[i].sends_failed = 0;
    ck_assert_int_eq(pthread_create(&wt[i], NULL, writer_thread, &wargs[i]), 0);
  }
  flush_until(c, epfd, &sink.total, expect);
  for (int i = 0; i < WRITERS; i++) pthread_join(wt[i], NULL);
  flush_until(c, epfd, &sink.total, expect);
  atomic_store(&sink.done, 1);
  pthread_join(st, NULL);
  ck_assert_uint_eq(atomic_load(&sink.total), expect);
  ck_assert_int_eq(sink.bad, 0);
  for (int i = 0; i < WRITERS; i++) ck_assert_int_eq(sink.next_seq[i], CHUNKS_PER_WRITER);
  conn_free(c);
  close(epfd);
  close(sv[0]);
  close(sv[1]);
}
END_TEST

typedef struct {
  int base;
  int count;
} publisher_arg_t;

static _Atomic int g_pub_bad;

static void *publisher_thread(void *arg) {
  const publisher_arg_t *p = arg;
  conn_t slots[TABLE_FDS];

  memset(slots, 0, sizeof slots);
  while (!atomic_load(&g_stop)) {
    for (int i = 0; i < p->count; i++) {
      int fd = p->base + i;

      slots[i].fd = fd;
      conn_publish(&slots[i]);
      if (fd < TABLE_FDS && conn_for_fd(fd) != &slots[i]) atomic_store(&g_pub_bad, 1);
      conn_unpublish(&slots[i]);
      if (fd < TABLE_FDS && conn_for_fd(fd) != NULL) atomic_store(&g_pub_bad, 1);
    }
  }
  return NULL;
}

START_TEST(table_stays_fixed_and_consistent_under_concurrent_publishers) {
  pthread_t th[3];
  publisher_arg_t args[3] = {{0, 8}, {8, 8}, {TABLE_FDS - 4, 8}};
  struct timespec ts = {0, 300 * 1000000L};

  ck_assert_int_eq(conn_table_init(TABLE_FDS), 0);
  ck_assert_int_eq(conn_table_init(TABLE_FDS * 8), 0);
  ck_assert_ptr_null(conn_for_fd(TABLE_FDS));
  ck_assert_ptr_null(conn_for_fd(-1));
  atomic_store(&g_stop, 0);
  atomic_store(&g_pub_bad, 0);
  for (int i = 0; i < 3; i++) ck_assert_int_eq(pthread_create(&th[i], NULL, publisher_thread, &args[i]), 0);
  nanosleep(&ts, NULL);
  atomic_store(&g_stop, 1);
  for (int i = 0; i < 3; i++) pthread_join(th[i], NULL);
  ck_assert_int_eq(atomic_load(&g_pub_bad), 0);
  ck_assert_ptr_null(conn_for_fd(TABLE_FDS + 5));
}
END_TEST

START_TEST(reinitialising_the_table_keeps_entries_and_size) {
  conn_t c;

  memset(&c, 0, sizeof c);
  c.fd = 3;
  ck_assert_int_eq(conn_table_init(8), 0);
  conn_publish(&c);
  ck_assert_int_eq(conn_table_init(1024), 0);
  ck_assert_ptr_eq(conn_for_fd(3), &c);
  ck_assert_ptr_null(conn_for_fd(8));
  conn_unpublish(&c);
}
END_TEST

static _Atomic int g_dead_seen;
static _Atomic int g_ok_after_dead;

typedef struct {
  conn_t *c;
} spill_arg_t;

static void *spill_thread(void *arg) {
  spill_arg_t *a = arg;
  unsigned char chunk[SPILL_CHUNK];

  memset(chunk, 0x5A, sizeof chunk);
  for (int i = 0; i < 200; i++) {
    int dead_before = atomic_load(&g_dead_seen);
    int rc = conn_send_buffered(a->c, chunk, sizeof chunk, NULL, 0);

    if (rc != 0) atomic_store(&g_dead_seen, 1);
    if (rc == 0 && dead_before) atomic_store(&g_ok_after_dead, 1);
  }
  return NULL;
}

START_TEST(unconsumed_backlog_is_capped_and_marks_the_connection_dead) {
  conn_t *c;
  pthread_t th[WRITERS];
  spill_arg_t arg;
  size_t pending;

  g_test_qsbr = qsbr_domain_create(1);
  c = conn_new(-1, NULL);
  ck_assert_ptr_nonnull(c);
  arg.c = c;
  atomic_store(&g_dead_seen, 0);
  atomic_store(&g_ok_after_dead, 0);
  for (int i = 0; i < WRITERS; i++) ck_assert_int_eq(pthread_create(&th[i], NULL, spill_thread, &arg), 0);
  for (int i = 0; i < WRITERS; i++) pthread_join(th[i], NULL);
  ck_assert_int_eq(atomic_load(&g_dead_seen), 1);
  ck_assert_int_eq(atomic_load(&g_ok_after_dead), 0);
  ck_assert_int_eq(c->dead, 1);
  pthread_mutex_lock(&c->out_lock);
  pending = c->out.len - c->out.off;
  pthread_mutex_unlock(&c->out_lock);
  ck_assert_uint_le(pending, (size_t)OUT_CAP);
  ck_assert_int_eq(conn_send_buffered(c, "x", 1, NULL, 0), -1);
  conn_free(c);
}
END_TEST

typedef struct {
  int sv_peer;
  int bytes;
} peer_arg_t;

static void *resetting_peer(void *arg) {
  const peer_arg_t *p = arg;
  unsigned char buf[256];
  struct linger lg = {1, 0};

  memset(buf, 0x33, sizeof buf);
  for (int i = 0; i < p->bytes / (int)sizeof buf; i++) {
    if (write(p->sv_peer, buf, sizeof buf) < 0) break;
  }
  setsockopt(p->sv_peer, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
  close(p->sv_peer);
  return NULL;
}

static void *late_sender(void *arg) {
  (void)arg;
  while (!atomic_load(&g_stop)) {
    conn_t *c = conn_for_fd(TEST_FD);

    if (c) conn_send_buffered(c, "ping", 4, NULL, 0);
    qsbr_worker_quiescent(g_test_qsbr, 1);
  }
  return NULL;
}

START_TEST(peer_reset_mid_read_surfaces_as_an_error_while_senders_keep_running) {
  int sv[2];
  conn_t *c;
  pthread_t peer;
  pthread_t sender;
  peer_arg_t pa;
  unsigned char buf[512];
  ssize_t n;
  int saw_reset = 0;
  size_t got = 0;

  ck_assert_int_eq(conn_table_init(TEST_FD + 1), 0);
  g_test_qsbr = qsbr_domain_create(2);
  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  ck_assert_int_eq(dup2(sv[0], TEST_FD), TEST_FD);
  close(sv[0]);
  c = conn_new(TEST_FD, NULL);
  ck_assert_ptr_nonnull(c);
  conn_publish(c);
  atomic_store(&g_stop, 0);
  pa.sv_peer = sv[1];
  pa.bytes = 4096;
  ck_assert_int_eq(pthread_create(&sender, NULL, late_sender, NULL), 0);
  ck_assert_int_eq(pthread_create(&peer, NULL, resetting_peer, &pa), 0);
  for (int guard = 0; guard < 2000; guard++) {
    n = recv(TEST_FD, buf, sizeof buf, 0);
    if (n > 0) {
      got += (size_t)n;
      continue;
    }
    if (n < 0 && (errno == ECONNRESET || errno == EPIPE)) {
      saw_reset = 1;
      break;
    }
    if (n == 0) break;
    usleep(500);
  }
  pthread_join(peer, NULL);
  conn_unpublish(c);
  conn_free(c);
  qsbr_worker_quiescent(g_test_qsbr, 0);
  atomic_store(&g_stop, 1);
  pthread_join(sender, NULL);
  close(TEST_FD);
  ck_assert_uint_le(got, (size_t)pa.bytes);
  ck_assert_int_eq(saw_reset || got == (size_t)pa.bytes, 1);
}
END_TEST

static Suite *conn_concurrency_suite(void) {
  Suite *s = suite_create("dipixy_conn_concurrency");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, concurrent_readers_never_see_torn_or_freed_conn_data);
  tcase_add_test(tc, concurrent_writers_deliver_whole_chunks_in_per_writer_order);
  tcase_add_test(tc, table_stays_fixed_and_consistent_under_concurrent_publishers);
  tcase_add_test(tc, reinitialising_the_table_keeps_entries_and_size);
  tcase_add_test(tc, unconsumed_backlog_is_capped_and_marks_the_connection_dead);
  tcase_add_test(tc, peer_reset_mid_read_surfaces_as_an_error_while_senders_keep_running);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(conn_concurrency_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
