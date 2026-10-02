/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_TEST_INPUT_HTTP_FIXTURE_H
#define DIPIRADIOHEAD_TEST_INPUT_HTTP_FIXTURE_H

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
  int listen_fd;
  const char *const *responses;
  const size_t *response_lens;
  int n_responses;
  const char *one;
  size_t one_len;
} http_fixture_t;

static inline int fixture_listener(unsigned *port_out) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 4), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static inline void fixture_read_request(int cfd) {
  struct timeval tv = {2, 0};
  char buf[4096];
  size_t got = 0;

  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (;;) {
    ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
    if (n <= 0) break;
    got += (size_t)n;
    if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0) break;
  }
}

static inline void fixture_send_all(int fd, const void *data, size_t len) {
  const char *p = data;

  while (len > 0) {
    ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
    if (n <= 0) return;
    p += n;
    len -= (size_t)n;
  }
}

static inline void fixture_single(http_fixture_t *f, int listen_fd, const char *resp, size_t len) {
  f->listen_fd = listen_fd;
  f->one = resp;
  f->one_len = len;
  f->responses = &f->one;
  f->response_lens = &f->one_len;
  f->n_responses = 1;
}

static inline void *fixture_serve(void *arg) {
  const http_fixture_t *f = arg;

  for (int i = 0; i < f->n_responses; i++) {
    int cfd = accept(f->listen_fd, NULL, NULL);

    if (cfd < 0) return NULL;
    fixture_read_request(cfd);
    fixture_send_all(cfd, f->responses[i], f->response_lens[i]);
    close(cfd);
  }
  return NULL;
}

#define FIXTURE_MP3_FRAME_LEN 417

static inline void fixture_mp3_frames(unsigned char *out, unsigned n) {
  memset(out, 0, (size_t)n * FIXTURE_MP3_FRAME_LEN);
  for (unsigned i = 0; i < n; i++) {
    out[i * FIXTURE_MP3_FRAME_LEN + 0] = 0xFF;
    out[i * FIXTURE_MP3_FRAME_LEN + 1] = 0xFB;
    out[i * FIXTURE_MP3_FRAME_LEN + 2] = 0x90;
  }
}

#endif
