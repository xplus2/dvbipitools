/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "lib/net/httpclient/httpclient.h"
#include "lib/net/httpclient/priv.h"

#define REQUEST_MAX 4096
#define RESPONSE_MAX 2048
#define SERVER_POLL_MS 20
#define DRIVE_ITERATIONS 400

typedef size_t (*route_fn)(const char *path, unsigned port, const void *ctx, char *out, size_t cap);

typedef struct {
  int listen_fd;
  unsigned port;
  route_fn route;
  const void *ctx;
  atomic_int stop;
  atomic_uint requests;
  pthread_t thread;
} server_t;

static void request_path(const char *req, char *path, size_t cap) {
  const char *start = strchr(req, ' ');
  const char *end;
  size_t n = 0;

  path[0] = '\0';
  if (!start) return;
  start++;
  end = strchr(start, ' ');
  if (!end) return;
  n = (size_t)(end - start);
  if (n >= cap) n = cap - 1;
  memcpy(path, start, n);
  path[n] = '\0';
}

static void serve_connection(server_t *s, int cfd) {
  struct timeval tv = {2, 0};
  char req[REQUEST_MAX];
  char path[256];
  char resp[RESPONSE_MAX];
  size_t got = 0;
  size_t n;

  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (;;) {
    ssize_t r = recv(cfd, req + got, sizeof req - 1 - got, 0);

    if (r <= 0) break;
    got += (size_t)r;
    if (got >= 4 && memcmp(req + got - 4, "\r\n\r\n", 4) == 0) break;
  }
  req[got] = '\0';
  request_path(req, path, sizeof path);
  n = s->route(path, s->port, s->ctx, resp, sizeof resp);
  atomic_fetch_add(&s->requests, 1);
  if (n) send(cfd, resp, n, MSG_NOSIGNAL);
  close(cfd);
}

static void *server_main(void *arg) {
  server_t *s = arg;

  while (!atomic_load(&s->stop)) {
    struct pollfd pfd = {s->listen_fd, POLLIN, 0};

    if (poll(&pfd, 1, SERVER_POLL_MS) > 0) {
      int cfd = accept(s->listen_fd, NULL, NULL);

      if (cfd >= 0) serve_connection(s, cfd);
    }
  }
  return NULL;
}

static void server_start(server_t *s, route_fn route, const void *ctx) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;

  memset(s, 0, sizeof *s);
  s->route = route;
  s->ctx = ctx;
  s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  ck_assert_int_ge(s->listen_fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(s->listen_fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(s->listen_fd, 8), 0);
  ck_assert_int_eq(getsockname(s->listen_fd, (struct sockaddr *)&addr, &alen), 0);
  s->port = ntohs(addr.sin_port);
  ck_assert_int_eq(pthread_create(&s->thread, NULL, server_main, s), 0);
}

static void server_stop(server_t *s) {
  atomic_store(&s->stop, 1);
  pthread_join(s->thread, NULL);
  close(s->listen_fd);
}

static size_t redirect_response(char *out, size_t cap, const char *location) {
  int n;

  if (location) n = snprintf(out, cap, "HTTP/1.1 302 Found\r\nLocation: %s\r\nConnection: close\r\n\r\n", location);
  else n = snprintf(out, cap, "HTTP/1.1 302 Found\r\nConnection: close\r\n\r\n");
  ck_assert_int_gt(n, 0);
  ck_assert_uint_lt((size_t)n, cap);
  return (size_t)n;
}

static size_t ok_response(char *out, size_t cap) {
  int n = snprintf(out, cap, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK");

  ck_assert_int_gt(n, 0);
  return (size_t)n;
}

static size_t route_cycle(const char *path, unsigned port, const void *ctx, char *out, size_t cap) {
  char location[128];

  (void)ctx;
  if (strcmp(path, "/a") == 0) {
    snprintf(location, sizeof location, "http://127.0.0.1:%u/b", port);
    return redirect_response(out, cap, location);
  }
  return redirect_response(out, cap, "/a");
}

static size_t route_chain(const char *path, unsigned port, const void *ctx, char *out, size_t cap) {
  const unsigned *hops = ctx;
  unsigned n = 0;
  char location[128];

  (void)port;
  if (sscanf(path, "/h%u", &n) != 1) return redirect_response(out, cap, NULL);
  if (n >= *hops) return ok_response(out, cap);
  snprintf(location, sizeof location, "/h%u", n + 1);
  return redirect_response(out, cap, location);
}

static size_t route_fixed_location(const char *path, unsigned port, const void *ctx, char *out, size_t cap) {
  (void)path;
  (void)port;
  return redirect_response(out, cap, ctx);
}

static size_t route_no_location(const char *path, unsigned port, const void *ctx, char *out, size_t cap) {
  (void)path;
  (void)port;
  (void)ctx;
  return redirect_response(out, cap, NULL);
}

static void make_url(http_url_t *url, unsigned port, const char *path) {
  char uri[128];

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u%s", port, path);
  ck_assert_int_eq(http_url_parse(uri, url), 0);
}

static http_async_state_t drive_async(http_async_t *a, net_err_reason_t *reason) {
  http_async_state_t st = HTTP_ASYNC_PENDING;

  for (int i = 0; i < DRIVE_ITERATIONS && st == HTTP_ASYNC_PENDING; i++) {
    struct pollfd pfd;

    pfd.fd = http_async_poll_fd(a);
    pfd.events = http_async_poll_events(a);
    pfd.revents = 0;
    poll(&pfd, 1, 50);
    st = http_async_step(a, reason);
  }
  return st;
}

START_TEST(redirect_cycle_stops_at_redirect_limit) {
  server_t s;
  http_url_t url;
  net_err_reason_t reason = NET_ERR_COUNT;

  server_start(&s, route_cycle, NULL);
  make_url(&url, s.port, "/a");
  ck_assert_ptr_null(http_get(&url, "test-agent", 0, NULL, &reason));
  ck_assert_int_eq(reason, NET_ERR_HTTP);
  ck_assert_uint_eq(atomic_load(&s.requests), (unsigned)HTTP_REDIRECT_MAX + 1u);
  server_stop(&s);
}
END_TEST

START_TEST(async_redirect_cycle_stops_at_redirect_limit) {
  server_t s;
  http_url_t url;
  http_async_t *a;
  net_err_reason_t reason = NET_ERR_COUNT;

  server_start(&s, route_cycle, NULL);
  make_url(&url, s.port, "/a");
  a = http_async_start(&url, "test-agent", 0, NULL, NULL);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(drive_async(a, &reason), HTTP_ASYNC_ERROR);
  ck_assert_int_eq(reason, NET_ERR_HTTP);
  ck_assert_uint_eq(atomic_load(&s.requests), (unsigned)HTTP_REDIRECT_MAX + 1u);
  http_async_free(a);
  server_stop(&s);
}
END_TEST

static const unsigned chain_hops[] = {0, 1, HTTP_REDIRECT_MAX, HTTP_REDIRECT_MAX + 1, HTTP_REDIRECT_MAX + 2};

START_TEST(redirect_chain_follows_up_to_limit_only) {
  unsigned hops = chain_hops[_i];
  server_t s;
  http_url_t url;
  http_t *h;
  net_err_reason_t reason = NET_ERR_COUNT;
  char want_path[32];

  server_start(&s, route_chain, &hops);
  make_url(&url, s.port, "/h0");
  h = http_get(&url, "test-agent", 0, NULL, &reason);
  if (hops <= HTTP_REDIRECT_MAX) {
    ck_assert_ptr_nonnull(h);
    ck_assert_int_eq(http_status(h), 200);
    snprintf(want_path, sizeof want_path, "/h%u", hops);
    ck_assert_str_eq(http_final_url(h)->path, want_path);
    ck_assert_uint_eq(atomic_load(&s.requests), hops + 1u);
    http_close(h);
  } else {
    ck_assert_ptr_null(h);
    ck_assert_int_eq(reason, NET_ERR_HTTP);
    ck_assert_uint_eq(atomic_load(&s.requests), (unsigned)HTTP_REDIRECT_MAX + 1u);
  }
  server_stop(&s);
}
END_TEST

START_TEST(async_redirect_chain_follows_up_to_limit_only) {
  unsigned hops = chain_hops[_i];
  server_t s;
  http_url_t url;
  http_async_t *a;
  http_t *h;
  net_err_reason_t reason = NET_ERR_COUNT;
  http_async_state_t st;

  server_start(&s, route_chain, &hops);
  make_url(&url, s.port, "/h0");
  a = http_async_start(&url, "test-agent", 0, NULL, NULL);
  ck_assert_ptr_nonnull(a);
  st = drive_async(a, &reason);
  if (hops <= HTTP_REDIRECT_MAX) {
    ck_assert_int_eq(st, HTTP_ASYNC_DONE);
    h = http_async_take(a);
    ck_assert_int_eq(http_status(h), 200);
    ck_assert_uint_eq(atomic_load(&s.requests), hops + 1u);
    http_close(h);
  } else {
    ck_assert_int_eq(st, HTTP_ASYNC_ERROR);
    ck_assert_int_eq(reason, NET_ERR_HTTP);
    ck_assert_uint_eq(atomic_load(&s.requests), (unsigned)HTTP_REDIRECT_MAX + 1u);
    http_async_free(a);
  }
  server_stop(&s);
}
END_TEST

static const char *const bad_locations[] = {
    "",
    "ftp://127.0.0.1/x",
    "relative/path",
    "http://",
    "http://127.0.0.1:0/x",
    "https://",
};

START_TEST(redirect_with_unusable_location_fails_on_first_hop) {
  server_t s;
  http_url_t url;
  net_err_reason_t reason = NET_ERR_COUNT;

  server_start(&s, route_fixed_location, bad_locations[_i]);
  make_url(&url, s.port, "/start");
  ck_assert_ptr_null(http_get(&url, "test-agent", 0, NULL, &reason));
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
  ck_assert_uint_eq(atomic_load(&s.requests), 1u);
  server_stop(&s);
}
END_TEST

START_TEST(redirect_without_location_fails_on_first_hop) {
  server_t s;
  http_url_t url;
  net_err_reason_t reason = NET_ERR_COUNT;

  server_start(&s, route_no_location, NULL);
  make_url(&url, s.port, "/start");
  ck_assert_ptr_null(http_get(&url, "test-agent", 0, NULL, &reason));
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
  ck_assert_uint_eq(atomic_load(&s.requests), 1u);
  server_stop(&s);
}
END_TEST

START_TEST(async_redirect_with_unusable_location_fails_on_first_hop) {
  server_t s;
  http_url_t url;
  http_async_t *a;
  net_err_reason_t reason = NET_ERR_COUNT;

  server_start(&s, route_fixed_location, bad_locations[_i]);
  make_url(&url, s.port, "/start");
  a = http_async_start(&url, "test-agent", 0, NULL, NULL);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(drive_async(a, &reason), HTTP_ASYNC_ERROR);
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
  ck_assert_uint_eq(atomic_load(&s.requests), 1u);
  http_async_free(a);
  server_stop(&s);
}
END_TEST

static Suite *redirect_suite(void) {
  Suite *s = suite_create("httpclient_redirect");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 20);
  tcase_add_test(tc, redirect_cycle_stops_at_redirect_limit);
  tcase_add_test(tc, async_redirect_cycle_stops_at_redirect_limit);
  tcase_add_loop_test(tc, redirect_chain_follows_up_to_limit_only, 0, (int)(sizeof chain_hops / sizeof chain_hops[0]));
  tcase_add_loop_test(tc, async_redirect_chain_follows_up_to_limit_only, 0, (int)(sizeof chain_hops / sizeof chain_hops[0]));
  tcase_add_loop_test(tc, redirect_with_unusable_location_fails_on_first_hop, 0, (int)(sizeof bad_locations / sizeof bad_locations[0]));
  tcase_add_test(tc, redirect_without_location_fails_on_first_hop);
  tcase_add_loop_test(tc, async_redirect_with_unusable_location_fails_on_first_hop, 0, (int)(sizeof bad_locations / sizeof bad_locations[0]));
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(redirect_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
