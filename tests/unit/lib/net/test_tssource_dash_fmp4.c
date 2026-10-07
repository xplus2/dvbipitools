/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

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

#include "lib/mux/fmp4/fmp4.h"
#include "lib/net/ts/source.h"
#include "lib/net/ts/source_priv.h"

typedef struct {
  int listen_fd;
  const char *const *responses;
  const size_t *response_lens;
  int n_responses;
} scripted_server_t;

static void *serve_scripted(void *arg) {
  const scripted_server_t *a = arg;
  for (int i = 0; i < a->n_responses; i++) {
    int cfd = accept(a->listen_fd, NULL, NULL);
    struct timeval tv = {2, 0};
    char buf[4096];
    size_t got = 0;
    if (cfd < 0) return NULL;
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    for (;;) {
      ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
      if (n <= 0) break;
      got += (size_t)n;
      if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0) break;
    }
    send(cfd, a->responses[i], a->response_lens[i], 0);
    close(cfd);
  }
  return NULL;
}

static int make_listener(unsigned *port_out) {
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

static tssrc_open_state_t drive_open(tssrc_open_t *o, int max_iters) {
  tssrc_open_state_t st = TSSRC_OPEN_PENDING;
  for (int i = 0; i < max_iters && st == TSSRC_OPEN_PENDING; i++) {
    struct pollfd pfd;
    int fd = tssrc_open_async_poll_fd(o);
    if (fd < 0) {
      st = tssrc_open_async_step(o, NULL);
      continue;
    }
    pfd.fd = fd;
    pfd.events = tssrc_open_async_poll_events(o);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    st = tssrc_open_async_step(o, NULL);
  }
  return st;
}

static size_t drive_read(tssrc_t *s, unsigned char *buf, size_t cap, int max_iters) {
  size_t got = 0;
  for (int i = 0; i < max_iters && got < cap; i++) {
    struct pollfd pfd;
    ssize_t n;
    int fd = tssrc_fd(s);
    if (fd >= 0) {
      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      poll(&pfd, 1, 50);
    }
    n = tssrc_read(s, buf + got, cap - got, NULL);
    if (n > 0) got += (size_t)n;
    else if (n < 0) break;
  }
  return got;
}

START_TEST(tssrc_http_dispatches_dash_fmp4_and_remuxes_to_ts) {
  unsigned mpd_port, init_port, seg_port;
  int mpd_fd = make_listener(&mpd_port);
  int init_fd = make_listener(&init_port);
  int seg_fd = make_listener(&seg_port);
  pthread_t mpd_th, init_th, seg_th;
  fmp4_track_cfg_t tcfg;
  fmp4_mux_t *m;
  unsigned char *initbuf, *segbuf;
  unsigned char init_saved[2048];
  size_t initlen, seglen;
  static const unsigned char avcc[] = {0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x00, 0x01, 0x00, 0x00};
  static const unsigned char frame[] = {0x00, 0x00, 0x00, 0x03, 0x65, 0x11, 0x22};
  fmp4_sample_t sample;
  char mpd_body[1024];
  char mpd_resp[1200], init_resp[2304], seg_resp[2304];
  size_t mpd_resp_len, init_resp_len, seg_resp_len;
  const char *mpd_responses[2];
  size_t mpd_lens[2];
  const char *init_responses[1];
  size_t init_lens[1];
  const char *seg_responses[1];
  size_t seg_lens[1];
  scripted_server_t mpd_srv, init_srv, seg_srv;
  tssrc_cfg_t cfg;
  tssrc_open_t *o;
  tssrc_t *s;
  unsigned char out[4096];
  size_t got;
  int i, saw_pat;
  char uri[64];

  memset(&tcfg, 0, sizeof tcfg);
  tcfg.codec = CODEC_H264;
  tcfg.track_id = 1;
  tcfg.timescale = 90000;
  tcfg.width = 1280;
  tcfg.height = 720;
  tcfg.cpriv = avcc;
  tcfg.cpriv_len = sizeof avcc;
  m = fmp4_mux_new(&tcfg, 1);
  ck_assert_ptr_nonnull(m);
  initlen = fmp4_init_segment(m, &initbuf);
  ck_assert_uint_le(initlen, sizeof init_saved);
  memcpy(init_saved, initbuf, initlen);
  fmp4_segment_begin(m, 1);
  memset(&sample, 0, sizeof sample);
  sample.track_idx = 0;
  sample.data = frame;
  sample.size = sizeof frame;
  sample.duration = 3000;
  sample.keyframe = 1;
  fmp4_segment_add_sample(m, &sample);
  seglen = fmp4_segment_end(m, &segbuf);

  snprintf(mpd_body, sizeof mpd_body,
    "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"
    "<?xml version=\"1.0\"?>\n<MPD type=\"static\">\n"
    "  <Period id=\"0\">\n"
    "    <AdaptationSet mimeType=\"video/mp4\">\n"
    "      <Representation id=\"v\" bandwidth=\"3000000\">\n"
    "        <SegmentTemplate initialization=\"http://127.0.0.1:%u/init.mp4\" media=\"http://127.0.0.1:%u/seg$Number$.m4s\" timescale=\"1000\" duration=\"10\" startNumber=\"1\"/>\n"
    "      </Representation>\n"
    "    </AdaptationSet>\n"
    "  </Period>\n"
    "</MPD>\n",
    init_port, seg_port);
  mpd_resp_len = strlen(mpd_body);
  ck_assert_uint_le(mpd_resp_len, sizeof mpd_resp);
  memcpy(mpd_resp, mpd_body, mpd_resp_len);
  mpd_responses[0] = mpd_resp;
  mpd_responses[1] = mpd_resp;
  mpd_lens[0] = mpd_resp_len;
  mpd_lens[1] = mpd_resp_len;
  mpd_srv.listen_fd = mpd_fd;
  mpd_srv.responses = mpd_responses;
  mpd_srv.response_lens = mpd_lens;
  mpd_srv.n_responses = 2;
  ck_assert_int_eq(pthread_create(&mpd_th, NULL, serve_scripted, &mpd_srv), 0);

  {
    size_t head_len = (size_t)snprintf(init_resp, sizeof init_resp, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n", initlen);
    ck_assert_uint_le(head_len + initlen, sizeof init_resp);
    memcpy(init_resp + head_len, init_saved, initlen);
    init_resp_len = head_len + initlen;
  }
  init_responses[0] = init_resp;
  init_lens[0] = init_resp_len;
  init_srv.listen_fd = init_fd;
  init_srv.responses = init_responses;
  init_srv.response_lens = init_lens;
  init_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&init_th, NULL, serve_scripted, &init_srv), 0);

  {
    size_t head_len = (size_t)snprintf(seg_resp, sizeof seg_resp, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n", seglen);
    ck_assert_uint_le(head_len + seglen, sizeof seg_resp);
    memcpy(seg_resp + head_len, segbuf, seglen);
    seg_resp_len = head_len + seglen;
  }
  seg_responses[0] = seg_resp;
  seg_lens[0] = seg_resp_len;
  seg_srv.listen_fd = seg_fd;
  seg_srv.responses = seg_responses;
  seg_srv.response_lens = seg_lens;
  seg_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&seg_th, NULL, serve_scripted, &seg_srv), 0);

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_HTTP;
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/live.mpd", mpd_port);
  ck_assert_int_eq(http_url_parse(uri, &cfg.http), 0);

  o = tssrc_open_async_start(&cfg, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive_open(o, 300), TSSRC_OPEN_DONE);
  s = tssrc_open_async_take(o);
  ck_assert_ptr_nonnull(s);

  got = drive_read(s, out, sizeof out, 300);
  ck_assert_uint_gt(got, 0u);
  ck_assert_uint_eq(got % 188, 0u);

  saw_pat = 0;
  for (i = 0; (size_t)i < got; i += 188) {
    unsigned pid = ((unsigned)(out[i + 1] & 0x1F) << 8) | out[i + 2];
    if (pid == 0x0000) saw_pat = 1;
  }
  ck_assert_int_eq(saw_pat, 1);

  tssrc_close(s);
  fmp4_mux_free(m);
  pthread_join(mpd_th, NULL);
  pthread_join(init_th, NULL);
  pthread_join(seg_th, NULL);
  close(mpd_fd);
  close(init_fd);
  close(seg_fd);
}
END_TEST

#define SCRIPT_MAX 4
#define SCRIPT_RESP_CAP 4096

typedef struct {
  char resp[SCRIPT_MAX][SCRIPT_RESP_CAP];
  const char *ptr[SCRIPT_MAX];
  size_t len[SCRIPT_MAX];
  scripted_server_t srv;
  pthread_t th;
  int fd;
  unsigned port;
  int n;
} script_t;

static void script_add(script_t *sc, const void *body, size_t body_len) {
  int i = sc->n++;
  size_t head;

  ck_assert_int_lt(i, SCRIPT_MAX);
  head = (size_t)snprintf(sc->resp[i], SCRIPT_RESP_CAP, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n", body_len);
  ck_assert_uint_le(head + body_len, SCRIPT_RESP_CAP);
  memcpy(sc->resp[i] + head, body, body_len);
  sc->ptr[i] = sc->resp[i];
  sc->len[i] = head + body_len;
}

static void script_start(script_t *sc) {
  sc->fd = make_listener(&sc->port);
  sc->srv.listen_fd = sc->fd;
  sc->srv.responses = sc->ptr;
  sc->srv.response_lens = sc->len;
  sc->srv.n_responses = sc->n;
  ck_assert_int_eq(pthread_create(&sc->th, NULL, serve_scripted, &sc->srv), 0);
}

static void script_stop(script_t *sc) {
  pthread_join(sc->th, NULL);
  close(sc->fd);
}

static tssrc_t *open_hls(const script_t *sc, const char *path) {
  tssrc_cfg_t cfg;
  tssrc_open_t *o;
  char uri[96];

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_HTTP;
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u%s", sc->port, path);
  ck_assert_int_eq(http_url_parse(uri, &cfg.http), 0);
  o = tssrc_open_async_start(&cfg, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive_open(o, 300), TSSRC_OPEN_DONE);
  return tssrc_open_async_take(o);
}

static size_t build_avc_init(unsigned char *out, size_t cap) {
  static const unsigned char avcc[] = {0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x00, 0x01, 0x00, 0x00};
  fmp4_track_cfg_t tcfg;
  fmp4_mux_t *m;
  unsigned char *buf;
  size_t len;

  memset(&tcfg, 0, sizeof tcfg);
  tcfg.codec = CODEC_H264;
  tcfg.track_id = 1;
  tcfg.timescale = 90000;
  tcfg.width = 1280;
  tcfg.height = 720;
  tcfg.cpriv = avcc;
  tcfg.cpriv_len = sizeof avcc;
  m = fmp4_mux_new(&tcfg, 1);
  ck_assert_ptr_nonnull(m);
  len = fmp4_init_segment(m, &buf);
  ck_assert_uint_le(len, cap);
  memcpy(out, buf, len);
  fmp4_mux_free(m);
  return len;
}

static const char hls_ts_media[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nseg1.ts\n";
static const char hls_fmp4_media[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:4.0,\nseg1.m4s\n";

START_TEST(hls_media_playlist_without_map_opens_as_ts_passthrough) {
  script_t sc = {0};
  tssrc_t *s;

  script_add(&sc, hls_ts_media, strlen(hls_ts_media));
  script_start(&sc);
  s = open_hls(&sc, "/live.m3u8");
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(s->http_sub, HTTP_SUB_HLS);
  ck_assert_uint_eq(s->n_media, 1u);
  ck_assert_ptr_nonnull(s->hls[0]);
  ck_assert_uint_eq(s->remux.n_tracks, 0u);
  tssrc_close(s);
  script_stop(&sc);
}
END_TEST

START_TEST(hls_master_picks_variant_without_map_as_ts_passthrough) {
  static const char master[] = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000000\nlow.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=3000000\nhigh.m3u8\n";
  script_t sc = {0};
  tssrc_t *s;

  script_add(&sc, master, strlen(master));
  script_add(&sc, hls_ts_media, strlen(hls_ts_media));
  script_start(&sc);
  s = open_hls(&sc, "/master.m3u8");
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(s->http_sub, HTTP_SUB_HLS);
  ck_assert_uint_eq(s->n_media, 1u);
  ck_assert_uint_eq(s->remux.n_tracks, 0u);
  tssrc_close(s);
  script_stop(&sc);
}
END_TEST

START_TEST(hls_master_with_audio_rendition_lacking_map_drops_audio) {
  static const char master[] = "#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",URI=\"audio.m3u8\"\n#EXT-X-STREAM-INF:BANDWIDTH=3000000,AUDIO=\"aud\"\nvideo.m3u8\n";
  unsigned char init[2048];
  size_t init_len = build_avc_init(init, sizeof init);
  script_t sc = {0};
  tssrc_t *s;

  script_add(&sc, master, strlen(master));
  script_add(&sc, hls_fmp4_media, strlen(hls_fmp4_media));
  script_add(&sc, hls_ts_media, strlen(hls_ts_media));
  script_add(&sc, init, init_len);
  script_start(&sc);
  s = open_hls(&sc, "/master.m3u8");
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(s->http_sub, HTTP_SUB_HLS);
  ck_assert_uint_eq(s->n_media, 1u);
  ck_assert_uint_eq(s->remux.n_tracks, 1u);
  tssrc_close(s);
  script_stop(&sc);
}
END_TEST

START_TEST(hls_media_playlist_with_map_fetches_init_and_remuxes) {
  unsigned char init[2048];
  size_t init_len = build_avc_init(init, sizeof init);
  script_t sc = {0};
  tssrc_t *s;

  script_add(&sc, hls_fmp4_media, strlen(hls_fmp4_media));
  script_add(&sc, init, init_len);
  script_start(&sc);
  s = open_hls(&sc, "/live.m3u8");
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(s->http_sub, HTTP_SUB_HLS);
  ck_assert_uint_eq(s->n_media, 1u);
  ck_assert_uint_eq(s->remux.n_tracks, 1u);
  tssrc_close(s);
  script_stop(&sc);
}
END_TEST

static Suite *tssource_dash_fmp4_suite(void) {
  Suite *s = suite_create("tssource_dash_fmp4");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, tssrc_http_dispatches_dash_fmp4_and_remuxes_to_ts);
  tcase_add_test(tc, hls_media_playlist_without_map_opens_as_ts_passthrough);
  tcase_add_test(tc, hls_master_picks_variant_without_map_as_ts_passthrough);
  tcase_add_test(tc, hls_master_with_audio_rendition_lacking_map_drops_audio);
  tcase_add_test(tc, hls_media_playlist_with_map_fetches_init_and_remuxes);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tssource_dash_fmp4_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
