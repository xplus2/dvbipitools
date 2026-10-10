/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../log_capture.h"
#include "lib/net/tls.h"
#include "lib/net/tls_server.h"

#include "dipimetrics/httpserver.h"
#include "lib/metrics/store.h"

static const char TEST_CERT_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDFzCCAf+gAwIBAgIUBRiDjgPn+MBnVrx1s66inOCkthwwDQYJKoZIhvcNAQEL\n"
    "BQAwGzEZMBcGA1UEAwwQZGlwaW1ldHJpY3MtdGVzdDAeFw0yNjA5MDYxNDMxMTla\n"
    "Fw00NjA5MDExNDMxMTlaMBsxGTAXBgNVBAMMEGRpcGltZXRyaWNzLXRlc3QwggEi\n"
    "MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC7p0HGTC9d3aX0BI7IqtQwCOC8\n"
    "Vf0yGDtaKEBvzkIQvrXP7ZGQTV5Oqv/2Cdxr9mACfTdfk4DX84dCLUw/gNKFtzMG\n"
    "M/ldKj3KbfbNIQbyVYbCTvDE3L4dnktvEwZQo0PqhtsU3+Ezb+sCMFS3V69EnRxQ\n"
    "EPCWtKEzHh8nvY4E/MUnPdMF3ApD6L1sYoLZLIxkaYyvmUnqfh+dDbjtQo4NT9Jj\n"
    "9sIy2SftX1/IZquJMMuRmNjvUR5Gt9wHUhLO34BoHClbbKolSo921TU98MxmZ69F\n"
    "M4nBej1XPV0ETsW098rxco6CrVMlR83ZQDJg86hMpV/z5i/wc2E3TX5yzRWzAgMB\n"
    "AAGjUzBRMB0GA1UdDgQWBBTq3NBvo+VQHFw6ZF6j2qZ59oAo8zAfBgNVHSMEGDAW\n"
    "gBTq3NBvo+VQHFw6ZF6j2qZ59oAo8zAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3\n"
    "DQEBCwUAA4IBAQAIlj56aGki634N7LPjKOVD8MOpNTRYzCthxEJ1GIy77OBTs4wQ\n"
    "Z6Rg3NR4v94wa8zbV09fJOKTKTR2OsFV7hmgjJgfz+XCcLSHxygy2XTmn2vlSufE\n"
    "TlnvuTNzqR2DV3i+Y/TTTv/M78bw6oxi7UtxGZz4oho97MqkgnjIbUx5gieVPCfZ\n"
    "1iLDvODLP+SOWVjALNWtQbRdrwIHb9SmZuvpHj8Ic1imgqGkTU02gBw1mH3cBz+P\n"
    "FWXBY+NfHUlvrQwIJ7exzr3f360GjBqFNjhW/T85jVxxTdRU+WKsXLMBxQsfH7FE\n"
    "7uvTHSvzrmyI842/11g7tTNduZLssR8bsjjT\n"
    "-----END CERTIFICATE-----\n";

static const char TEST_KEY_PEM[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC7p0HGTC9d3aX0\n"
    "BI7IqtQwCOC8Vf0yGDtaKEBvzkIQvrXP7ZGQTV5Oqv/2Cdxr9mACfTdfk4DX84dC\n"
    "LUw/gNKFtzMGM/ldKj3KbfbNIQbyVYbCTvDE3L4dnktvEwZQo0PqhtsU3+Ezb+sC\n"
    "MFS3V69EnRxQEPCWtKEzHh8nvY4E/MUnPdMF3ApD6L1sYoLZLIxkaYyvmUnqfh+d\n"
    "DbjtQo4NT9Jj9sIy2SftX1/IZquJMMuRmNjvUR5Gt9wHUhLO34BoHClbbKolSo92\n"
    "1TU98MxmZ69FM4nBej1XPV0ETsW098rxco6CrVMlR83ZQDJg86hMpV/z5i/wc2E3\n"
    "TX5yzRWzAgMBAAECggEAaxl+tMpujkgemzKuu2CkUfX/Lf51z818smweurEhi6UM\n"
    "52tTlORTWtrF04q1PvkSutj0bZwmARqArAhmaCgB/0cb3AJ14/Jj6dDw9wpOiKi/\n"
    "jM65I+JIr2bU7sQQ6p0D+iqVh0hvo4fQvSMQdsOYyLRSoG/KoHjfN+mNJoVNNRKD\n"
    "sr7+frPy8He9NL16B2jxumohB5OBL2w/0kJupe/sjbFWlc9Z7vhiYiApDXaj1/Qv\n"
    "1XiEAcnl6QabGaaG9/pcfnSfEr/nf8x3NpegNjEk3/p1S1QEjUdEdJNvL5vi642M\n"
    "fEe6u37HGNJh7udcANA97F5IUw4DlQMkw2/qfMuv4QKBgQDw/v/BMJ01BnIU1NNm\n"
    "L466KTvos2TtJ7GEFOj2ZpB9qm7pHlwAx1g55eGkFnpcrZN2p3oosr1rWbiuN27r\n"
    "N86jlJJVgMb2XUtNQQmAJzzUueI/bWDKJKiYQnQiClfRvSwY80bjaAu/qFzicbg/\n"
    "5MaexYrrqVniPJ/KQqIUSYg/bwKBgQDHVhSA1Yek1cv6/iYTZzUf7kco1TdlXVTI\n"
    "4L8olYslBGpzOqr94Rkpo35sWJhHo9kRuHvCXyQH/iGQM80jtnw7IjYnW4RChpEg\n"
    "9C59qmst2EYbEomSPYyPuniNfxqpj9DJfzkd+3BOv2fZIqRtL70FK7DigDfGAKeH\n"
    "QE6RJA1r/QKBgQCrOzOzK3x3oYXLQVCXCXFq2kNj2pr6Wjqp80V3VXaSo1c8scKD\n"
    "FyCburdxJDt6wCXHp8WHR0CJFu2+c0qPPE7JMZYrxF0ZzT3kvTIn6TkymISltmyQ\n"
    "FC3qSUVErn6pWrULYUdb6qB2ZATjLBPS6hUp2IgVW3Wu5o5OYrvQmFKDZwKBgBlZ\n"
    "OvY/MSanAW+Djjf9ceYDmQz6QDclrTh2TBjPG4izjQ3mMgRi8Z436kXA5myFy37T\n"
    "ZPmMu7pAeOgrjjSyag3jBdvvaVxEXIRCSP82Arcrv46FxvTP2uYUImxr82oIndZh\n"
    "1VOYu+lnsb8NBrfT0EeuDKpg/7awMp2icdtHKGXJAoGAQf2tOeatMwl8TLefeuI+\n"
    "oCKgngkmoJK7ID4TQunmxiJ5mLyFJX1A7Y8RZMmmjoLk89HCrpOf1xs+nP8/WYby\n"
    "E9WKLkBei8sL9CILV6k5beKT7JscplWp09qHF1nUQBjmxuyRU/w9Q7XCLfQrskqu\n"
    "VswJnEEQ9l/eWRi4DxUU8bA=\n"
    "-----END PRIVATE KEY-----\n";

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int connect_to(int listen_fd) {
  struct sockaddr_in sa;
  socklen_t sl = sizeof sa;
  int cfd;

  ck_assert_int_eq(getsockname(listen_fd, (struct sockaddr *)&sa, &sl), 0);
  cfd = socket(AF_INET, SOCK_STREAM, 0);
  ck_assert_int_ge(cfd, 0);
  ck_assert_int_eq(connect(cfd, (struct sockaddr *)&sa, sl), 0);
  return cfd;
}

static void write_temp_pem(const char *content, char *path) {
  int fd = mkstemp(path);
  size_t len = strlen(content);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq((int)write(fd, content, len), (int)len);
  close(fd);
}

static void set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  ck_assert_int_eq(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0);
}

static size_t recv_all(int fd, char *buf, size_t cap) {
  size_t total = 0;
  for (;;) {
    ssize_t n = recv(fd, buf + total, cap - 1 - total, 0);
    if (n <= 0)
      break;
    total += (size_t)n;
    if (total >= cap - 1)
      break;
  }
  buf[total] = '\0';
  return total;
}

/* drives hs via real poll(), same shape as main.c's loop, until cfd's peer (server)
   has closed: whatever it sent is then fully queued for a non-blocking recv_all() */
static void drive_until_closed(http_server_t *hs, store_t *st, int cfd) {
  double deadline = mono() + 2.0;
  while (mono() < deadline) {
    struct pollfd pfds[1 + HTTP_MAX_CONNS];
    struct pollfd cpfd;
    int n = 0;

    http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof *pfds), &n);
    poll(pfds, (nfds_t)n, 20);
    http_server_service(hs, pfds, n, st, mono(), 0);

    cpfd.fd = cfd;
    cpfd.events = POLLIN;
    cpfd.revents = 0;
    poll(&cpfd, 1, 0);
    if (cpfd.revents & (POLLIN | POLLHUP)) /* POLLHUP alone doesn't fire while unread data remains */
      return;
  }
  ck_abort_msg("server never closed the connection");
}

static void tls_handshake_client(http_server_t *hs, store_t *st, tls_t *client) {
  double deadline = mono() + 3.0;
  while (mono() < deadline) {
    struct pollfd pfds[1 + HTTP_MAX_CONNS];
    int n = 0;
    tls_handshake_status_t hst;

    http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof *pfds), &n);
    poll(pfds, (nfds_t)n, 20);
    http_server_service(hs, pfds, n, st, mono(), 0);

    hst = tls_handshake_step(client);
    if (hst == TLS_HANDSHAKE_DONE)
      return;
    if (hst == TLS_HANDSHAKE_ERROR)
      ck_abort_msg("client tls handshake failed");
  }
  ck_abort_msg("client tls handshake never completed");
}

static void tls_write_all(tls_t *client, const char *data, size_t len) {
  size_t off = 0;
  double deadline = mono() + 2.0;
  while (off < len && mono() < deadline) {
    ssize_t n = tls_write(client, data + off, len - off);
    if (n > 0)
      off += (size_t)n;
  }
  ck_assert_uint_eq(off, len);
}

static size_t tls_recv_all(http_server_t *hs, store_t *st, tls_t *client, char *buf, size_t cap) {
  size_t total = 0;
  double deadline = mono() + 2.0;
  while (mono() < deadline) {
    struct pollfd pfds[1 + HTTP_MAX_CONNS];
    int n = 0;
    ssize_t got;

    http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof *pfds), &n);
    poll(pfds, (nfds_t)n, 20);
    http_server_service(hs, pfds, n, st, mono(), 0);

    got = tls_read(client, buf + total, cap - 1 - total);
    if (got > 0)
      total += (size_t)got;
    else if (got < 0)
      break;
  }
  buf[total] = '\0';
  return total;
}

START_TEST(tls_get_metrics_returns_200_over_https) {
  store_t st;
  int lfd;
  int cfd;
  http_server_t *hs;
  char cert_path[] = "/tmp/dipimetrics_test_cert_XXXXXX";
  char key_path[] = "/tmp/dipimetrics_test_key_XXXXXX";
  tls_server_ctx_t *tls_ctx;
  tls_t *client;
  char buf[8192];

  write_temp_pem(TEST_CERT_PEM, cert_path);
  write_temp_pem(TEST_KEY_PEM, key_path);
  tls_ctx = tls_server_ctx_new(cert_path, key_path);
  unlink(cert_path);
  unlink(key_path);
  if (!tls_ctx)
    return;

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  ck_assert_int_ge(lfd, 0);
  hs = http_server_new(lfd, tls_ctx, "");
  ck_assert_ptr_nonnull(hs);

  cfd = connect_to(lfd);
  set_nonblock(cfd);
  client = tls_connect_start(cfd, "dipimetrics-test", 1);
  ck_assert_ptr_nonnull(client);
  tls_handshake_client(hs, &st, client);

  tls_write_all(client, "GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n", 35);
  tls_recv_all(hs, &st, client, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 200 OK") == buf);
  ck_assert(strstr(buf, "Content-Type: application/openmetrics-text") != NULL);

  tls_close(client);
  http_server_free(hs);
  close(lfd);
  tls_server_ctx_free(tls_ctx);
}
END_TEST

START_TEST(get_metrics_returns_200_and_openmetrics_body) {
  store_t st;
  int lfd;
  int cfd;
  http_server_t *hs;
  char buf[8192];

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  ck_assert_int_ge(lfd, 0);
  hs = http_server_new(lfd, NULL, "");
  ck_assert_ptr_nonnull(hs);

  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, "GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n", 35, 0), 35);

  drive_until_closed(hs, &st, cfd);
  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 200 OK") == buf);
  ck_assert(strstr(buf, "Content-Type: application/openmetrics-text") != NULL);
  ck_assert(strstr(buf, "# EOF") != NULL);

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(fragmented_request_across_multiple_writes_still_returns_200) {
  store_t st;
  int lfd;
  int cfd;
  http_server_t *hs;
  char buf[8192];
  const char req_part1[] = "GET /met";
  const char req_part2[] = "rics HTTP/1.1\r\nHost: x\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  ck_assert_int_ge(lfd, 0);
  hs = http_server_new(lfd, NULL, "");
  ck_assert_ptr_nonnull(hs);

  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req_part1, sizeof req_part1 - 1, 0), (int)sizeof req_part1 - 1);

  /* ticks on partial buffer: last_len parse skip path */
  for (int i = 0; i < 3; i++) {
    struct pollfd pfds[1 + HTTP_MAX_CONNS];
    int n = 0;
    http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof pfds[0]), &n);
    poll(pfds, (nfds_t)n, 20);
    http_server_service(hs, pfds, n, &st, mono(), 0);
  }

  ck_assert_int_eq((int)send(cfd, req_part2, sizeof req_part2 - 1, 0), (int)sizeof req_part2 - 1);

  drive_until_closed(hs, &st, cfd);
  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 200 OK") == buf);
  ck_assert(strstr(buf, "Content-Type: application/openmetrics-text") != NULL);

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(unknown_path_returns_404) {
  store_t st;
  int lfd;
  int cfd;
  http_server_t *hs;
  char buf[8192];
  const char req[] = "GET /nope HTTP/1.1\r\nHost: x\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd, NULL, "");
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, sizeof req - 1, 0), (int)sizeof req - 1);

  drive_until_closed(hs, &st, cfd);
  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 404 Not Found") == buf);

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(post_to_metrics_also_returns_404) {
  store_t st;
  int lfd;
  int cfd;
  http_server_t *hs;
  char buf[8192];
  const char req[] = "POST /metrics HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd, NULL, "");
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, sizeof req - 1, 0), (int)sizeof req - 1);

  drive_until_closed(hs, &st, cfd);
  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 404 Not Found") == buf);

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(query_string_is_stripped_before_matching) {
  store_t st;
  int lfd;
  int cfd;
  http_server_t *hs;
  char buf[8192];
  const char req[] = "GET /metrics?foo=bar HTTP/1.1\r\nHost: x\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd, NULL, "");
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, sizeof req - 1, 0), (int)sizeof req - 1);

  drive_until_closed(hs, &st, cfd);
  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 200 OK") == buf);

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(sequential_scrapes_each_get_a_correct_independent_response) {
  store_t st;
  int lfd, c1, c2;
  http_server_t *hs;
  char buf1[8192], buf2[8192];

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd, NULL, "");

  c1 = connect_to(lfd);
  ck_assert_int_gt((int)send(c1, "GET /metrics HTTP/1.1\r\n\r\n", 26, 0), 0);
  drive_until_closed(hs, &st, c1);
  recv_all(c1, buf1, sizeof buf1);

  c2 = connect_to(lfd);
  ck_assert_int_gt((int)send(c2, "GET /nope HTTP/1.1\r\n\r\n", 23, 0), 0);
  drive_until_closed(hs, &st, c2);
  recv_all(c2, buf2, sizeof buf2);

  ck_assert(strstr(buf1, "200 OK") != NULL);
  ck_assert(strstr(buf2, "404 Not Found") != NULL);

  close(c1);
  close(c2);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(request_counters_reflect_status_and_include_current_request) {
  store_t st;
  int lfd, c1, c2, c3;
  http_server_t *hs;
  char buf1[8192], buf2[8192], buf3[8192];

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd, NULL, "");

  c1 = connect_to(lfd);
  ck_assert_int_gt((int)send(c1, "GET /metrics HTTP/1.1\r\n\r\n", 26, 0), 0);
  drive_until_closed(hs, &st, c1);
  recv_all(c1, buf1, sizeof buf1);
  ck_assert(strstr(buf1, "dvbipi_metrics_http_requests_total{status=\"200\"} 1") != NULL);
  ck_assert(strstr(buf1, "dvbipi_metrics_http_requests_total{status=\"404\"} 0") != NULL);

  c2 = connect_to(lfd);
  ck_assert_int_gt((int)send(c2, "GET /nope HTTP/1.1\r\n\r\n", 23, 0), 0);
  drive_until_closed(hs, &st, c2);
  recv_all(c2, buf2, sizeof buf2);
  ck_assert_uint_eq(st.stats.http_requests_200, 1u);
  ck_assert_uint_eq(st.stats.http_requests_404, 1u);

  c3 = connect_to(lfd);
  ck_assert_int_gt((int)send(c3, "GET /metrics HTTP/1.1\r\n\r\n", 26, 0), 0);
  drive_until_closed(hs, &st, c3);
  recv_all(c3, buf3, sizeof buf3);
  ck_assert(strstr(buf3, "dvbipi_metrics_http_requests_total{status=\"200\"} 2") != NULL);
  ck_assert(strstr(buf3, "dvbipi_metrics_http_requests_total{status=\"404\"} 1") != NULL);

  close(c1);
  close(c2);
  close(c3);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(metrics_requires_auth_when_configured) {
  store_t st;
  int lfd;
  int cfd;
  http_server_t *hs;
  char buf[8192];
  const char req[] = "GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd, NULL, "Basic dXNlcjpwYXNz");
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, sizeof req - 1, 0), (int)sizeof req - 1);

  drive_until_closed(hs, &st, cfd);
  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 401 Unauthorized") == buf);
  ck_assert(strstr(buf, "WWW-Authenticate: Basic realm=\"metrics\"") != NULL);

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(metrics_with_correct_auth_returns_200) {
  store_t st;
  int lfd;
  int cfd;
  http_server_t *hs;
  char buf[8192];
  const char req[] = "GET /metrics HTTP/1.1\r\nHost: x\r\nAuthorization: Basic dXNlcjpwYXNz\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd, NULL, "Basic dXNlcjpwYXNz");
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, sizeof req - 1, 0), (int)sizeof req - 1);

  drive_until_closed(hs, &st, cfd);
  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 200 OK") == buf);

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(metrics_with_wrong_auth_returns_401) {
  store_t st;
  int lfd;
  int cfd;
  http_server_t *hs;
  char buf[8192];
  const char req[] = "GET /metrics HTTP/1.1\r\nHost: x\r\nAuthorization: Basic d3Jvbmc=\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd, NULL, "Basic dXNlcjpwYXNz");
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, sizeof req - 1, 0), (int)sizeof req - 1);

  drive_until_closed(hs, &st, cfd);
  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 401 Unauthorized") == buf);

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

#define BASIC_AUTH "Basic dXNlcjpwYXNz"
#define LONG_FIELD 300
#define OVERSIZE_LEN 9000

static size_t serve_one(const char *auth, const char *req, size_t req_len, int verbose, store_t *st, char *buf, size_t cap) {
  int lfd;
  int cfd;
  http_server_t *hs;
  double deadline = mono() + 2.0;
  size_t got;

  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  ck_assert_int_ge(lfd, 0);
  hs = http_server_new(lfd, NULL, auth);
  ck_assert_ptr_nonnull(hs);
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, req_len, 0), (int)req_len);
  while (mono() < deadline) {
    struct pollfd pfds[1 + HTTP_MAX_CONNS];
    struct pollfd cpfd;
    int n = 0;

    http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof *pfds), &n);
    poll(pfds, (nfds_t)n, 20);
    http_server_service(hs, pfds, n, st, mono(), verbose);
    cpfd.fd = cfd;
    cpfd.events = POLLIN;
    cpfd.revents = 0;
    poll(&cpfd, 1, 0);
    if (cpfd.revents & (POLLIN | POLLHUP | POLLERR)) break;
  }
  got = recv_all(cfd, buf, cap);
  close(cfd);
  http_server_free(hs);
  close(lfd);
  return got;
}

typedef struct {
  const char *header;
  const char *status;
} auth_case_t;

static const auth_case_t auth_cases[] = {
  {"", "401 Unauthorized"},
  {"Authorization: Basic dXNlcjpwYXNx\r\n", "401 Unauthorized"},
  {"Authorization: Basic dXNlcjpw\r\n", "401 Unauthorized"},
  {"Authorization: Basic dXNlcjpwYXNzeA==\r\n", "401 Unauthorized"},
  {"Authorization: Bearer dXNlcjpwYXNz\r\n", "401 Unauthorized"},
  {"Authorization:\r\n", "401 Unauthorized"},
  {"X-Authorization: Basic dXNlcjpwYXNz\r\n", "401 Unauthorized"},
  {"authorization: Basic dXNlcjpwYXNz\r\n", "200 OK"},
  {"AUTHORIZATION: Basic dXNlcjpwYXNz\r\n", "200 OK"},
};

START_TEST(metrics_auth_rejects_every_kind_of_bad_credential) {
  const auth_case_t *c = &auth_cases[_i];
  store_t st;
  char req[512];
  char buf[8192];
  int n;
  int ok = strcmp(c->status, "200 OK") == 0;

  store_init(&st);
  n = snprintf(req, sizeof req, "GET /metrics HTTP/1.1\r\nHost: x\r\n%s\r\n", c->header);
  serve_one(BASIC_AUTH, req, (size_t)n, 0, &st, buf, sizeof buf);
  ck_assert_ptr_nonnull(strstr(buf, c->status));
  ck_assert_uint_eq(st.stats.http_requests_200, ok ? 1u : 0u);
  ck_assert_uint_eq(st.stats.http_requests_404, 0u);
}
END_TEST

START_TEST(unknown_paths_need_no_credentials_even_when_auth_is_configured) {
  store_t st;
  char buf[8192];
  const char req[] = "GET /elsewhere HTTP/1.1\r\nHost: x\r\n\r\n";

  store_init(&st);
  serve_one(BASIC_AUTH, req, sizeof req - 1, 0, &st, buf, sizeof buf);
  ck_assert_ptr_nonnull(strstr(buf, "404 Not Found"));
  ck_assert_uint_eq(st.stats.http_requests_404, 1u);
}
END_TEST

START_TEST(oversized_request_is_dropped_without_a_response) {
  store_t st;
  char buf[8192];
  char *req = malloc(OVERSIZE_LEN);
  size_t got;
  int n;

  ck_assert_ptr_nonnull(req);
  store_init(&st);
  n = snprintf(req, OVERSIZE_LEN, "GET /metrics HTTP/1.1\r\nX-Pad: ");
  memset(req + n, 'a', (size_t)(OVERSIZE_LEN - n));
  got = serve_one("", req, OVERSIZE_LEN, 0, &st, buf, sizeof buf);
  ck_assert_uint_eq(got, 0u);
  ck_assert_uint_eq(st.stats.http_requests_200, 0u);
  ck_assert_uint_eq(st.stats.http_requests_404, 0u);
  free(req);
}
END_TEST

START_TEST(idle_connection_is_dropped_once_its_deadline_passes) {
  store_t st;
  int lfd;
  int cfd;
  http_server_t *hs;
  struct pollfd pfds[1 + HTTP_MAX_CONNS];
  double t0 = mono();
  char buf[8];
  int n = 0;

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd, NULL, "");
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, "GET ", 4, 0), 4);
  http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof *pfds), &n);
  poll(pfds, (nfds_t)n, 200);
  http_server_service(hs, pfds, n, &st, t0, 0);
  n = 0;
  http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof *pfds), &n);
  ck_assert_int_eq(n, 2);
  http_server_service(hs, pfds, n, &st, t0 + 1.0, 0);
  n = 0;
  http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof *pfds), &n);
  ck_assert_int_eq(n, 2);
  http_server_service(hs, pfds, n, &st, t0 + 6.0, 0);
  n = 0;
  http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof *pfds), &n);
  ck_assert_int_eq(n, 1);
  ck_assert_int_le((int)recv(cfd, buf, sizeof buf, 0), 0);
  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

static int connect_from(int listen_fd, const char *src) {
  struct sockaddr_in sa;
  struct sockaddr_in la;
  socklen_t sl = sizeof sa;
  int cfd;

  ck_assert_int_eq(getsockname(listen_fd, (struct sockaddr *)&sa, &sl), 0);
  memset(&la, 0, sizeof la);
  la.sin_family = AF_INET;
  ck_assert_int_eq(inet_pton(AF_INET, src, &la.sin_addr), 1);
  cfd = socket(AF_INET, SOCK_STREAM, 0);
  ck_assert_int_ge(cfd, 0);
  ck_assert_int_eq(bind(cfd, (struct sockaddr *)&la, sizeof la), 0);
  ck_assert_int_eq(connect(cfd, (struct sockaddr *)&sa, sl), 0);
  return cfd;
}

static void service_once(http_server_t *hs, store_t *st, int *n_open) {
  struct pollfd pfds[1 + HTTP_MAX_CONNS];
  int n = 0;
  http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof *pfds), &n);
  poll(pfds, (nfds_t)n, 200);
  http_server_service(hs, pfds, n, st, mono(), 0);
  n = 0;
  http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof *pfds), &n);
  *n_open = n - 1;
}

static int is_closed_by_server(int fd) {
  char b[8];
  return recv(fd, b, sizeof b, MSG_DONTWAIT) == 0;
}

START_TEST(idle_flood_from_one_peer_is_capped_and_scrape_still_served) {
  store_t st;
  int lfd;
  int clients[HTTP_MAX_CONNS];
  int scrape;
  int open_conns = 0;
  http_server_t *hs;
  char buf[8192];
  const char req[] = "GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd, NULL, "");
  for (int i = 0; i < HTTP_MAX_CONNS; i++) clients[i] = connect_to(lfd);
  service_once(hs, &st, &open_conns);
  ck_assert_int_eq(open_conns, HTTP_MAX_CONNS_PER_PEER);
  ck_assert(is_closed_by_server(clients[0]));
  ck_assert(!is_closed_by_server(clients[HTTP_MAX_CONNS - 1]));
  scrape = connect_to(lfd);
  ck_assert_int_eq((int)send(scrape, req, sizeof req - 1, 0), (int)sizeof req - 1);
  drive_until_closed(hs, &st, scrape);
  recv_all(scrape, buf, sizeof buf);
  ck_assert_ptr_nonnull(strstr(buf, "200 OK"));
  for (int i = 0; i < HTTP_MAX_CONNS; i++) close(clients[i]);
  close(scrape);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(full_pool_evicts_oldest_idle_connection_for_a_new_peer) {
  store_t st;
  int lfd;
  int clients[HTTP_MAX_CONNS];
  int scrape;
  int open_conns = 0;
  http_server_t *hs;
  char buf[8192];
  char src[16];
  const char req[] = "GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd, NULL, "");
  for (int i = 0; i < HTTP_MAX_CONNS; i++) {
    snprintf(src, sizeof src, "127.0.1.%d", i + 1);
    clients[i] = connect_from(lfd, src);
  }
  service_once(hs, &st, &open_conns);
  ck_assert_int_eq(open_conns, HTTP_MAX_CONNS);
  scrape = connect_from(lfd, "127.0.2.1");
  ck_assert_int_eq((int)send(scrape, req, sizeof req - 1, 0), (int)sizeof req - 1);
  drive_until_closed(hs, &st, scrape);
  recv_all(scrape, buf, sizeof buf);
  ck_assert_ptr_nonnull(strstr(buf, "200 OK"));
  ck_assert(is_closed_by_server(clients[0]));
  ck_assert(!is_closed_by_server(clients[HTTP_MAX_CONNS - 1]));
  for (int i = 0; i < HTTP_MAX_CONNS; i++) close(clients[i]);
  close(scrape);
  http_server_free(hs);
  close(lfd);
}
END_TEST

typedef struct {
  const char *request;
  const char *log_has;
} method_case_t;

static const method_case_t method_cases[] = {
  {"HEAD /metrics HTTP/1.1\r\nHost: x\r\n\r\n", "404 HEAD /metrics"},
  {"PUT /metrics HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n", "404 PUT /metrics"},
  {"DELETE / HTTP/1.1\r\nHost: x\r\n\r\n", "404 DELETE /"},
  {"GET /metrics/ HTTP/1.1\r\nHost: x\r\n\r\n", "404 GET /metrics/"},
  {"GET /METRICS HTTP/1.1\r\nHost: x\r\n\r\n", "404 GET /METRICS"},
  {"ABCDEFGHIJKLMNOPQRST /metrics HTTP/1.1\r\nHost: x\r\n\r\n", "404 ABCDEFGHIJKLMNO /metrics"},
  {"\x01\x02garbage\r\n\r\n", NULL},
};

START_TEST(other_methods_and_malformed_requests_get_404) {
  const method_case_t *c = &method_cases[_i];
  store_t st;
  char buf[8192];
  char msg[4096];

  store_init(&st);
  log_capture_begin();
  serve_one("", c->request, strlen(c->request), 1, &st, buf, sizeof buf);
  log_capture_end(msg, sizeof msg);
  ck_assert_ptr_nonnull(strstr(buf, "HTTP/1.1 404 Not Found"));
  ck_assert_uint_eq(st.stats.http_requests_404, 1u);
  if (c->log_has) ck_assert_ptr_nonnull(strstr(msg, c->log_has));
  else ck_assert_ptr_null(strstr(msg, "404 "));
}
END_TEST

START_TEST(overlong_path_is_truncated_in_the_log_and_still_gets_404) {
  store_t st;
  char buf[8192];
  char msg[4096];
  char req[LONG_FIELD + 64];
  char path[LONG_FIELD + 1];
  int n;

  memset(path, 'p', LONG_FIELD);
  path[0] = '/';
  path[LONG_FIELD] = '\0';
  n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: x\r\n\r\n", path);
  store_init(&st);
  log_capture_begin();
  serve_one("", req, (size_t)n, 1, &st, buf, sizeof buf);
  log_capture_end(msg, sizeof msg);
  ck_assert_ptr_nonnull(strstr(buf, "HTTP/1.1 404 Not Found"));
  ck_assert_ptr_nonnull(strstr(msg, "404 GET /ppp"));
  ck_assert_uint_lt(strlen(msg), (size_t)LONG_FIELD);
}
END_TEST

static Suite *httpserver_suite(void) {
  Suite *s = suite_create("dipimetrics_httpserver");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 15);
  tcase_add_test(tc, get_metrics_returns_200_and_openmetrics_body);
  tcase_add_test(tc, fragmented_request_across_multiple_writes_still_returns_200);
  tcase_add_test(tc, unknown_path_returns_404);
  tcase_add_test(tc, post_to_metrics_also_returns_404);
  tcase_add_test(tc, query_string_is_stripped_before_matching);
  tcase_add_test(tc, sequential_scrapes_each_get_a_correct_independent_response);
  tcase_add_test(tc, request_counters_reflect_status_and_include_current_request);
  tcase_add_test(tc, tls_get_metrics_returns_200_over_https);
  tcase_add_test(tc, metrics_requires_auth_when_configured);
  tcase_add_test(tc, metrics_with_correct_auth_returns_200);
  tcase_add_test(tc, metrics_with_wrong_auth_returns_401);
  tcase_add_loop_test(tc, metrics_auth_rejects_every_kind_of_bad_credential, 0, (int)(sizeof auth_cases / sizeof auth_cases[0]));
  tcase_add_test(tc, unknown_paths_need_no_credentials_even_when_auth_is_configured);
  tcase_add_test(tc, oversized_request_is_dropped_without_a_response);
  tcase_add_test(tc, idle_connection_is_dropped_once_its_deadline_passes);
  tcase_add_test(tc, idle_flood_from_one_peer_is_capped_and_scrape_still_served);
  tcase_add_test(tc, full_pool_evicts_oldest_idle_connection_for_a_new_peer);
  tcase_add_loop_test(tc, other_methods_and_malformed_requests_get_404, 0, (int)(sizeof method_cases / sizeof method_cases[0]));
  tcase_add_test(tc, overlong_path_is_truncated_in_the_log_and_still_gets_404);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(httpserver_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
