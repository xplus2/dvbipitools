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

#define SCRIPT_MAX 6
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

static void script_add_status(script_t *sc, const char *status, const void *body, size_t body_len) {
  int i = sc->n++;
  size_t head;

  ck_assert_int_lt(i, SCRIPT_MAX);
  head = (size_t)snprintf(sc->resp[i], SCRIPT_RESP_CAP, "HTTP/1.1 %s\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n", status, body_len);
  ck_assert_uint_le(head + body_len, SCRIPT_RESP_CAP);
  memcpy(sc->resp[i] + head, body, body_len);
  sc->ptr[i] = sc->resp[i];
  sc->len[i] = head + body_len;
}

static void script_add(script_t *sc, const void *body, size_t body_len) {
  script_add_status(sc, "200 OK", body, body_len);
}

static void script_start(script_t *sc) {
  sc->fd = make_listener(&sc->port);
  sc->srv.listen_fd = sc->fd;
  sc->srv.responses = sc->ptr;
  sc->srv.response_lens = sc->len;
  sc->srv.n_responses = sc->n;
  ck_assert_int_eq(pthread_create(&sc->th, NULL, serve_scripted, &sc->srv), 0);
}

static void script_stop(const script_t *sc) {
  pthread_join(sc->th, NULL);
  close(sc->fd);
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

static tssrc_open_state_t open_state(const script_t *sc, const char *path, tssrc_t **result) {
  tssrc_cfg_t cfg;
  tssrc_open_t *o;
  tssrc_open_state_t st;
  char uri[96];

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_HTTP;
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u%s", sc->port, path);
  ck_assert_int_eq(http_url_parse(uri, &cfg.http), 0);
  o = tssrc_open_async_start(&cfg, NULL);
  ck_assert_ptr_nonnull(o);
  st = drive_open(o, 300);
  if (st == TSSRC_OPEN_DONE && result) *result = tssrc_open_async_take(o);
  else tssrc_open_async_free(o);
  return st;
}

static const char media_map[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:4.0,\nseg1.m4s\n";
static const char master_audio[] = "#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",URI=\"audio.m3u8\"\n#EXT-X-STREAM-INF:BANDWIDTH=3000000,AUDIO=\"aud\"\nvideo.m3u8\n";

static tssrc_open_state_t run_script(const char *path, void (*fill)(script_t *), tssrc_t **result) {
  script_t sc = {0};
  tssrc_open_state_t st;
  fill(&sc);
  script_start(&sc);
  st = open_state(&sc, path, result);
  script_stop(&sc);
  return st;
}

static void fill_ts_direct(script_t *sc) {
  unsigned char body[3 * 188];
  memset(body, 0xAB, sizeof body);
  body[0] = 0x47;
  body[188] = 0x47;
  body[376] = 0x47;
  script_add(sc, body, sizeof body);
}

static void fill_short_unknown(script_t *sc) {
  script_add(sc, "abc", 3);
}

static void fill_empty(script_t *sc) {
  script_add(sc, "", 0);
}

static void fill_master_two_inits(script_t *sc) {
  unsigned char init[2048];
  size_t n = build_avc_init(init, sizeof init);
  script_add(sc, master_audio, strlen(master_audio));
  script_add(sc, media_map, strlen(media_map));
  script_add(sc, media_map, strlen(media_map));
  script_add(sc, init, n);
  script_add(sc, init, n);
}

static void fill_master_bad_variant_status(script_t *sc) {
  script_add(sc, master_audio, strlen(master_audio));
  script_add_status(sc, "404 Not Found", "", 0);
}

static void fill_master_bad_variant_body(script_t *sc) {
  script_add(sc, master_audio, strlen(master_audio));
  script_add(sc, "not a playlist at all", 21);
}

static void fill_media_init_404(script_t *sc) {
  script_add(sc, media_map, strlen(media_map));
  script_add_status(sc, "404 Not Found", "", 0);
}

static void fill_media_init_garbage(script_t *sc) {
  script_add(sc, media_map, strlen(media_map));
  script_add(sc, "garbage init segment", 20);
}

static void fill_media_bad_playlist(script_t *sc) {
  script_add(sc, "#EXTM3U\nthis is not valid\n", 26);
}

static void fill_master_bad_variant_url(script_t *sc) {
  static const char m[] = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000\nhttp://\n";
  script_add(sc, m, strlen(m));
}

static void fill_master_no_variants(script_t *sc) {
  static const char m[] = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000\n";
  script_add(sc, m, strlen(m));
}

static void fill_dash_video_only(script_t *sc) {
  static const char mpd[] =
      "<?xml version=\"1.0\"?><MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"dynamic\"><Period id=\"1\">"
      "<AdaptationSet mimeType=\"video/mp4\"><Representation id=\"v\" bandwidth=\"1000\"><SegmentTemplate initialization=\"i.mp4\" media=\"s$Number$.m4s\" timescale=\"1000\" duration=\"2000\" startNumber=\"1\"/></Representation></AdaptationSet>"
      "</Period></MPD>";
  script_add(sc, mpd, strlen(mpd));
}

static void fill_dash_audio_only(script_t *sc) {
  static const char mpd[] =
      "<?xml version=\"1.0\"?><MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"dynamic\"><Period id=\"1\">"
      "<AdaptationSet mimeType=\"audio/mp4\"><Representation id=\"a\" bandwidth=\"1000\"><SegmentTemplate initialization=\"i.mp4\" media=\"s$Number$.m4s\" timescale=\"1000\" duration=\"2000\" startNumber=\"1\"/></Representation></AdaptationSet>"
      "</Period></MPD>";
  script_add(sc, mpd, strlen(mpd));
}

static void fill_dash_both(script_t *sc) {
  static const char mpd[] =
      "<?xml version=\"1.0\"?><MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"dynamic\"><Period id=\"1\">"
      "<AdaptationSet mimeType=\"video/mp4\"><Representation id=\"v\" bandwidth=\"1000\"><SegmentTemplate initialization=\"i.mp4\" media=\"s$Number$.m4s\" timescale=\"1000\" duration=\"2000\" startNumber=\"1\"/></Representation></AdaptationSet>"
      "<AdaptationSet mimeType=\"audio/mp4\"><Representation id=\"a\" bandwidth=\"1000\"><SegmentTemplate initialization=\"ai.mp4\" media=\"as$Number$.m4s\" timescale=\"1000\" duration=\"2000\" startNumber=\"1\"/></Representation></AdaptationSet>"
      "</Period></MPD>";
  script_add(sc, mpd, strlen(mpd));
}

static void fill_dash_xml_without_periods(script_t *sc) {
  static const char mpd[] = "<?xml version=\"1.0\"?><MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\"></MPD>";
  script_add(sc, mpd, strlen(mpd));
}

START_TEST(open_ok_cases) {
  static const struct {
    const char *path;
    void (*fill)(script_t *);
    int sub;
    unsigned n_media;
  } t[] = {
      {"/s", fill_ts_direct, HTTP_SUB_TS, 0},
      {"/m.mpd", fill_dash_video_only, HTTP_SUB_DASH, 1},
      {"/m.mpd", fill_dash_audio_only, HTTP_SUB_DASH, 1},
      {"/m.mpd", fill_dash_both, HTTP_SUB_DASH, 2},
      {"/master.m3u8", fill_master_two_inits, HTTP_SUB_HLS, 2},
      {"/live.m3u8", fill_media_bad_playlist, HTTP_SUB_HLS, 1},
  };
  tssrc_t *s = NULL;
  ck_assert_int_eq(run_script(t[_i].path, t[_i].fill, &s), TSSRC_OPEN_DONE);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(s->http_sub, t[_i].sub);
  if (t[_i].n_media) ck_assert_uint_eq(s->n_media, t[_i].n_media);
  tssrc_close(s);
}
END_TEST

START_TEST(open_error_cases) {
  static const struct {
    const char *path;
    void (*fill)(script_t *);
  } t[] = {
      {"/s", fill_short_unknown},
      {"/s", fill_empty},
      {"/m.mpd", fill_dash_xml_without_periods},
      {"/master.m3u8", fill_master_bad_variant_status},
      {"/master.m3u8", fill_master_bad_variant_body},
      {"/live.m3u8", fill_media_init_404},
      {"/live.m3u8", fill_media_init_garbage},
      {"/master.m3u8", fill_master_bad_variant_url},
      {"/master.m3u8", fill_master_no_variants},
  };
  tssrc_t *s = NULL;
  ck_assert_int_eq(run_script(t[_i].path, t[_i].fill, &s), TSSRC_OPEN_ERROR);
  ck_assert_ptr_null(s);
}
END_TEST

START_TEST(non_http_kinds_open_synchronously) {
  tssrc_cfg_t cfg;
  tssrc_open_t *o;
  tssrc_t *s;
  char path[] = "/tmp/dvbipitools_test_openasync_XXXXXX";
  int fd = mkstemp(path);
  unsigned char ts[188];
  net_err_reason_t reason = NET_ERR_OTHER;
  memset(ts, 0, sizeof ts);
  ts[0] = 0x47;
  ck_assert_int_eq(write(fd, ts, sizeof ts), (ssize_t)sizeof ts);
  close(fd);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_FILE;
  cfg.file_path = path;
  o = tssrc_open_async_start(&cfg, &reason);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(tssrc_open_async_poll_fd(o), -1);
  ck_assert_int_eq(tssrc_open_async_poll_events(o), 0);
  ck_assert_int_eq(tssrc_open_async_step(o, NULL), TSSRC_OPEN_DONE);
  s = tssrc_open_async_take(o);
  ck_assert_ptr_nonnull(s);
  tssrc_close(s);
  cfg.file_path = "/nonexistent/dir/file.ts";
  ck_assert_ptr_null(tssrc_open_async_start(&cfg, &reason));
  unlink(path);
}
END_TEST

START_TEST(blocking_open_forms) {
  script_t sc = {0};
  tssrc_cfg_t cfg;
  tssrc_t *s;
  net_err_reason_t reason = NET_ERR_OTHER;
  char uri[96];
  fill_ts_direct(&sc);
  script_start(&sc);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_HTTP;
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/s", sc.port);
  ck_assert_int_eq(http_url_parse(uri, &cfg.http), 0);
  cfg.user_agent = "ua-test";
  s = tssrc_open_blocking(&cfg, 5.0, &reason);
  ck_assert_ptr_nonnull(s);
  tssrc_close(s);
  script_stop(&sc);

  memset(&sc, 0, sizeof sc);
  fill_short_unknown(&sc);
  script_start(&sc);
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/s", sc.port);
  ck_assert_int_eq(http_url_parse(uri, &cfg.http), 0);
  ck_assert_ptr_null(tssrc_open_blocking(&cfg, 5.0, &reason));
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
  script_stop(&sc);

  ck_assert_int_eq(http_url_parse("http://127.0.0.1:1/s", &cfg.http), 0);
  ck_assert_ptr_null(tssrc_open_blocking(&cfg, 5.0, &reason));
  ck_assert_ptr_null(tssrc_open_blocking(&cfg, 5.0, NULL));
}
END_TEST

START_TEST(blocking_open_times_out_on_silent_server) {
  int lfd;
  unsigned port;
  tssrc_cfg_t cfg;
  net_err_reason_t reason = NET_ERR_OTHER;
  char uri[96];
  lfd = make_listener(&port);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_HTTP;
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/s", port);
  ck_assert_int_eq(http_url_parse(uri, &cfg.http), 0);
  ck_assert_ptr_null(tssrc_open_blocking(&cfg, 0.3, &reason));
  ck_assert_int_eq(reason, NET_ERR_TIMEOUT);
  close(lfd);
}
END_TEST

static Suite *open_async_suite(void) {
  Suite *s = suite_create("open_async");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 60);
  tcase_add_loop_test(tc, open_ok_cases, 0, 6);
  tcase_add_loop_test(tc, open_error_cases, 0, 9);
  tcase_add_test(tc, non_http_kinds_open_synchronously);
  tcase_add_test(tc, blocking_open_forms);
  tcase_add_test(tc, blocking_open_times_out_on_silent_server);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(open_async_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
