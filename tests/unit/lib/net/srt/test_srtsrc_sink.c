/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <srt/srt.h>

#include "lib/net/srt/srtin.h"
#include "lib/net/srt/srtsink.h"
#include "lib/net/srt/srtsrc.h"

#define PAYLOAD_CHUNKS 5
#define PAYLOAD_CHUNK_BYTES 1316
#define LINK_DEADLINE_S 10.0

static double now_seconds(void) {
  struct timespec ts;

  ck_assert_int_eq(clock_gettime(CLOCK_MONOTONIC, &ts), 0);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static unsigned free_udp_port(void) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

typedef struct {
  const char *passphrase;
  int pbkeylen;
  const char *streamid;
  unsigned latency_ms;
} link_case_t;

static const link_case_t link_cases[] = {
    {NULL, 0, NULL, 0},
    {"0123456789abcdef", 16, "stream-x", 120},
};

START_TEST(loopback_link_delivers_payload_in_order) {
  const link_case_t *c = &link_cases[_i];
  const size_t total = (size_t)PAYLOAD_CHUNKS * PAYLOAD_CHUNK_BYTES;
  unsigned port = free_udp_port();
  srtsrc_cfg_t sc;
  srtsink_cfg_t kc;
  srtsrc_t *src;
  srtsink_t *sink;
  srtsink_status_t st;
  unsigned char *payload = malloc(total);
  unsigned char *rx = malloc(total);
  size_t got = 0;
  int sent = 0;
  double deadline = now_seconds() + LINK_DEADLINE_S;

  ck_assert_ptr_nonnull(payload);
  ck_assert_ptr_nonnull(rx);
  for (size_t i = 0; i < total; i++) payload[i] = (unsigned char)(i * 13 + 7);

  memset(&sc, 0, sizeof sc);
  sc.host = "127.0.0.1";
  sc.port = port;
  sc.listen = 1;
  sc.passphrase = c->passphrase;
  sc.pbkeylen = c->pbkeylen;
  sc.streamid = c->streamid;
  sc.latency_ms = c->latency_ms;
  src = srtsrc_open(&sc);
  ck_assert_ptr_nonnull(src);

  memset(&kc, 0, sizeof kc);
  kc.peers[0].host = "127.0.0.1";
  kc.peers[0].port = port;
  kc.npeers = 1;
  kc.passphrase = c->passphrase;
  kc.pbkeylen = c->pbkeylen;
  kc.streamid = c->streamid;
  kc.latency_ms = c->latency_ms;
  sink = srtsink_open(&kc);
  ck_assert_ptr_nonnull(sink);

  while (got < total && now_seconds() < deadline) {
    struct pollfd pfd;

    memset(&st, 0, sizeof st);
    srtsink_service(sink, &st);
    if (st.connected && !sent) {
      srtsink_write(sink, payload, total);
      sent = 1;
    }
    pfd.fd = srtsrc_fd(src);
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, 20) > 0) {
      ssize_t n = read(pfd.fd, rx + got, total - got);

      if (n <= 0) break;
      got += (size_t)n;
    }
  }
  ck_assert_uint_eq(got, total);
  ck_assert_mem_eq(rx, payload, total);

  srtsrc_close(src);
  srtsink_close(sink);
  free(payload);
  free(rx);
}
END_TEST

typedef struct {
  SRTSOCKET lsn;
  const unsigned char *payload;
  size_t len;
  atomic_int go;
} listener_arg_t;

static void *listener_thread(void *arg) {
  listener_arg_t *a = arg;
  struct sockaddr_storage peer;
  int plen = sizeof peer;
  SRTSOCKET c = srt_accept(a->lsn, (struct sockaddr *)&peer, &plen);

  if (c == SRT_INVALID_SOCK) return NULL;
  while (!atomic_load(&a->go)) usleep(1000);
  for (size_t off = 0; off < a->len; off += PAYLOAD_CHUNK_BYTES) srt_sendmsg2(c, (const char *)a->payload + off, PAYLOAD_CHUNK_BYTES, NULL);
  for (int i = 0; i < 500 && srt_getsockstate(c) == SRTS_CONNECTED; i++) usleep(10000);
  srt_close(c);
  return NULL;
}

START_TEST(srtin_single_caller_receives_from_listener) {
  const size_t total = (size_t)PAYLOAD_CHUNKS * PAYLOAD_CHUNK_BYTES;
  unsigned port = free_udp_port();
  unsigned char *payload = malloc(total);
  unsigned char rx[2048];
  unsigned char *got_buf = malloc(total);
  struct sockaddr_in addr;
  listener_arg_t la;
  pthread_t th;
  srtin_cfg_t cfg;
  srtin_t *in;
  size_t got = 0;
  double deadline = now_seconds() + LINK_DEADLINE_S;

  ck_assert_ptr_nonnull(payload);
  ck_assert_ptr_nonnull(got_buf);
  for (size_t i = 0; i < total; i++) payload[i] = (unsigned char)(i * 11 + 5);
  ck_assert_int_ne(srt_startup(), SRT_ERROR);
  la.lsn = srt_create_socket();
  ck_assert_int_ne(la.lsn, SRT_INVALID_SOCK);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((unsigned short)port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_ne(srt_bind(la.lsn, (struct sockaddr *)&addr, sizeof addr), SRT_ERROR);
  ck_assert_int_ne(srt_listen(la.lsn, 1), SRT_ERROR);
  la.payload = payload;
  la.len = total;
  atomic_init(&la.go, 0);
  ck_assert_int_eq(pthread_create(&th, NULL, listener_thread, &la), 0);

  memset(&cfg, 0, sizeof cfg);
  cfg.peers[0].host = "127.0.0.1";
  cfg.peers[0].port = port;
  cfg.npeers = 1;
  in = srtin_open(&cfg);
  atomic_store(&la.go, 1);
  ck_assert_ptr_nonnull(in);
  while (got < total && now_seconds() < deadline) {
    int reconnected = 0;
    int n = srtin_read(in, rx, sizeof rx, &reconnected);

    ck_assert_int_ge(n, 0);
    ck_assert_int_eq(reconnected, 0);
    if (n > 0) {
      ck_assert_uint_le(got + (size_t)n, total);
      memcpy(got_buf + got, rx, (size_t)n);
      got += (size_t)n;
    }
  }
  ck_assert_uint_eq(got, total);
  ck_assert_mem_eq(got_buf, payload, total);
  srtin_close(in);
  pthread_join(th, NULL);
  srt_close(la.lsn);
  srt_cleanup();
  free(payload);
  free(got_buf);
}
END_TEST

START_TEST(srtin_rendezvous_without_local_address_fails) {
  srtin_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.peers[0].host = "127.0.0.1";
  cfg.peers[0].port = free_udp_port();
  cfg.npeers = 1;
  cfg.rendezvous = 1;
  ck_assert_ptr_null(srtin_open(&cfg));
}
END_TEST

START_TEST(close_before_any_peer_connects_returns_promptly) {
  srtsrc_cfg_t sc;
  srtsrc_t *src;
  double start;

  memset(&sc, 0, sizeof sc);
  sc.host = "127.0.0.1";
  sc.port = free_udp_port();
  sc.listen = 1;
  src = srtsrc_open(&sc);
  ck_assert_ptr_nonnull(src);
  usleep(300000);
  start = now_seconds();
  srtsrc_close(src);
  ck_assert_msg(now_seconds() - start < 2.0, "close waited for a peer");
}
END_TEST

START_TEST(srtsrc_open_rejects_bad_config) {
  char big[300];
  srtsrc_cfg_t sc;

  memset(big, 'a', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  memset(&sc, 0, sizeof sc);
  sc.host = "127.0.0.1";
  sc.port = 9;
  switch (_i) {
    case 0:
      sc.host = NULL;
      break;
    case 1:
      sc.port = 0;
      break;
    case 2:
      sc.host = big + sizeof big - 1 - 64;
      break;
    case 3:
      sc.passphrase = big + sizeof big - 1 - 128;
      break;
    case 4:
      sc.streamid = big + sizeof big - 1 - 128;
      break;
    default:
      sc.packetfilter = big + sizeof big - 1 - 256;
      break;
  }
  ck_assert_ptr_null(srtsrc_open(&sc));
}
END_TEST

START_TEST(srtsink_open_rejects_bad_peer_counts) {
  static const int counts[] = {0, -1, SRTSINK_MAX_PEERS + 1};
  srtsink_cfg_t kc;

  memset(&kc, 0, sizeof kc);
  kc.npeers = counts[_i];
  ck_assert_ptr_null(srtsink_open(&kc));
}
END_TEST

static int queue_ms_value(srtsink_t *sink, uint64_t *out) {
  srtsink_t *sinks[1] = {sink};
  srtsink_queue_ctx_t q = {sinks, 1, SRT_QUEUE_METRICS_BASIC};
  metrics_writer_t w;
  metrics_hdr_t hdr;
  metrics_reader_t r;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t v;
  int found = 0;

  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = METRICS_COMPONENT_TVHEAD;
  hdr.metrics_id[0] = 'x';
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  srtsink_put_queue_metrics(&w, &q);
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
  while (metrics_reader_next(&r, &id, label, sizeof label, &v) == 1) {
    if (id != METRICS_ID_SRT_SENDER_QUEUE_MILLISECONDS) continue;
    *out = v;
    found = 1;
  }
  return found;
}

START_TEST(queue_ms_needs_bitrate_then_follows_queued_bytes) {
  srtsink_cfg_t kc;
  srtsink_t *sink;
  srtsink_status_t st;
  unsigned char chunk[PAYLOAD_CHUNK_BYTES];
  uint64_t ms = 0;
  double start;

  memset(chunk, 0x47, sizeof chunk);
  memset(&kc, 0, sizeof kc);
  kc.peers[0].host = "127.0.0.1";
  kc.peers[0].port = free_udp_port();
  kc.npeers = 1;
  kc.queue_metrics = SRT_QUEUE_METRICS_BASIC;
  sink = srtsink_open(&kc);
  ck_assert_ptr_nonnull(sink);

  srtsink_service(sink, &st);
  for (int i = 0; i < 50; i++) srtsink_write(sink, chunk, sizeof chunk);
  ck_assert(!queue_ms_value(sink, &ms));
  start = now_seconds();
  while (now_seconds() - start < 1.2) {
    srtsink_service(sink, &st);
    usleep(20000);
  }
  ck_assert(queue_ms_value(sink, &ms));
  ck_assert_uint_ge(ms, 800u);
  ck_assert_uint_le(ms, 1300u);
  srtsink_close(sink);
}
END_TEST

START_TEST(queue_ms_with_connected_peer_stays_small_while_streaming) {
  unsigned port = free_udp_port();
  srtsrc_cfg_t sc;
  srtsink_cfg_t kc;
  srtsrc_t *src;
  srtsink_t *sink;
  srtsink_status_t st;
  unsigned char chunk[PAYLOAD_CHUNK_BYTES];
  unsigned char rx[4 * PAYLOAD_CHUNK_BYTES];
  uint64_t ms = 0;
  double deadline = now_seconds() + LINK_DEADLINE_S;
  double stream_start = 0.0;
  size_t sent = 0;

  memset(chunk, 0x47, sizeof chunk);
  memset(&sc, 0, sizeof sc);
  sc.host = "127.0.0.1";
  sc.port = port;
  sc.listen = 1;
  src = srtsrc_open(&sc);
  ck_assert_ptr_nonnull(src);
  memset(&kc, 0, sizeof kc);
  kc.peers[0].host = "127.0.0.1";
  kc.peers[0].port = port;
  kc.npeers = 1;
  kc.queue_metrics = SRT_QUEUE_METRICS_BASIC;
  sink = srtsink_open(&kc);
  ck_assert_ptr_nonnull(sink);

  while (now_seconds() < deadline) {
    struct pollfd pfd;
    double now = now_seconds();

    memset(&st, 0, sizeof st);
    srtsink_service(sink, &st);
    if (st.connected && stream_start == 0.0) stream_start = now;
    if (stream_start != 0.0) {
      if (now - stream_start >= 2.5) break;
      while ((double)sent * 0.01 <= now - stream_start) {
        srtsink_write(sink, chunk, sizeof chunk);
        sent++;
      }
    }
    pfd.fd = srtsrc_fd(src);
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, 5) > 0 && read(pfd.fd, rx, sizeof rx) < 0) break;
  }
  ck_assert_msg(stream_start != 0.0, "link never came up");
  ck_assert(queue_ms_value(sink, &ms));
  ck_assert_uint_le(ms, 200u);
  srtsrc_close(src);
  srtsink_close(sink);
}
END_TEST

static Suite *srtsrc_sink_suite(void) {
  Suite *s = suite_create("srtsrc_sink");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_loop_test(tc, loopback_link_delivers_payload_in_order, 0, (int)(sizeof link_cases / sizeof link_cases[0]));
  tcase_add_test(tc, close_before_any_peer_connects_returns_promptly);
  tcase_add_loop_test(tc, srtsrc_open_rejects_bad_config, 0, 6);
  tcase_add_loop_test(tc, srtsink_open_rejects_bad_peer_counts, 0, 3);
  tcase_add_test(tc, queue_ms_needs_bitrate_then_follows_queued_bytes);
  tcase_add_test(tc, queue_ms_with_connected_peer_stays_small_while_streaming);
  suite_add_tcase(s, tc);
  tc = tcase_create("srtin");
  tcase_add_test(tc, srtin_single_caller_receives_from_listener);
  tcase_add_test(tc, srtin_rendezvous_without_local_address_fails);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(srtsrc_sink_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
