/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "lib/mux/amf.h"
#include "lib/net/rtmp/chunk.h"
#include "lib/net/rtmp/handshake.h"
#include "lib/net/rtmpout.h"

#define RTMP_TYPE_INVOKE 20
#define SRV_QUIET_MS 300

static int make_listener(unsigned *port_out) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;

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

static void *fake_server_thread(void *arg) {
  fake_server_t *s = arg;
  int cfd = accept(s->listen_fd, NULL, NULL);
  unsigned char s0s1s2[1 + 2 * RTMP_HANDSHAKE_SIZE];
  unsigned char msg[512];
  size_t n;
  struct timeval tv = {1, 0};

  if (cfd < 0)
    return NULL;

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
    if (0 == rtmpout_write(o, type, 0, data, len))
      return 1;
    usleep(5000);
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
  int listen_fd = make_listener(&port);
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
  int listen_fd = make_listener(&port);
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
    rtmpout_write(o, FLV_TAG_VIDEO, 0, interframe, sizeof interframe);
    usleep(5000);
  }
  ck_assert_int_eq(drive_until_sent(o, FLV_TAG_VIDEO, keyframe, sizeof keyframe, 400), 1);

  pthread_join(th, NULL);
  close(listen_fd);
  ck_assert_ptr_null(memmem(srv.captured, srv.captured_len, "INT", 3));
  ck_assert_ptr_nonnull(memmem(srv.captured, srv.captured_len, "KEY", 3));

  rtmpout_close(o);
}
END_TEST

START_TEST(rtmpout_sends_adobe_authmod_for_userinfo_uri) {
  unsigned port;
  int listen_fd = make_listener(&port);
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
    rtmpout_write(o, FLV_TAG_VIDEO, 0, keyframe, sizeof keyframe);
    usleep(5000);
  }

  pthread_join(th, NULL);
  close(listen_fd);
  ck_assert_ptr_nonnull(memmem(srv.captured, srv.captured_len, "authmod=adobe&user=alice", 25));

  rtmpout_close(o);
}
END_TEST

static Suite *rtmpout_suite(void) {
  Suite *s = suite_create("rtmpout");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rtmpout_open_rejects_malformed_urls);
  tcase_add_test(tc, rtmpout_delivers_keyframe_end_to_end);
  tcase_add_test(tc, rtmpout_holds_back_interframe_until_keyframe);
  tcase_add_test(tc, rtmpout_sends_adobe_authmod_for_userinfo_uri);
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
