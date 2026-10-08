/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "lib/mux/amf.h"
#include "lib/net/rtmp/chunk.h"
#include "lib/net/rtmp/handshake.h"
#include "lib/net/rtmp/rtmpout.h"

#define RTMP_TYPE_INVOKE 20
#define SRV_QUIET_MS 300

static int make_listener(unsigned *port_out, int rcvbuf) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;

  ck_assert_int_ge(fd, 0);
  if (rcvbuf)
    ck_assert_int_eq(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf), 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 1), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

/* discards until quiet, content already verified by test_rtmp.c */
static void recv_until_quiet(int fd) {
  struct timeval tv = {0, SRV_QUIET_MS * 1000};
  unsigned char buf[8192];
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (;;)
    if (recv(fd, buf, sizeof buf, 0) <= 0)
      return;
}

static size_t build_chunk(unsigned char *out, uint32_t cid, unsigned char type, uint32_t stream_id, const unsigned char *payload, size_t len) {
  rtmp_chunk_header_t h;
  size_t n;
  h.fmt = RTMP_CHUNK_FMT_0;
  h.cid = cid;
  h.timestamp = 0;
  h.length = (uint32_t)len;
  h.type = type;
  h.stream_id = stream_id;
  n = rtmp_chunk_basic_header_write(out, h.fmt, h.cid);
  n += rtmp_chunk_message_header_write(out + n, &h);
  memcpy(out + n, payload, len);
  return n + len;
}

static size_t build_result_connect(unsigned char *out) {
  ebuf_t b;
  size_t n;
  memset(&b, 0, sizeof b);
  amf_string(&b, "_result");
  amf_number(&b, 1);
  amf_object_start(&b);
  amf_object_end(&b);
  amf_object_start(&b);
  amf_object_end(&b);
  n = build_chunk(out, 3, RTMP_TYPE_INVOKE, 0, b.p, b.len);
  ebuf_free(&b);
  return n;
}

static size_t build_result_create_stream(unsigned char *out, double stream_id) {
  ebuf_t b;
  size_t n;
  memset(&b, 0, sizeof b);
  amf_string(&b, "_result");
  amf_number(&b, 2);
  amf_null(&b);
  amf_number(&b, stream_id);
  n = build_chunk(out, 3, RTMP_TYPE_INVOKE, 0, b.p, b.len);
  ebuf_free(&b);
  return n;
}

typedef struct {
  int listen_fd;
  unsigned char captured[4096];
  size_t captured_len;
} fake_server_t;

static int accept_until_ready(int listen_fd) {
  int cfd = accept(listen_fd, NULL, NULL);
  unsigned char s0s1s2[1 + 2 * RTMP_HANDSHAKE_SIZE];
  unsigned char msg[512];
  size_t n;

  if (cfd < 0)
    return -1;

  recv_until_quiet(cfd); /* C0+C1 */
  s0s1s2[0] = RTMP_VERSION;
  memset(s0s1s2 + 1, 0x42, 2 * RTMP_HANDSHAKE_SIZE); /* arbitrary: simple handshake never validates this */
  send(cfd, s0s1s2, sizeof s0s1s2, 0);

  recv_until_quiet(cfd); /* C2 + connect */
  n = build_result_connect(msg);
  send(cfd, msg, n, 0);

  recv_until_quiet(cfd); /* releaseStream + FCPublish + createStream */
  n = build_result_create_stream(msg, 9.0);
  send(cfd, msg, n, 0);
  return cfd;
}

static void *fake_server_thread(void *arg) {
  fake_server_t *s = arg;
  int cfd = accept_until_ready(s->listen_fd);
  struct timeval tv = {1, 0};

  if (cfd < 0)
    return NULL;

  /* no drain here: publish + first tag can arrive too close to split cleanly */
  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (;;) {
    ssize_t r = recv(cfd, s->captured + s->captured_len, sizeof s->captured - s->captured_len, 0);
    if (r <= 0)
      break;
    s->captured_len += (size_t)r;
    if (s->captured_len >= sizeof s->captured)
      break;
  }

  close(cfd);
  return NULL;
}

typedef struct {
  int listen_fd;
  atomic_int drain;
  atomic_int found_end;
} stall_server_t;

static void *stall_server_thread(void *arg) {
  stall_server_t *s = arg;
  int cfd = accept_until_ready(s->listen_fd);
  struct timeval tv = {1, 0};
  unsigned char buf[65536 + 2];
  size_t keep = 0;

  if (cfd < 0)
    return NULL;

  while (!atomic_load(&s->drain)) {
    struct timespec ts = {0, 5000000L};
    nanosleep(&ts, NULL);
  }
  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (;;) {
    ssize_t r = recv(cfd, buf + keep, sizeof buf - keep, 0);
    size_t have;
    if (r <= 0)
      break;
    have = keep + (size_t)r;
    if (memmem(buf, have, "END", 3)) {
      atomic_store(&s->found_end, 1);
      break;
    }
    keep = have < 2 ? have : 2;
    memmove(buf, buf + have - keep, keep);
  }
  close(cfd);
  return NULL;
}

/* like fake_server_thread but replies only to the handshake, then captures
   whatever the client sends next (C2 + connect) instead of driving to ready */
static void *capture_connect_thread(void *arg) {
  fake_server_t *s = arg;
  int cfd = accept(s->listen_fd, NULL, NULL);
  unsigned char s0s1s2[1 + 2 * RTMP_HANDSHAKE_SIZE];
  struct timeval tv = {1, 0};

  if (cfd < 0)
    return NULL;

  recv_until_quiet(cfd); /* C0+C1 */
  s0s1s2[0] = RTMP_VERSION;
  memset(s0s1s2 + 1, 0x42, 2 * RTMP_HANDSHAKE_SIZE);
  send(cfd, s0s1s2, sizeof s0s1s2, 0);

  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (;;) {
    ssize_t r = recv(cfd, s->captured + s->captured_len, sizeof s->captured - s->captured_len, 0);
    if (r <= 0)
      break;
    s->captured_len += (size_t)r;
    if (s->captured_len >= sizeof s->captured)
      break;
  }
  close(cfd);
  return NULL;
}

static int drive_until_sent(rtmpout_t *o, flv_tag_type_t type, const unsigned char *data, size_t len, int max_iters) {
  for (int i = 0; i < max_iters; i++) {
    if (0 == rtmpout_write(o, type, 0, data, len, NULL, 0))
      return 1;
    struct timespec ts = {0, 5000000L};
    nanosleep(&ts, NULL);
  }
  return 0;
}

START_TEST(rtmpout_open_rejects_malformed_urls) {
  rtmpout_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);

  cfg.url = "http://host/app/key";
  ck_assert_ptr_null(rtmpout_open(&cfg));

  cfg.url = "rtmp://";
  ck_assert_ptr_null(rtmpout_open(&cfg));

  cfg.url = "rtmp://host";
  ck_assert_ptr_null(rtmpout_open(&cfg));
}
END_TEST

START_TEST(rtmpout_delivers_keyframe_end_to_end) {
  unsigned port;
  int listen_fd = make_listener(&port, 0);
  pthread_t th;
  fake_server_t srv;
  char url[64];
  rtmpout_cfg_t cfg;
  rtmpout_t *o;
  unsigned char keyframe[8] = {0x17, 0x01, 0x00, 0x00, 0x00, 'K', 'E', 'Y'}; /* classic H.264 NALU tag, FrameType=key */

  memset(&srv, 0, sizeof srv);
  srv.listen_fd = listen_fd;
  ck_assert_int_eq(pthread_create(&th, NULL, fake_server_thread, &srv), 0);

  snprintf(url, sizeof url, "rtmp://127.0.0.1:%u/live/key123", port);
  memset(&cfg, 0, sizeof cfg);
  cfg.url = url;
  o = rtmpout_open(&cfg);
  ck_assert_ptr_nonnull(o);

  ck_assert_int_eq(drive_until_sent(o, FLV_TAG_VIDEO, keyframe, sizeof keyframe, 400), 1);

  pthread_join(th, NULL);
  close(listen_fd);
  ck_assert_ptr_nonnull(memmem(srv.captured, srv.captured_len, "KEY", 3));

  rtmpout_close(o);
}
END_TEST

START_TEST(rtmpout_holds_back_interframe_until_keyframe) {
  unsigned port;
  int listen_fd = make_listener(&port, 0);
  pthread_t th;
  fake_server_t srv;
  char url[64];
  rtmpout_cfg_t cfg;
  rtmpout_t *o;
  unsigned char interframe[8] = {0x27, 0x01, 0x00, 0x00, 0x00, 'I', 'N', 'T'}; /* FrameType=inter, held back pre-keyframe */
  unsigned char keyframe[8] = {0x17, 0x01, 0x00, 0x00, 0x00, 'K', 'E', 'Y'};

  memset(&srv, 0, sizeof srv);
  srv.listen_fd = listen_fd;
  ck_assert_int_eq(pthread_create(&th, NULL, fake_server_thread, &srv), 0);

  snprintf(url, sizeof url, "rtmp://127.0.0.1:%u/live/key123", port);
  memset(&cfg, 0, sizeof cfg);
  cfg.url = url;
  o = rtmpout_open(&cfg);
  ck_assert_ptr_nonnull(o);

  /* held-back frames also return 0, not -1: drive_until_sent's success check doesn't apply here */
  for (int i = 0; i < 50; i++) {
    rtmpout_write(o, FLV_TAG_VIDEO, 0, interframe, sizeof interframe, NULL, 0);
    struct timespec ts = {0, 5000000L};
    nanosleep(&ts, NULL);
  }
  ck_assert_int_eq(drive_until_sent(o, FLV_TAG_VIDEO, keyframe, sizeof keyframe, 400), 1);

  pthread_join(th, NULL);
  close(listen_fd);
  ck_assert_ptr_null(memmem(srv.captured, srv.captured_len, "INT", 3));
  ck_assert_ptr_nonnull(memmem(srv.captured, srv.captured_len, "KEY", 3));

  rtmpout_close(o);
}
END_TEST

START_TEST(rtmpout_survives_stalled_peer) {
  unsigned port;
  int listen_fd = make_listener(&port, 16384);
  pthread_t th;
  stall_server_t srv;
  char url[64];
  rtmpout_cfg_t cfg;
  rtmpout_t *o;
  unsigned char keyframe[8] = {0x17, 0x01, 0x00, 0x00, 0x00, 'K', 'E', 'Y'};
  unsigned char endframe[8] = {0x17, 0x01, 0x00, 0x00, 0x00, 'E', 'N', 'D'};
  size_t big_len = 512 * 1024;
  unsigned char *big = calloc(1, big_len);

  ck_assert_ptr_nonnull(big);
  big[0] = 0x17;
  big[1] = 0x01;
  memset(&srv, 0, sizeof srv);
  srv.listen_fd = listen_fd;
  ck_assert_int_eq(pthread_create(&th, NULL, stall_server_thread, &srv), 0);

  snprintf(url, sizeof url, "rtmp://127.0.0.1:%u/live/key123", port);
  memset(&cfg, 0, sizeof cfg);
  cfg.url = url;
  o = rtmpout_open(&cfg);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive_until_sent(o, FLV_TAG_VIDEO, keyframe, sizeof keyframe, 400), 1);

  for (int i = 0; i < 20; i++)
    ck_assert_int_eq(rtmpout_write(o, FLV_TAG_VIDEO, (uint32_t)i * 40, big, big_len, NULL, 0), 0);

  atomic_store(&srv.drain, 1);
  for (int i = 0; i < 400 && !atomic_load(&srv.found_end); i++) {
    struct timespec ts = {0, 5000000L};
    rtmpout_write(o, FLV_TAG_VIDEO, 1000 + (uint32_t)i * 40, endframe, sizeof endframe, NULL, 0);
    nanosleep(&ts, NULL);
  }

  pthread_join(th, NULL);
  close(listen_fd);
  ck_assert_int_eq(atomic_load(&srv.found_end), 1);

  rtmpout_close(o);
  free(big);
}
END_TEST

START_TEST(rtmpout_sends_adobe_authmod_for_userinfo_uri) {
  unsigned port;
  int listen_fd = make_listener(&port, 0);
  pthread_t th;
  fake_server_t srv;
  char url[96];
  rtmpout_cfg_t cfg;
  rtmpout_t *o;
  unsigned char keyframe[8] = {0x17, 0x01, 0x00, 0x00, 0x00, 'K', 'E', 'Y'};

  memset(&srv, 0, sizeof srv);
  srv.listen_fd = listen_fd;
  ck_assert_int_eq(pthread_create(&th, NULL, capture_connect_thread, &srv), 0);

  snprintf(url, sizeof url, "rtmp://alice:s3cret@127.0.0.1:%u/live/key123", port);
  memset(&cfg, 0, sizeof cfg);
  cfg.url = url;
  o = rtmpout_open(&cfg);
  ck_assert_ptr_nonnull(o);

  for (int i = 0; i < 200; i++) {
    rtmpout_write(o, FLV_TAG_VIDEO, 0, keyframe, sizeof keyframe, NULL, 0);
    struct timespec ts = {0, 5000000L};
    nanosleep(&ts, NULL);
  }

  pthread_join(th, NULL);
  close(listen_fd);
  ck_assert_ptr_nonnull(memmem(srv.captured, srv.captured_len, "authmod=adobe&user=alice", 25));

  rtmpout_close(o);
}
END_TEST

START_TEST(rtmpout_queue_overflow_drops_the_connection) {
  unsigned port;
  int listen_fd = make_listener(&port, 16384);
  pthread_t th;
  stall_server_t srv;
  char url[64];
  rtmpout_cfg_t cfg;
  rtmpout_t *o;
  unsigned char keyframe[8] = {0x17, 0x01, 0x00, 0x00, 0x00, 'K', 'E', 'Y'};
  size_t big_len = 512 * 1024;
  unsigned char *big = calloc(1, big_len);
  int failed_at = -1;

  ck_assert_ptr_nonnull(big);
  big[0] = 0x17;
  big[1] = 0x00;
  memset(&srv, 0, sizeof srv);
  srv.listen_fd = listen_fd;
  ck_assert_int_eq(pthread_create(&th, NULL, stall_server_thread, &srv), 0);

  snprintf(url, sizeof url, "rtmp://127.0.0.1:%u/live/key123", port);
  memset(&cfg, 0, sizeof cfg);
  cfg.url = url;
  o = rtmpout_open(&cfg);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive_until_sent(o, FLV_TAG_VIDEO, keyframe, sizeof keyframe, 400), 1);

  for (int i = 0; i < 48 && failed_at < 0; i++)
    if (rtmpout_write(o, FLV_TAG_VIDEO, (uint32_t)i, big, big_len, NULL, 0) < 0)
      failed_at = i;
  ck_assert_int_gt(failed_at, 0);
  ck_assert_int_eq(rtmpout_write(o, FLV_TAG_VIDEO, 0, keyframe, sizeof keyframe, NULL, 0), -1);

  atomic_store(&srv.drain, 1);
  rtmpout_close(o);
  pthread_join(th, NULL);
  close(listen_fd);
  free(big);
}
END_TEST

START_TEST(rtmpout_partial_write_keeps_remainder_queued) {
  unsigned port;
  int listen_fd = make_listener(&port, 16384);
  pthread_t th;
  stall_server_t srv;
  char url[64];
  rtmpout_cfg_t cfg;
  rtmpout_t *o;
  unsigned char keyframe[8] = {0x17, 0x01, 0x00, 0x00, 0x00, 'K', 'E', 'Y'};
  unsigned char audio[2] = {0xAF, 0x01};
  size_t big_len = 7u << 20;
  unsigned char *big = calloc(1, big_len);

  ck_assert_ptr_nonnull(big);
  big[0] = 0x17;
  big[1] = 0x01;
  memcpy(big + big_len - 3, "END", 3);
  memset(&srv, 0, sizeof srv);
  srv.listen_fd = listen_fd;
  ck_assert_int_eq(pthread_create(&th, NULL, stall_server_thread, &srv), 0);

  snprintf(url, sizeof url, "rtmp://127.0.0.1:%u/live/key123", port);
  memset(&cfg, 0, sizeof cfg);
  cfg.url = url;
  o = rtmpout_open(&cfg);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive_until_sent(o, FLV_TAG_VIDEO, keyframe, sizeof keyframe, 400), 1);
  ck_assert_int_eq(rtmpout_write(o, FLV_TAG_VIDEO, 40, big, big_len, NULL, 0), 0);
  ck_assert_int_eq(atomic_load(&srv.found_end), 0);

  atomic_store(&srv.drain, 1);
  for (int i = 0; i < 2000 && !atomic_load(&srv.found_end); i++) {
    struct timespec ts = {0, 5000000L};
    rtmpout_write(o, FLV_TAG_AUDIO, 80 + (uint32_t)i, audio, sizeof audio, NULL, 0);
    nanosleep(&ts, NULL);
  }
  pthread_join(th, NULL);
  close(listen_fd);
  ck_assert_int_eq(atomic_load(&srv.found_end), 1);
  rtmpout_close(o);
  free(big);
}
END_TEST

START_TEST(rtmpout_url_forms_are_parsed) {
  static const char *const good[] = {
    "rtmp://host/app",
    "rtmp://host:1936/app/key",
    "rtmp://host/a/b/key",
    "rtmp://user@host/app/key",
    "rtmp://user:pass@host/app/key",
    "rtmp://user:p@ss@host:1936/app/key",
    "rtmps://host/app/key",
  };
  rtmpout_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
    cfg.url = good[i];
    rtmpout_t *o = rtmpout_open(&cfg);
    ck_assert_msg(o != NULL, "%s", good[i]);
    rtmpout_close(o);
  }
}
END_TEST

START_TEST(rtmpout_oversized_and_bad_url_parts_are_rejected) {
  static const char *const bad[] = {
    "rtmp://host/",
    "rtmp://host:/app/key",
    "rtmp://host:70000/app/key",
    "rtmp://host:12345678/app/key",
    "rtmp://:pass@host/app/key",
    "rtmp://@host/app/key",
    "rtmp:///app/key",
  };
  char big[1200];
  char url[1500];
  rtmpout_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    cfg.url = bad[i];
    ck_assert_msg(rtmpout_open(&cfg) == NULL, "%s", bad[i]);
  }
  memset(big, 'a', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  cfg.url = url;
  snprintf(url, sizeof url, "rtmp://%s@host/app/key", big);
  ck_assert_ptr_null(rtmpout_open(&cfg));
  snprintf(url, sizeof url, "rtmp://user:%s@host/app/key", big);
  ck_assert_ptr_null(rtmpout_open(&cfg));
  snprintf(url, sizeof url, "rtmp://%s/app/key", big);
  ck_assert_ptr_null(rtmpout_open(&cfg));
  snprintf(url, sizeof url, "rtmp://host/%s/key", big);
  ck_assert_ptr_null(rtmpout_open(&cfg));
}
END_TEST

START_TEST(rtmpout_unreachable_peers_stay_non_fatal) {
  static const char *const urls[] = {"rtmp://127.0.0.1:1/live/key", "rtmps://127.0.0.1:1/live/key"};
  rtmpout_cfg_t cfg;
  rtmpout_t *o;
  unsigned char keyframe[8] = {0x17, 0x01, 0x00, 0x00, 0x00, 'K', 'E', 'Y'};

  memset(&cfg, 0, sizeof cfg);
  cfg.url = urls[_i];
  cfg.insecure = 1;
  o = rtmpout_open(&cfg);
  ck_assert_ptr_nonnull(o);
  for (int i = 0; i < 40; i++) {
    struct timespec ts = {0, 10000000L};
    ck_assert_int_eq(rtmpout_write(o, FLV_TAG_VIDEO, 0, keyframe, sizeof keyframe, NULL, 0), -1);
    nanosleep(&ts, NULL);
  }
  rtmpout_close(o);
}
END_TEST

typedef struct {
  int listen_fd;
} closer_t;

static void *closing_server_thread(void *arg) {
  closer_t *c = arg;
  for (int i = 0; i < 3; i++) {
    int cfd = accept(c->listen_fd, NULL, NULL);
    if (cfd < 0) break;
    close(cfd);
  }
  return NULL;
}

START_TEST(rtmpout_peer_that_hangs_up_is_retried) {
  static const char *const schemes[] = {"rtmp", "rtmps"};
  unsigned port;
  int listen_fd = make_listener(&port, 0);
  pthread_t th;
  closer_t srv = {listen_fd};
  char url[64];
  rtmpout_cfg_t cfg;
  rtmpout_t *o;
  unsigned char keyframe[8] = {0x17, 0x01, 0x00, 0x00, 0x00, 'K', 'E', 'Y'};

  ck_assert_int_eq(pthread_create(&th, NULL, closing_server_thread, &srv), 0);
  snprintf(url, sizeof url, "%s://127.0.0.1:%u/live/key", schemes[_i], port);
  memset(&cfg, 0, sizeof cfg);
  cfg.url = url;
  cfg.insecure = 1;
  o = rtmpout_open(&cfg);
  ck_assert_ptr_nonnull(o);
  for (int i = 0; i < 300; i++) {
    struct timespec ts = {0, 10000000L};
    rtmpout_write(o, FLV_TAG_VIDEO, 0, keyframe, sizeof keyframe, NULL, 0);
    nanosleep(&ts, NULL);
  }
  shutdown(listen_fd, SHUT_RDWR);
  pthread_join(th, NULL);
  close(listen_fd);
  rtmpout_close(o);
}
END_TEST

static Suite *rtmpout_suite(void) {
  Suite *s = suite_create("rtmpout");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rtmpout_open_rejects_malformed_urls);
  tcase_add_test(tc, rtmpout_delivers_keyframe_end_to_end);
  tcase_add_test(tc, rtmpout_holds_back_interframe_until_keyframe);
  tcase_add_test(tc, rtmpout_survives_stalled_peer);
  tcase_add_test(tc, rtmpout_sends_adobe_authmod_for_userinfo_uri);
  tcase_add_test(tc, rtmpout_queue_overflow_drops_the_connection);
  tcase_add_test(tc, rtmpout_partial_write_keeps_remainder_queued);
  tcase_add_test(tc, rtmpout_url_forms_are_parsed);
  tcase_add_test(tc, rtmpout_oversized_and_bad_url_parts_are_rejected);
  tcase_add_loop_test(tc, rtmpout_unreachable_peers_stay_non_fatal, 0, 2);
  tcase_add_loop_test(tc, rtmpout_peer_that_hangs_up_is_retried, 0, 2);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rtmpout_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
