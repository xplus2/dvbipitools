/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_TEST_H2_RIG_H
#define DIPIXY_TEST_H2_RIG_H

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "h2_client.h"
#include "dipixy/http2/http2.h"
#include "dipixy/http2/http2_int.h"
#include "dipixy/reactor/conn.h"
#include "dipixy/reactor/internal.h"

typedef struct {
  int sv[2];
  int epfd;
  conn_t *c;
  h2_conn_t *h2;
  h2c_t cl;
  config_t cfg;
} rig_t;

static inline void rig_open(rig_t *r) {
  memset(r, 0, sizeof *r);
  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, r->sv), 0);
  ck_assert_int_eq(fcntl(r->sv[0], F_SETFL, O_NONBLOCK), 0);
  ck_assert_int_eq(fcntl(r->sv[1], F_SETFL, O_NONBLOCK), 0);
  r->epfd = epoll_create1(0);
  ck_assert_int_ge(r->epfd, 0);
  conn_table_init(r->sv[1] + 64);
  reactor_set_context(&r->cfg, NULL, NULL);
  r->c = conn_new(r->sv[0], NULL);
  ck_assert_ptr_nonnull(r->c);
  r->c->epfd = r->epfd;
  ck_assert_int_eq(epoll_ctl(r->epfd, EPOLL_CTL_ADD, r->sv[0], &(struct epoll_event){.events = EPOLLIN, .data.fd = r->sv[0]}), 0);
  ck_assert_int_eq(h2_conn_attach(r->c), 0);
  r->h2 = r->c->h2;
  h2c_init(&r->cl);
}

static inline int rig_server_alive(const rig_t *r) {
  return conn_for_fd(r->sv[0]) != NULL;
}

static inline void rig_close(rig_t *r) {
  if (rig_server_alive(r)) h2_conn_close(r->epfd, r->c);
  h2c_free(&r->cl);
  close(r->sv[1]);
  close(r->epfd);
}

static inline void rig_client_send_raw(rig_t *r, const uint8_t *data, size_t len) {
  ck_assert_int_eq((int)write(r->sv[1], data, len), (int)len);
}

static inline void rig_client_send(rig_t *r) {
  uint8_t buf[H2C_IO_BUF];
  size_t n = h2c_take(&r->cl, buf, sizeof buf);

  if (n) rig_client_send_raw(r, buf, n);
}

static inline void rig_client_recv(rig_t *r) {
  uint8_t buf[H2C_IO_BUF];
  ssize_t n;

  while ((n = read(r->sv[1], buf, sizeof buf)) > 0) h2c_feed(&r->cl, buf, (size_t)n);
}

static inline size_t rig_scan_frames(rig_t *r, h2c_frame_t *out, size_t cap) {
  uint8_t buf[H2C_IO_BUF];
  ssize_t n = read(r->sv[1], buf, sizeof buf);

  return n > 0 ? h2c_scan_frames(buf, (size_t)n, out, cap) : 0;
}

static inline void rig_server_read(rig_t *r) {
  h2_handle_readable(r->epfd, r->c);
}

static inline void rig_feed_server_direct(rig_t *r) {
  uint8_t buf[H2C_IO_BUF];
  ssize_t n;

  rig_client_send(r);
  while ((n = read(r->sv[0], buf, sizeof buf)) > 0) ck_assert_int_ge((int)nghttp2_session_mem_recv(r->h2->ng, buf, (size_t)n), 0);
}

static inline void rig_settle_server_output(rig_t *r) {
  h2_flush_tx(r->h2, r->c);
  rig_client_recv(r);
}

static inline void rig_exchange(rig_t *r) {
  for (int i = 0; i < 4; i++) {
    rig_client_send(r);
    if (rig_server_alive(r)) rig_server_read(r);
    rig_client_recv(r);
  }
}

static inline int32_t rig_request(rig_t *r, const char *method, const char *path, const nghttp2_nv *extra, size_t n_extra) {
  return h2c_request(&r->cl, method, path, extra, n_extra);
}

#endif
