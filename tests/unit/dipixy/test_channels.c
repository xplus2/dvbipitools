/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dipixy/args.h"
#include "dipixy/ts/capture/capture.h"
#include "dipixy/ts/channels/channels.h"
#include "lib/helper/sds_xml.h"

static void write_temp_file(char *path, const char *content) {
  char tmpl[] = "/tmp/dvbipitools_test_channels_XXXXXX.m3u";
  int fd;
  FILE *f;
  strcpy(path, tmpl);
  fd = mkstemps(path, 4);
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  fputs(content, f);
  fclose(f);
}

typedef struct {
  int listen_fd;
  volatile int accepts;
} playlist_http_server_t;

static void *playlist_http_server_thread(void *arg) {
  playlist_http_server_t *s = arg;
  static const char resp[] = "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\n\r\n";
  for (;;) {
    int fd = accept(s->listen_fd, NULL, NULL);
    if (fd < 0)
      return NULL;
    s->accepts++;
    send(fd, resp, sizeof resp - 1, MSG_NOSIGNAL);
  }
}

static unsigned start_local_http_server(int *listen_fd_out, pthread_t *tid_out, playlist_http_server_t *srv) {
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof addr;
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

  ck_assert_int_ge(listen_fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(listen_fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(listen_fd, 4), 0);
  ck_assert_int_eq(getsockname(listen_fd, (struct sockaddr *)&addr, &addrlen), 0);

  srv->listen_fd = listen_fd;
  srv->accepts = 0;
  ck_assert_int_eq(pthread_create(tid_out, NULL, playlist_http_server_thread, srv), 0);
  *listen_fd_out = listen_fd;
  return (unsigned)ntohs(addr.sin_port);
}

static void stop_local_http_server(int listen_fd, pthread_t tid) {
  pthread_cancel(tid);
  pthread_join(tid, NULL);
  close(listen_fd);
}

static channels_t *build_single_m3u_list(const char *path) {
  config_t cfg;
  source_def_t src;

  memset(&cfg, 0, sizeof cfg);
  memset(&src, 0, sizeof src);
  src.kind = SRC_M3U;
  src.value = path;
  src.ordinal = 1;
  cfg.sources = &src;
  cfg.n_sources = 1;
  return channels_build(&cfg);
}

START_TEST(mixed_scheme_playlist_opens_static_ctx_per_scheme) {
  int listen_fd;
  pthread_t tid;
  playlist_http_server_t srv;
  unsigned http_port = start_local_http_server(&listen_fd, &tid, &srv);
  char path[160], content[512];
  channels_t *ch;
  capture_ctx_t *http_ctx, *srt_ctx, *rtp_ctx;
  int family, rtp;
  char addr[64];
  unsigned port;

  snprintf(content, sizeof content,
           "#EXTINF:-1,RTP Channel\n"
           "rtp://@239.1.1.1:5000\n"
           "#EXTINF:-1,HTTP Channel\n"
           "http://127.0.0.1:%u/stream\n"
           "#EXTINF:-1,SRT Channel\n"
           "srt://192.0.2.1:9000\n",
           http_port);
  write_temp_file(path, content);

  ch = build_single_m3u_list(path);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  /* rtp entry: resolved per-request via channels_resolve(), no static_ctx */
  rtp_ctx = channels_resolve_static(ch, 1, 1, NULL);
  ck_assert_ptr_null(rtp_ctx);
  ck_assert_int_eq(channels_resolve(ch, 1, 1, NULL, &family, addr, sizeof addr, &port, &rtp, NULL), 0);
  ck_assert_str_eq(addr, "239.1.1.1");
  ck_assert_uint_eq(port, 5000u);
  ck_assert_int_eq(rtp, 1);

  /* http entry: opened eagerly, static_ctx set */
  http_ctx = channels_resolve_static(ch, 1, 2, NULL);
  ck_assert_ptr_nonnull(http_ctx);
  capture_close(http_ctx);

  /* srt entry: no libsrt in this build (srtsrc_stub always fails), stays listed,
     static_ctx NULL, unresolvable either way */
  srt_ctx = channels_resolve_static(ch, 1, 3, NULL);
  ck_assert_ptr_null(srt_ctx);
  ck_assert_int_ne(channels_resolve(ch, 1, 3, NULL, &family, addr, sizeof addr, &port, &rtp, NULL), 0);

  channels_free(ch);
  stop_local_http_server(listen_fd, tid);
}
END_TEST

START_TEST(sighup_reload_reuses_http_connection_via_dedup) {
  int listen_fd;
  pthread_t tid;
  playlist_http_server_t srv;
  unsigned http_port = start_local_http_server(&listen_fd, &tid, &srv);
  char path[160], content[256];
  config_t cfg;
  source_def_t src;
  channels_t *ch;
  capture_ctx_t *before, *after;

  snprintf(content, sizeof content,
           "#EXTINF:-1,HTTP Channel\n"
           "http://127.0.0.1:%u/stream\n",
           http_port);
  write_temp_file(path, content);

  memset(&cfg, 0, sizeof cfg);
  memset(&src, 0, sizeof src);
  src.kind = SRC_M3U;
  src.value = path;
  src.ordinal = 1;
  cfg.sources = &src;
  cfg.n_sources = 1;

  ch = channels_build(&cfg);
  ck_assert_ptr_nonnull(ch);
  before = channels_resolve_static(ch, 1, 1, NULL);
  ck_assert_ptr_nonnull(before);
  capture_close(before);

  channels_reload_all(ch, &cfg);

  after = channels_resolve_static(ch, 1, 1, NULL);
  ck_assert_ptr_nonnull(after);
  ck_assert_ptr_eq(before, after); /* same ctx: reload deduped, didn't reconnect */
  ck_assert_int_eq(srv.accepts, 1); /* one TCP connect total, across build + reload */
  capture_close(after);

  unlink(path);
  channels_free(ch);
  stop_local_http_server(listen_fd, tid);
}
END_TEST

typedef struct {
  char names[8][64];
  int count;
} collected_names_t;

static void collect_name(void *ctx, const channel_item_t *item) {
  collected_names_t *c = ctx;
  strncpy(c->names[c->count], item->name, sizeof c->names[0] - 1);
  c->count++;
}

START_TEST(srt_open_failure_keeps_entry_in_list) {
  char path[160], content[256];
  channels_t *ch;
  collected_names_t collected = {{{0}}, 0};

  snprintf(content, sizeof content,
           "#EXTINF:-1,RTP Channel\n"
           "rtp://@239.1.1.1:5000\n"
           "#EXTINF:-1,SRT Channel\n"
           "srt://192.0.2.1:9000\n");
  write_temp_file(path, content);

  ch = build_single_m3u_list(path);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  ck_assert_int_eq(channels_list_for_each(ch, 1, collect_name, &collected), 2);
  ck_assert_str_eq(collected.names[0], "RTP Channel");
  ck_assert_str_eq(collected.names[1], "SRT Channel");

  channels_free(ch);
}
END_TEST

typedef struct {
  unsigned tsid, onid, sid;
  int calls;
} collected_triplet_t;

static void collect_triplet(void *ctx, const channel_item_t *item) {
  collected_triplet_t *c = ctx;
  c->tsid = item->tsid;
  c->onid = item->onid;
  c->sid = item->sid;
  c->calls++;
}

START_TEST(list_for_each_passes_dvb_triplet_through) {
  char path[160];
  channels_t *ch;
  collected_triplet_t collected = {0, 0, 0, 0};

  write_temp_file(path, "#EXTINF:-1 tsid=\"11\" onid=\"22\" sid=\"33\",Triplet Channel\nrtp://@239.1.1.1:5000\n");

  ch = build_single_m3u_list(path);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  ck_assert_int_eq(channels_list_for_each(ch, 1, collect_triplet, &collected), 1);
  ck_assert_int_eq(collected.calls, 1);
  ck_assert_uint_eq(collected.tsid, 11u);
  ck_assert_uint_eq(collected.onid, 22u);
  ck_assert_uint_eq(collected.sid, 33u);

  channels_free(ch);
}
END_TEST

typedef struct {
  int has_fcc;
  sds_fcc_t fcc;
  int calls;
} collected_fcc_t;

static void collect_fcc(void *ctx, const channel_item_t *item) {
  collected_fcc_t *c = ctx;
  c->has_fcc = item->has_fcc;
  c->fcc = item->fcc;
  c->calls++;
}

START_TEST(list_for_each_passes_fcc_through) {
  config_t cfg;
  source_def_t src;
  sds_service_t svc;
  sds_fcc_t fcc;
  unsigned char buf[4096];
  size_t len;
  char path[160];
  FILE *f;
  channels_t *ch;
  collected_fcc_t collected;

  memset(&collected, 0, sizeof collected);

  memset(&svc, 0, sizeof svc);
  snprintf(svc.name, sizeof svc.name, "FCC Channel");
  snprintf(svc.address, sizeof svc.address, "239.1.1.1");
  svc.port = 5000;

  memset(&fcc, 0, sizeof fcc);
  snprintf(fcc.addr, sizeof fcc.addr, "10.0.0.2");
  fcc.port = 7000;
  fcc.rtx_time_ms = 3000;
  fcc.rtx_pt = 98;

  len = sds_build_broadcast("example.invalid", 1, &svc, 1, NULL, &fcc, buf, sizeof buf);
  ck_assert_uint_gt(len, 0u);

  {
    char tmpl[] = "/tmp/dvbipitools_test_channels_XXXXXX.xml";
    int fd;
    strcpy(path, tmpl);
    fd = mkstemps(path, 4);
    ck_assert_int_ge(fd, 0);
    f = fdopen(fd, "w");
    fwrite(buf, 1, len, f);
    fclose(f);
  }

  memset(&cfg, 0, sizeof cfg);
  memset(&src, 0, sizeof src);
  src.kind = SRC_XML;
  src.value = path;
  src.ordinal = 1;
  cfg.sources = &src;
  cfg.n_sources = 1;
  ch = channels_build(&cfg);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  ck_assert_int_eq(channels_list_for_each(ch, 1, collect_fcc, &collected), 1);
  ck_assert_int_eq(collected.calls, 1);
  ck_assert_int_eq(collected.has_fcc, 1);
  ck_assert_str_eq(collected.fcc.addr, "10.0.0.2");
  ck_assert_uint_eq(collected.fcc.port, 7000u);
  ck_assert_uint_eq(collected.fcc.rtx_time_ms, 3000u);
  ck_assert_uint_eq(collected.fcc.rtx_pt, 98u);

  channels_free(ch);
}
END_TEST

START_TEST(channels_resolve_passes_ret_and_fcc_through) {
  config_t cfg;
  source_def_t src;
  sds_service_t svc;
  sds_ret_t ret;
  sds_fcc_t fcc;
  unsigned char buf[4096];
  size_t len;
  char path[160];
  FILE *f;
  channels_t *ch;
  channel_ret_fcc_t rf;
  int family, rtp;
  char addr[64];
  unsigned port;

  memset(&svc, 0, sizeof svc);
  snprintf(svc.name, sizeof svc.name, "Both Channel");
  snprintf(svc.address, sizeof svc.address, "239.1.1.2");
  svc.port = 5001;

  memset(&ret, 0, sizeof ret);
  snprintf(ret.addr, sizeof ret.addr, "10.0.0.1");
  ret.port = 6000;
  ret.rtx_time_ms = 2000;
  ret.rtx_pt = 99;

  memset(&fcc, 0, sizeof fcc);
  snprintf(fcc.addr, sizeof fcc.addr, "10.0.0.2");
  fcc.port = 7000;
  fcc.rtx_time_ms = 3000;
  fcc.rtx_pt = 98;

  len = sds_build_broadcast("example.invalid", 1, &svc, 1, &ret, &fcc, buf, sizeof buf);
  ck_assert_uint_gt(len, 0u);

  {
    char tmpl[] = "/tmp/dvbipitools_test_channels_XXXXXX.xml";
    int fd;
    strcpy(path, tmpl);
    fd = mkstemps(path, 4);
    ck_assert_int_ge(fd, 0);
    f = fdopen(fd, "w");
    fwrite(buf, 1, len, f);
    fclose(f);
  }

  memset(&cfg, 0, sizeof cfg);
  memset(&src, 0, sizeof src);
  src.kind = SRC_XML;
  src.value = path;
  src.ordinal = 1;
  cfg.sources = &src;
  cfg.n_sources = 1;
  ch = channels_build(&cfg);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  memset(&rf, 0, sizeof rf);
  ck_assert_int_eq(channels_resolve(ch, 1, 1, NULL, &family, addr, sizeof addr, &port, &rtp, &rf), 0);
  ck_assert_int_eq(rf.has_ret, 1);
  ck_assert_str_eq(rf.ret.addr, "10.0.0.1");
  ck_assert_uint_eq(rf.ret.port, 6000u);
  ck_assert_int_eq(rf.has_fcc, 1);
  ck_assert_str_eq(rf.fcc.addr, "10.0.0.2");
  ck_assert_uint_eq(rf.fcc.port, 7000u);

  channels_free(ch);
}
END_TEST

static Suite *channels_suite(void) {
  Suite *s = suite_create("dipixy_channels");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, mixed_scheme_playlist_opens_static_ctx_per_scheme);
  tcase_add_test(tc, sighup_reload_reuses_http_connection_via_dedup);
  tcase_add_test(tc, srt_open_failure_keeps_entry_in_list);
  tcase_add_test(tc, list_for_each_passes_dvb_triplet_through);
  tcase_add_test(tc, list_for_each_passes_fcc_through);
  tcase_add_test(tc, channels_resolve_passes_ret_and_fcc_through);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(channels_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
