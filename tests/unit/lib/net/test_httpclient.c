/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "lib/net/httpclient/httpclient.h"
#include "lib/net/httpclient/priv.h"

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define MAX_CHUNKS 4
#define MAX_SCRIPTS 8
#define CHUNK_CAP 20000

typedef enum { END_CLOSE, END_RESET, END_HOLD } end_t;

typedef struct {
  char data[MAX_CHUNKS][CHUNK_CAP];
  size_t len[MAX_CHUNKS];
  unsigned delay_ms[MAX_CHUNKS];
  int req_of[MAX_CHUNKS];
  int nrequests;
  int nchunks;
  end_t end;
  unsigned hold_ms;
} script_t;

typedef struct {
  int listen_fd;
  unsigned port;
  script_t scripts[MAX_SCRIPTS];
  int nscripts;
  pthread_t th;
} fake_t;

static void script_add(script_t *s, unsigned delay_ms, const char *data, size_t len) {
  ck_assert_int_lt(s->nchunks, MAX_CHUNKS);
  ck_assert_uint_lt(len, (size_t)CHUNK_CAP);
  memcpy(s->data[s->nchunks], data, len);
  s->len[s->nchunks] = len;
  s->delay_ms[s->nchunks] = delay_ms;
  s->nchunks++;
}

static void script_str(script_t *s, const char *str) {
  script_add(s, 0, str, strlen(str));
}

static void play(int cfd, const script_t *s) {
  struct timeval tv = {2, 0};
  char buf[4096];
  int nreq = s->nrequests ? s->nrequests : 1;
  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (int rq = 0; rq < nreq; rq++) {
    size_t got = 0;
    for (;;) {
      ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
      if (n <= 0) break;
      got += (size_t)n;
      if (got >= 4 && memmem(buf, got, "\r\n\r\n", 4)) break;
      if (got == sizeof buf) break;
    }
    for (int i = 0; i < s->nchunks; i++) {
      if (s->req_of[i] != rq) continue;
      if (s->delay_ms[i]) usleep(s->delay_ms[i] * 1000u);
      if (send(cfd, s->data[i], s->len[i], MSG_NOSIGNAL) < 0) break;
    }
  }
  if (s->end == END_HOLD) usleep(s->hold_ms * 1000u);
  if (s->end == END_RESET) {
    struct linger lg = {1, 0};
    setsockopt(cfd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
  }
  close(cfd);
}

static void *serve(void *arg) {
  fake_t *f = arg;
  for (int i = 0; i < f->nscripts; i++) {
    struct pollfd pfd = {.fd = f->listen_fd, .events = POLLIN, .revents = 0};
    int cfd;
    if (poll(&pfd, 1, 15000) <= 0) break;
    cfd = accept(f->listen_fd, NULL, NULL);
    if (cfd < 0) break;
    play(cfd, &f->scripts[i]);
  }
  return NULL;
}

static fake_t *fake_new(void) {
  fake_t *f = calloc(1, sizeof *f);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  ck_assert_ptr_nonnull(f);
  f->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(f->listen_fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(f->listen_fd, 8), 0);
  ck_assert_int_eq(getsockname(f->listen_fd, (struct sockaddr *)&addr, &alen), 0);
  f->port = ntohs(addr.sin_port);
  return f;
}

static void fake_go(fake_t *f) {
  ck_assert_int_eq(pthread_create(&f->th, NULL, serve, f), 0);
}

static void fake_end(fake_t *f) {
  pthread_join(f->th, NULL);
  close(f->listen_fd);
  free(f);
}

static http_t *fetch(fake_t *f, const char *path, const char *extra, net_err_reason_t *reason) {
  char uri[128];
  http_url_t url;
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u%s", f->port, path);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  return http_get(&url, "ua", 0, extra, reason);
}

static http_t *one_shot(const char *resp, net_err_reason_t *reason) {
  fake_t *f = fake_new();
  http_t *h;
  f->nscripts = 1;
  script_str(&f->scripts[0], resp);
  fake_go(f);
  h = fetch(f, "/x", NULL, reason);
  fake_end(f);
  return h;
}

START_TEST(icy_status_line_is_accepted) {
  net_err_reason_t reason = NET_ERR_OTHER;
  http_t *h = one_shot("ICY 200 OK\r\nicy-name: station\r\nContent-Length: 0\r\n\r\n", &reason);
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(http_status(h), 200);
  ck_assert_str_eq(http_header(h, "ICY-NAME"), "station");
  http_close(h);
}
END_TEST

START_TEST(icy_prefix_split_across_reads) {
  fake_t *f = fake_new();
  http_t *h;
  f->nscripts = 1;
  script_add(&f->scripts[0], 0, "IC", 2);
  script_add(&f->scripts[0], 100, "Y 200 OK\r\nContent-Length: 0\r\n\r\n", 31);
  fake_go(f);
  h = fetch(f, "/x", NULL, NULL);
  fake_end(f);
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(http_status(h), 200);
  http_close(h);
}
END_TEST

START_TEST(icy_with_oversized_hold_is_rejected) {
  fake_t *f = fake_new();
  net_err_reason_t reason = NET_ERR_OTHER;
  char big[9000];
  http_t *h;
  memset(big, 'a', sizeof big);
  memcpy(big, "ICY 200 OK\r\nX: ", 15);
  f->nscripts = 1;
  script_add(&f->scripts[0], 0, big, 8190);
  script_add(&f->scripts[0], 100, big, 100);
  fake_go(f);
  h = fetch(f, "/x", NULL, &reason);
  fake_end(f);
  ck_assert_ptr_null(h);
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
}
END_TEST

START_TEST(malformed_status_line_is_format_error) {
  net_err_reason_t reason = NET_ERR_OTHER;
  ck_assert_ptr_null(one_shot("garbage line\r\n\r\n", &reason));
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
}
END_TEST

START_TEST(more_headers_than_slots_are_dropped) {
  char resp[4096];
  size_t n = (size_t)snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n");
  http_t *h;
  for (int i = 0; i < 40; i++) n += (size_t)snprintf(resp + n, sizeof resp - n, "X-H%d: v\r\n", i);
  snprintf(resp + n, sizeof resp - n, "\r\n");
  h = one_shot(resp, NULL);
  ck_assert_ptr_nonnull(h);
  ck_assert_ptr_nonnull(http_header(h, "x-h0"));
  ck_assert_ptr_null(http_header(h, "x-h39"));
  http_close(h);
}
END_TEST

START_TEST(too_many_headers_for_the_parser_is_format_error) {
  char resp[4096];
  size_t n = (size_t)snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\n");
  net_err_reason_t reason = NET_ERR_OTHER;
  for (int i = 0; i < 80; i++) n += (size_t)snprintf(resp + n, sizeof resp - n, "X-H%d: v\r\n", i);
  snprintf(resp + n, sizeof resp - n, "\r\n");
  ck_assert_ptr_null(one_shot(resp, &reason));
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
}
END_TEST

START_TEST(long_header_values_are_truncated_but_long_location_fails) {
  char *resp = malloc(6000);
  size_t n;
  http_t *h;
  net_err_reason_t reason = NET_ERR_OTHER;
  ck_assert_ptr_nonnull(resp);
  n = (size_t)snprintf(resp, 6000, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nX-Long: ");
  memset(resp + n, 'v', 3000);
  n += 3000;
  snprintf(resp + n, 6000 - n, "\r\n\r\n");
  h = one_shot(resp, NULL);
  ck_assert_ptr_nonnull(h);
  ck_assert_uint_eq((unsigned)strlen(http_header(h, "x-long")), 2047u);
  http_close(h);
  n = (size_t)snprintf(resp, 6000, "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1/");
  memset(resp + n, 'p', 3000);
  n += 3000;
  snprintf(resp + n, 6000 - n, "\r\n\r\n");
  ck_assert_ptr_null(one_shot(resp, &reason));
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
  free(resp);
}
END_TEST

START_TEST(malformed_content_length_is_format_error) {
  static const char *const cl[] = {"abc", "12x", "99999999999999999999999999", "", "-5"};
  char resp[128];
  net_err_reason_t reason = NET_ERR_OTHER;
  snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nContent-Length: %s\r\n\r\n", cl[_i]);
  ck_assert_ptr_null(one_shot(resp, &reason));
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
}
END_TEST

START_TEST(unterminated_oversized_headers_are_format_error) {
  fake_t *f = fake_new();
  net_err_reason_t reason = NET_ERR_OTHER;
  char big[4096];
  http_t *h;
  memset(big, 'a', sizeof big);
  f->nscripts = 1;
  script_str(&f->scripts[0], "HTTP/1.1 200 OK\r\nX: ");
  for (int i = 0; i < 2; i++) script_add(&f->scripts[0], 0, big, sizeof big);
  f->scripts[0].nchunks = 3;
  script_add(&f->scripts[0], 0, big, 100);
  fake_go(f);
  h = fetch(f, "/x", NULL, &reason);
  fake_end(f);
  ck_assert_ptr_null(h);
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
}
END_TEST

START_TEST(close_before_headers_reports_eof) {
  net_err_reason_t reason = NET_ERR_OTHER;
  ck_assert_ptr_null(one_shot("", &reason));
  ck_assert_int_eq(reason, NET_ERR_EOF);
}
END_TEST

START_TEST(reset_before_headers_reports_read_error) {
  fake_t *f = fake_new();
  net_err_reason_t reason = NET_ERR_OTHER;
  http_t *h;
  f->nscripts = 1;
  f->scripts[0].end = END_RESET;
  fake_go(f);
  h = fetch(f, "/x", NULL, &reason);
  fake_end(f);
  ck_assert_ptr_null(h);
  ck_assert_int_eq(reason, NET_ERR_READ);
}
END_TEST

START_TEST(stalled_headers_time_out) {
  fake_t *f = fake_new();
  net_err_reason_t reason = NET_ERR_OTHER;
  http_t *h;
  f->nscripts = 1;
  script_str(&f->scripts[0], "HTTP/1.1 200 OK\r\nX-Part: ");
  f->scripts[0].end = END_HOLD;
  f->scripts[0].hold_ms = 6500;
  fake_go(f);
  h = fetch(f, "/x", NULL, &reason);
  fake_end(f);
  ck_assert_ptr_null(h);
  ck_assert_int_eq(reason, NET_ERR_TIMEOUT);
}
END_TEST

START_TEST(transfer_encoding_forms) {
  static const struct {
    const char *te;
    int ok;
  } t[] = {{"chunked", 1}, {"CHUNKED \t", 1}, {"gzip", 0}, {"gzip, chunked", 0}, {"chunkedx", 0}};
  char resp[200];
  net_err_reason_t reason = NET_ERR_OTHER;
  http_t *h;
  snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nTransfer-Encoding: %s\r\n\r\n0\r\n\r\n", t[_i].te);
  h = one_shot(resp, &reason);
  if (t[_i].ok) {
    ck_assert_ptr_nonnull(h);
    http_close(h);
  } else {
    ck_assert_ptr_null(h);
    ck_assert_int_eq(reason, NET_ERR_FORMAT);
  }
}
END_TEST

START_TEST(redirect_is_followed) {
  fake_t *f = fake_new();
  char resp[200];
  http_t *h;
  f->nscripts = 2;
  snprintf(resp, sizeof resp, "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:%u/next\r\nContent-Length: 0\r\n\r\n", f->port);
  script_str(&f->scripts[0], resp);
  script_str(&f->scripts[1], "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  fake_go(f);
  h = fetch(f, "/x", "X-Extra: 1", NULL);
  fake_end(f);
  ck_assert_ptr_nonnull(h);
  ck_assert_str_eq(http_final_url(h)->path, "/next");
  http_close(h);
}
END_TEST

START_TEST(redirect_without_location_fails) {
  net_err_reason_t reason = NET_ERR_OTHER;
  ck_assert_ptr_null(one_shot("HTTP/1.1 302 Found\r\nContent-Length: 0\r\n\r\n", &reason));
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
}
END_TEST

START_TEST(endless_redirects_end_in_http_error) {
  fake_t *f = fake_new();
  char resp[200];
  net_err_reason_t reason = NET_ERR_OTHER;
  http_t *h;
  f->nscripts = 6;
  snprintf(resp, sizeof resp, "HTTP/1.1 301 Moved\r\nLocation: http://127.0.0.1:%u/again\r\nContent-Length: 0\r\n\r\n", f->port);
  for (int i = 0; i < 6; i++) script_str(&f->scripts[i], resp);
  fake_go(f);
  h = fetch(f, "/x", NULL, &reason);
  fake_end(f);
  ck_assert_ptr_null(h);
  ck_assert_int_eq(reason, NET_ERR_HTTP);
}
END_TEST

START_TEST(not_modified_and_error_statuses) {
  net_err_reason_t reason = NET_ERR_OTHER;
  http_t *h = one_shot("HTTP/1.1 304 Not Modified\r\n\r\n", &reason);
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(http_status(h), 304);
  http_close(h);
  ck_assert_ptr_null(one_shot("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", &reason));
  ck_assert_int_eq(reason, NET_ERR_HTTP);
  ck_assert_ptr_null(one_shot("HTTP/1.1 100 Continue\r\nContent-Length: 0\r\n\r\n", &reason));
  ck_assert_int_eq(reason, NET_ERR_HTTP);
}
END_TEST

START_TEST(request_builder_forms) {
  http_url_t u;
  char buf[2048];
  int n;
  memset(&u, 0, sizeof u);
  strcpy(u.host, "example.org");
  strcpy(u.path, "/p?q=1");
  u.port = 80;
  n = build_get_request(buf, sizeof buf, &u, "agent", NULL);
  ck_assert_int_gt(n, 0);
  ck_assert_ptr_nonnull(strstr(buf, "Host: example.org\r\n"));
  u.tls = 1;
  u.port = 8443;
  n = build_get_request(buf, sizeof buf, &u, "agent", "X-A: b");
  ck_assert_ptr_nonnull(strstr(buf, "Host: example.org:8443\r\n"));
  ck_assert_ptr_nonnull(strstr(buf, "X-A: b\r\n\r\n"));
  strcpy(u.host, "::1");
  u.port = 443;
  n = build_get_request(buf, sizeof buf, &u, "agent", NULL);
  ck_assert_ptr_nonnull(strstr(buf, "Host: [::1]\r\n"));
  ck_assert_int_eq(build_get_request(buf, 20, &u, "agent", NULL), -1);
}
END_TEST

START_TEST(reuse_decision_matrix) {
  static const struct {
    int minor, status, chunked, chunk_done, has_cl;
    size_t cl, consumed;
    const char *conn;
    int want;
  } t[] = {
      {1, 200, 0, 0, 1, 10, 10, NULL, 1},
      {0, 200, 0, 0, 1, 10, 10, NULL, 0},
      {1, 200, 0, 0, 1, 10, 5, NULL, 0},
      {1, 200, 0, 0, 0, 0, 0, NULL, 0},
      {1, 200, 1, 1, 0, 0, 0, NULL, 1},
      {1, 200, 1, 0, 0, 0, 0, NULL, 0},
      {1, 304, 0, 0, 0, 0, 0, NULL, 1},
      {1, 200, 0, 0, 1, 10, 10, "close", 0},
      {1, 200, 0, 0, 1, 10, 10, "Keep-Alive", 1},
      {1, 200, 0, 0, 1, 10, 10, "keep-alive, Close", 0},
  };
  struct http h;
  memset(&h, 0, sizeof h);
  h.minor_version = t[_i].minor;
  h.status = t[_i].status;
  h.chunked = t[_i].chunked;
  h.chunk_done = t[_i].chunk_done;
  h.has_content_length = t[_i].has_cl;
  h.content_length = t[_i].cl;
  h.body_consumed = t[_i].consumed;
  if (t[_i].conn) {
    strcpy(h.hdr[0].name, "connection");
    strcpy(h.hdr[0].value, t[_i].conn);
    h.hdr_count = 1;
  }
  ck_assert_int_eq(http_can_reuse(&h), t[_i].want);
}
END_TEST

START_TEST(reset_for_reuse_keeps_connection_only) {
  struct http h;
  http_url_t u;
  memset(&h, 0, sizeof h);
  memset(&u, 0, sizeof u);
  h.fd = 42;
  h.status = 200;
  h.hdr_count = 3;
  strcpy(u.host, "next.example");
  reset_http_for_reuse(&h, &u);
  ck_assert_int_eq(h.fd, 42);
  ck_assert_int_eq(h.status, 0);
  ck_assert_int_eq(h.hdr_count, 0);
  ck_assert_str_eq(h.url.host, "next.example");
}
END_TEST

START_TEST(redirect_status_set) {
  ck_assert_int_eq(http_is_redirect_status(301), 1);
  ck_assert_int_eq(http_is_redirect_status(302), 1);
  ck_assert_int_eq(http_is_redirect_status(303), 1);
  ck_assert_int_eq(http_is_redirect_status(307), 1);
  ck_assert_int_eq(http_is_redirect_status(308), 1);
  ck_assert_int_eq(http_is_redirect_status(300), 0);
  ck_assert_int_eq(http_is_redirect_status(200), 0);
}
END_TEST

START_TEST(connect_failure_is_reported) {
  http_url_t url;
  net_err_reason_t reason = NET_ERR_OTHER;
  ck_assert_int_eq(http_url_parse("http://127.0.0.1:1/x", &url), 0);
  ck_assert_ptr_null(http_get(&url, NULL, 0, NULL, &reason));
  ck_assert_int_ne(reason, NET_ERR_OTHER);
}
END_TEST

static http_async_state_t drive(http_async_t *a, net_err_reason_t *reason) {
  http_async_state_t st = HTTP_ASYNC_PENDING;
  for (int i = 0; i < 400 && st == HTTP_ASYNC_PENDING; i++) {
    struct pollfd pfd;
    pfd.fd = http_async_poll_fd(a);
    pfd.events = http_async_poll_events(a);
    pfd.revents = 0;
    poll(&pfd, 1, 50);
    st = http_async_step(a, reason);
  }
  return st;
}

static http_async_state_t async_run(fake_t *f, const char *path, const char *extra, net_err_reason_t *reason, http_t **out) {
  char uri[128];
  http_url_t url;
  http_async_t *a;
  http_async_state_t st;
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u%s", f->port, path);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  a = http_async_start(&url, NULL, 0, extra, reason);
  ck_assert_ptr_nonnull(a);
  st = drive(a, reason);
  if (st == HTTP_ASYNC_DONE && out) *out = http_async_take(a);
  else http_async_free(a);
  return st;
}

static http_async_state_t async_one(const char *resp, net_err_reason_t *reason) {
  fake_t *f = fake_new();
  http_async_state_t st;
  f->nscripts = 1;
  script_str(&f->scripts[0], resp);
  fake_go(f);
  st = async_run(f, "/x", NULL, reason, NULL);
  fake_end(f);
  return st;
}

START_TEST(async_success_and_extra_header) {
  fake_t *f = fake_new();
  http_t *h = NULL;
  f->nscripts = 1;
  script_str(&f->scripts[0], "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  fake_go(f);
  ck_assert_int_eq(async_run(f, "/x", "X-Extra: 1", NULL, &h), HTTP_ASYNC_DONE);
  fake_end(f);
  ck_assert_int_eq(http_status(h), 200);
  http_close(h);
}
END_TEST

START_TEST(async_error_statuses_and_formats) {
  static const struct {
    const char *resp;
    http_async_state_t st;
    net_err_reason_t reason;
  } t[] = {
      {"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", HTTP_ASYNC_ERROR, NET_ERR_HTTP},
      {"HTTP/1.1 304 Not Modified\r\n\r\n", HTTP_ASYNC_DONE, NET_ERR_OTHER},
      {"HTTP/1.1 302 Found\r\nContent-Length: 0\r\n\r\n", HTTP_ASYNC_ERROR, NET_ERR_FORMAT},
      {"garbage\r\n\r\n", HTTP_ASYNC_ERROR, NET_ERR_FORMAT},
      {"HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n", HTTP_ASYNC_ERROR, NET_ERR_FORMAT},
      {"HTTP/1.1 200 OK\r\nContent-Length: zz\r\n\r\n", HTTP_ASYNC_ERROR, NET_ERR_FORMAT},
      {"", HTTP_ASYNC_ERROR, NET_ERR_EOF},
  };
  net_err_reason_t reason = NET_ERR_OTHER;
  ck_assert_int_eq(async_one(t[_i].resp, &reason), t[_i].st);
  if (t[_i].st == HTTP_ASYNC_ERROR) ck_assert_int_eq(reason, t[_i].reason);
}
END_TEST

START_TEST(async_oversized_headers_fail) {
  fake_t *f = fake_new();
  char big[4096];
  net_err_reason_t reason = NET_ERR_OTHER;
  memset(big, 'a', sizeof big);
  f->nscripts = 1;
  script_str(&f->scripts[0], "HTTP/1.1 200 OK\r\nX: ");
  script_add(&f->scripts[0], 0, big, sizeof big);
  script_add(&f->scripts[0], 0, big, sizeof big);
  script_add(&f->scripts[0], 0, big, sizeof big);
  fake_go(f);
  ck_assert_int_eq(async_run(f, "/x", NULL, &reason, NULL), HTTP_ASYNC_ERROR);
  fake_end(f);
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
}
END_TEST

START_TEST(async_redirect_chain_and_limit) {
  fake_t *f = fake_new();
  char redir[200];
  http_t *h = NULL;
  net_err_reason_t reason = NET_ERR_OTHER;
  f->nscripts = 3;
  snprintf(redir, sizeof redir, "HTTP/1.1 307 Temporary\r\nLocation: /two\r\nContent-Length: 0\r\n\r\n");
  script_str(&f->scripts[0], redir);
  script_str(&f->scripts[1], redir);
  script_str(&f->scripts[2], "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  fake_go(f);
  ck_assert_int_eq(async_run(f, "/one", NULL, &reason, &h), HTTP_ASYNC_DONE);
  fake_end(f);
  ck_assert_str_eq(http_final_url(h)->path, "/two");
  http_close(h);
  f = fake_new();
  f->nscripts = 6;
  for (int i = 0; i < 6; i++) script_str(&f->scripts[i], redir);
  fake_go(f);
  ck_assert_int_eq(async_run(f, "/one", NULL, &reason, NULL), HTTP_ASYNC_ERROR);
  fake_end(f);
  ck_assert_int_eq(reason, NET_ERR_HTTP);
}
END_TEST

START_TEST(async_connect_refused_reports_error) {
  http_url_t url;
  http_async_t *a;
  net_err_reason_t reason = NET_ERR_OTHER;
  ck_assert_int_eq(http_url_parse("http://127.0.0.1:1/x", &url), 0);
  a = http_async_start(&url, "agent", 0, NULL, &reason);
  if (a) {
    ck_assert_int_eq(drive(a, &reason), HTTP_ASYNC_ERROR);
    http_async_free(a);
  }
  ck_assert_int_ne(reason, NET_ERR_OTHER);
}
END_TEST

START_TEST(async_tls_url_without_tls_support_fails) {
  fake_t *f = fake_new();
  http_url_t url;
  http_async_t *a;
  net_err_reason_t reason = NET_ERR_OTHER;
  f->nscripts = 1;
  fake_go(f);
  ck_assert_int_eq(http_url_parse("http://127.0.0.1/x", &url), 0);
  url.tls = 1;
  url.port = f->port;
  a = http_async_start(&url, "agent", 0, NULL, &reason);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(drive(a, &reason), HTTP_ASYNC_ERROR);
  http_async_free(a);
  fake_end(f);
}
END_TEST

START_TEST(async_stage_timeout_in_every_phase) {
  static const http_async_phase_t phases[] = {HA_CONNECTING, HA_TLS_HANDSHAKE, HA_SENDING, HA_READING_HEADERS};
  http_url_t url;
  http_async_t *a;
  net_err_reason_t reason = NET_ERR_OTHER;
  ck_assert_int_eq(http_url_parse("http://127.0.0.1:1/x", &url), 0);
  a = http_async_start(&url, "agent", 0, NULL, &reason);
  ck_assert_ptr_nonnull(a);
  for (size_t i = 0; i < NELEM(phases); i++) {
    a->phase = phases[i];
    a->deadline = 0;
    reason = NET_ERR_OTHER;
    ck_assert_int_eq(http_async_step(a, &reason), HTTP_ASYNC_ERROR);
    ck_assert_int_eq(reason, NET_ERR_TIMEOUT);
  }
  http_async_free(a);
}
END_TEST

START_TEST(async_reuse_sends_on_the_kept_connection) {
  fake_t *f = fake_new();
  http_t *h1 = NULL;
  http_t *h2 = NULL;
  http_url_t url;
  http_async_t *a;
  char uri[96];
  f->nscripts = 1;
  f->scripts[0].nrequests = 2;
  script_str(&f->scripts[0], "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  script_str(&f->scripts[0], "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  f->scripts[0].req_of[1] = 1;
  fake_go(f);
  ck_assert_int_eq(async_run(f, "/one", NULL, NULL, &h1), HTTP_ASYNC_DONE);
  ck_assert_int_eq(http_can_reuse(h1), 1);
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/two", f->port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  a = http_async_start_reuse(h1, &url, NULL, 0, "X-R: 1", NULL);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(drive(a, NULL), HTTP_ASYNC_DONE);
  h2 = http_async_take(a);
  ck_assert_int_eq(http_status(h2), 200);
  http_close(h2);
  fake_end(f);
}
END_TEST

static Suite *httpclient_suite(void) {
  Suite *s = suite_create("httpclient");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 60);
  tcase_add_test(tc, icy_status_line_is_accepted);
  tcase_add_test(tc, icy_prefix_split_across_reads);
  tcase_add_test(tc, icy_with_oversized_hold_is_rejected);
  tcase_add_test(tc, malformed_status_line_is_format_error);
  tcase_add_test(tc, more_headers_than_slots_are_dropped);
  tcase_add_test(tc, too_many_headers_for_the_parser_is_format_error);
  tcase_add_test(tc, long_header_values_are_truncated_but_long_location_fails);
  tcase_add_loop_test(tc, malformed_content_length_is_format_error, 0, 5);
  tcase_add_test(tc, unterminated_oversized_headers_are_format_error);
  tcase_add_test(tc, close_before_headers_reports_eof);
  tcase_add_test(tc, reset_before_headers_reports_read_error);
  tcase_add_test(tc, stalled_headers_time_out);
  tcase_add_loop_test(tc, transfer_encoding_forms, 0, 5);
  tcase_add_test(tc, redirect_is_followed);
  tcase_add_test(tc, redirect_without_location_fails);
  tcase_add_test(tc, endless_redirects_end_in_http_error);
  tcase_add_test(tc, not_modified_and_error_statuses);
  tcase_add_test(tc, request_builder_forms);
  tcase_add_loop_test(tc, reuse_decision_matrix, 0, 10);
  tcase_add_test(tc, reset_for_reuse_keeps_connection_only);
  tcase_add_test(tc, redirect_status_set);
  tcase_add_test(tc, connect_failure_is_reported);
  tcase_add_test(tc, async_success_and_extra_header);
  tcase_add_loop_test(tc, async_error_statuses_and_formats, 0, 7);
  tcase_add_test(tc, async_oversized_headers_fail);
  tcase_add_test(tc, async_redirect_chain_and_limit);
  tcase_add_test(tc, async_connect_refused_reports_error);
  tcase_add_test(tc, async_tls_url_without_tls_support_fails);
  tcase_add_test(tc, async_stage_timeout_in_every_phase);
  tcase_add_test(tc, async_reuse_sends_on_the_kept_connection);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(httpclient_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
