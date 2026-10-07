/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <check.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/sys/signal.h"
#include "../log_capture.h"
#include "dipirec/config.h"
#include "dipirec/filter/pace.h"
#include "lib/demux/crc32.h"
#include "lib/demux/psi/psi.h"
#include "lib/mux/psi_build.h"
#include "lib/net/multicast.h"
#include "dipirec/record.h"
#include "dipirec/record/priv.h"
#include "../lib/mux/ts_test_util.h"
#define sink_open metrics_sink_open
#define sink_close metrics_sink_close
#include "../metrics_sink.h"
#undef sink_open
#undef sink_close

START_TEST(fmt_dur_under_an_hour_omits_hours) {
  char buf[16];
  fmt_dur(65.0, buf, sizeof buf); /* 1:05 */
  ck_assert_str_eq(buf, " 1:05");
}
END_TEST

START_TEST(fmt_dur_with_hours_includes_them) {
  char buf[16];
  fmt_dur(3725.0, buf, sizeof buf); /* 1:02:05 */
  ck_assert_str_eq(buf, " 1:02:05");
}
END_TEST

START_TEST(fmt_dur_zero_and_negative_clamp_to_zero) {
  char buf[16];
  fmt_dur(0.0, buf, sizeof buf);
  ck_assert_str_eq(buf, " 0:00");
  fmt_dur(-5.0, buf, sizeof buf);
  ck_assert_str_eq(buf, " 0:00");
}
END_TEST

START_TEST(fmt_dur_caps_at_99_59_59) {
  char buf[16];
  fmt_dur(1000000.0, buf, sizeof buf);
  ck_assert_str_eq(buf, "99:59:59");
}
END_TEST

START_TEST(stop_now_false_before_duration_elapses) {
  config_t cfg;
  double start = mono_seconds();
  memset(&cfg, 0, sizeof cfg);
  cfg.duration_s = 3600; /* an hour: nowhere near elapsed yet */
  ck_assert_int_eq(stop_now(&cfg, start), 0);
}
END_TEST

START_TEST(stop_now_true_once_duration_elapses) {
  config_t cfg;
  double start;
  struct timespec wait = {0, 20 * 1000 * 1000}; /* 20ms */
  memset(&cfg, 0, sizeof cfg);
  cfg.duration_s = 1; /* smallest representable unit; 0 means "run forever" */
  start = mono_seconds() - 2.0; /* pretend we started 2 real seconds ago */
  nanosleep(&wait, NULL);
  ck_assert_int_eq(stop_now(&cfg, start), 1);
}
END_TEST

START_TEST(stop_now_false_forever_when_duration_is_zero) {
  config_t cfg;
  double start = mono_seconds() - 1000000.0; /* absurdly long ago */
  memset(&cfg, 0, sizeof cfg);
  cfg.duration_s = 0; /* until stopped */
  ck_assert_int_eq(stop_now(&cfg, start), 0);
}
END_TEST

START_TEST(stop_now_true_on_stop_signal_regardless_of_duration) {
  config_t cfg;
  double start = mono_seconds();
  memset(&cfg, 0, sizeof cfg);
  cfg.duration_s = 3600;
  signals_install();
  raise(SIGTERM); /* Check forks each test, so this only affects this test's process */
  ck_assert_int_eq(stop_now(&cfg, start), 1);
}
END_TEST

#define TS_PKTS 4

static void make_ts_file(char *path, size_t cap) {
  unsigned char pkt[188];
  int fd;

  snprintf(path, cap, "/tmp/dipirec_src_XXXXXX");
  fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  for (int i = 0; i < TS_PKTS; i++) ck_assert_int_eq((int)write(fd, pkt, sizeof pkt), (int)sizeof pkt);
  close(fd);
}

static unsigned rec_free_udp_port(void) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  ck_assert_int_ge(fd, 0);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &len), 0);
  port = ntohs(a.sin_port);
  close(fd);
  return port;
}

static size_t drain_source(src_t *s) {
  unsigned char buf[2048];
  size_t total = 0;

  for (int i = 0; i < 20; i++) {
    ssize_t n = src_read(s, buf, sizeof buf);

    if (n < 0) break;
    ck_assert_int_eq((int)(n % 188), 0);
    for (ssize_t off = 0; off < n; off += 188) ck_assert_uint_eq(buf[off], 0x47);
    total += (size_t)n;
  }
  return total;
}

START_TEST(src_open_reads_a_file) {
  config_t cfg;
  src_t s;
  char path[64];

  make_ts_file(path, sizeof path);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, path), 0);
  ck_assert_int_eq(src_open(&cfg, &s), 0);
  ck_assert_int_eq(s.kind, URI_FILE);
  ck_assert_int_eq(src_wait_readable(&s, 1000), 1);
  ck_assert_uint_eq(drain_source(&s), (size_t)TS_PKTS * 188);
  src_close(&s);
  unlink(path);
}
END_TEST

START_TEST(src_open_missing_file_fails) {
  config_t cfg;
  src_t s;

  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, "/nonexistent-dir-dipirec/in.ts"), 0);
  ck_assert_int_eq(src_open(&cfg, &s), -1);
}
END_TEST

START_TEST(src_open_reads_stdin) {
  config_t cfg;
  src_t s;
  char path[64];
  int fd;

  make_ts_file(path, sizeof path);
  fd = open(path, O_RDONLY);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_ge(dup2(fd, STDIN_FILENO), 0);
  close(fd);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, "-"), 0);
  ck_assert_int_eq(src_open(&cfg, &s), 0);
  ck_assert_uint_eq(drain_source(&s), (size_t)TS_PKTS * 188);
  src_close(&s);
  unlink(path);
}
END_TEST

START_TEST(src_open_joins_udp_and_rtp_groups) {
  static const char *const schemes[] = {"udp", "rtp"};
  config_t cfg;
  src_t s;
  char group[32];
  char uri[96];
  struct sockaddr_in dst;
  unsigned char dgram[12 + 188];
  unsigned char buf[2048];
  unsigned port = rec_free_udp_port();
  int tx = socket(AF_INET, SOCK_DGRAM, 0);
  size_t hdr = _i ? 12 : 0;

  ck_assert_int_ge(tx, 0);
  snprintf(group, sizeof group, "239.80.%u.%u", ((unsigned)getpid() >> 8) & 0xFF, 1 + ((unsigned)getpid() & 0xFF) % 250);
  snprintf(uri, sizeof uri, "%s://@%s:%u", schemes[_i], group, port);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, uri), 0);
  ck_assert_int_eq(src_open(&cfg, &s), 0);
  ck_assert_int_eq(src_wait_readable(&s, 0), 0);

  memset(dgram, 0xFF, sizeof dgram);
  if (hdr) {
    dgram[0] = 0x80;
    dgram[1] = 33;
    dgram[11] = 9;
  }
  dgram[hdr] = 0x47;
  memset(&dst, 0, sizeof dst);
  dst.sin_family = AF_INET;
  dst.sin_port = htons((unsigned short)port);
  ck_assert_int_eq(inet_pton(AF_INET, group, &dst.sin_addr), 1);
  ck_assert_int_eq((int)sendto(tx, dgram, hdr + 188, 0, (struct sockaddr *)&dst, sizeof dst), (int)(hdr + 188));
  ck_assert_int_eq(src_wait_readable(&s, 1000), 1);
  ck_assert_int_eq((int)src_read(&s, buf, sizeof buf), 188);
  ck_assert_uint_eq(buf[0], 0x47);
  src_close(&s);
  close(tx);
}
END_TEST

typedef struct {
  int listen_fd;
  int status;
} rec_http_srv_t;

static void *rec_http_thread(void *arg) {
  const rec_http_srv_t *srv = arg;
  int cfd = accept(srv->listen_fd, NULL, NULL);
  char req[1024];
  char head[128];
  unsigned char pkt[188];
  size_t got = 0;
  if (cfd < 0) return NULL;
  while (got < sizeof req - 1) {
    ssize_t n = recv(cfd, req + got, sizeof req - 1 - got, 0);
    if (n <= 0) break;
    got += (size_t)n;
    req[got] = '\0';
    if (strstr(req, "\r\n\r\n")) break;
  }
  if (srv->status == 200)
    snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nConnection: close\r\n\r\n");
  else
    snprintf(head, sizeof head, "HTTP/1.1 %d X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", srv->status);
  send(cfd, head, strlen(head), MSG_NOSIGNAL);
  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  if (srv->status == 200)
    for (int i = 0; i < TS_PKTS; i++) send(cfd, pkt, sizeof pkt, MSG_NOSIGNAL);
  close(cfd);
  return NULL;
}

typedef struct {
  int status;
  int open_ret;
} http_case_t;

static const http_case_t http_cases[] = {{200, 0}, {404, -1}};

START_TEST(src_open_reads_http_and_reports_errors) {
  rec_http_srv_t srv;
  pthread_t th;
  config_t cfg;
  src_t s;
  char uri[64];
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  srv.status = http_cases[_i].status;
  srv.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  ck_assert_int_ge(srv.listen_fd, 0);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(srv.listen_fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(listen(srv.listen_fd, 1), 0);
  ck_assert_int_eq(getsockname(srv.listen_fd, (struct sockaddr *)&a, &len), 0);
  ck_assert_int_eq(pthread_create(&th, NULL, rec_http_thread, &srv), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/live.ts", ntohs(a.sin_port));
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, uri), 0);
  ck_assert_int_eq(src_open(&cfg, &s), http_cases[_i].open_ret);
  if (!http_cases[_i].open_ret) {
    ck_assert_int_eq(s.kind, URI_HTTP);
    ck_assert_uint_eq(drain_source(&s), (size_t)TS_PKTS * 188);
    src_close(&s);
  }
  pthread_join(th, NULL);
  close(srv.listen_fd);
}
END_TEST

typedef struct {
  const char *addr;
  int mc_enabled;
  int open_ret;
} ret_case_t;

static const ret_case_t ret_cases[] = {
  {"127.0.0.1", 0, 0},
  {"not-an-address", 0, -1},
  {"127.0.0.1", 1, 0},
};

START_TEST(src_open_with_ret_wires_client_and_unwinds_on_failure) {
  const ret_case_t *c = &ret_cases[_i];
  config_t cfg;
  src_t s;
  char uri[96];
  snprintf(uri, sizeof uri, "rtp://@239.81.%u.%u:%u", ((unsigned)getpid() >> 8) & 0xFF, 1 + ((unsigned)getpid() & 0xFF) % 250, rec_free_udp_port());
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, uri), 0);
  cfg.ret.enabled = 1;
  cfg.ret.family = AF_INET;
  snprintf(cfg.ret.addr, sizeof cfg.ret.addr, "%s", c->addr);
  cfg.ret.port = rec_free_udp_port();
  cfg.ret.mc_enabled = c->mc_enabled;
  ck_assert_int_eq(src_open(&cfg, &s), c->open_ret);
  if (!c->open_ret) {
    ck_assert_ptr_nonnull(s.ret);
    ck_assert_int_eq(src_wait_readable(&s, 0), 1);
    src_close(&s);
  }
}
END_TEST

static void noop_alarm(int sig) {
  (void)sig;
}

START_TEST(src_wait_readable_returns_zero_when_interrupted) {
  config_t cfg;
  src_t s;
  char uri[96];
  struct sigaction sa;
  struct itimerval it;

  snprintf(uri, sizeof uri, "udp://@239.83.%u.%u:%u", ((unsigned)getpid() >> 8) & 0xFF, 1 + ((unsigned)getpid() & 0xFF) % 250, rec_free_udp_port());
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, uri), 0);
  ck_assert_int_eq(src_open(&cfg, &s), 0);
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = noop_alarm;
  ck_assert_int_eq(sigaction(SIGALRM, &sa, NULL), 0);
  memset(&it, 0, sizeof it);
  it.it_value.tv_usec = 50000;
  ck_assert_int_eq(setitimer(ITIMER_REAL, &it, NULL), 0);
  {
    struct timespec t0;
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    ck_assert_int_eq(src_wait_readable(&s, 5000), 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ck_assert_double_lt((double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9, 2.0);
  }
  src_close(&s);
}
END_TEST

START_TEST(src_wait_readable_reports_poll_failure) {
  config_t cfg;
  src_t s;
  char path[64];
  struct rlimit saved;
  struct rlimit rl;
  int rc;

  make_ts_file(path, sizeof path);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, path), 0);
  ck_assert_int_eq(src_open(&cfg, &s), 0);
  ck_assert_int_eq(getrlimit(RLIMIT_NOFILE, &saved), 0);
  rl.rlim_cur = 0;
  rl.rlim_max = saved.rlim_max;
  ck_assert_int_eq(setrlimit(RLIMIT_NOFILE, &rl), 0);
  rc = src_wait_readable(&s, 0);
  ck_assert_int_eq(setrlimit(RLIMIT_NOFILE, &saved), 0);
  src_close(&s);
  ck_assert_int_eq(rc, -1);
  unlink(path);
}
END_TEST

static size_t file_size_of(const char *path, unsigned char *buf, size_t cap) {
  int fd = open(path, O_RDONLY);
  ssize_t n;

  ck_assert_int_ge(fd, 0);
  n = read(fd, buf, cap);
  ck_assert_int_ge((int)n, 0);
  close(fd);
  return (size_t)n;
}

START_TEST(sink_writes_a_file) {
  config_t cfg;
  out_sink_t o;
  char path[64];
  unsigned char data[600];
  unsigned char back[1024];

  make_ts_file(path, sizeof path);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, path), 0);
  for (size_t i = 0; i < sizeof data; i++) data[i] = (unsigned char)(i * 5 + 1);
  ck_assert_int_eq(sink_open(&cfg, &cfg.out[0], &o), 0);
  ck_assert_int_eq(o.kind, OUT_FILE);
  ck_assert_int_eq(sink_write(&o, data, sizeof data), 0);
  ck_assert_int_eq(sink_write(&o, data, 100), 0);
  sink_close(&o);
  ck_assert_uint_eq(file_size_of(path, back, sizeof back), 700u);
  ck_assert_int_eq(memcmp(back, data, sizeof data), 0);
  ck_assert_int_eq(memcmp(back + 600, data, 100), 0);
  unlink(path);
}
END_TEST

START_TEST(sink_open_unwritable_file_fails) {
  config_t cfg;
  out_sink_t o;

  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, "/nonexistent-dir-dipirec/out.ts"), 0);
  ck_assert_int_eq(sink_open(&cfg, &cfg.out[0], &o), -1);
}
END_TEST

START_TEST(sink_stdout_writes_to_fd_one_and_stays_open) {
  config_t cfg;
  out_sink_t o;
  char path[64];
  unsigned char data[300];
  unsigned char back[400];
  int saved = dup(STDOUT_FILENO);
  int fd;

  ck_assert_int_ge(saved, 0);
  make_ts_file(path, sizeof path);
  fd = open(path, O_WRONLY | O_TRUNC);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_ge(dup2(fd, STDOUT_FILENO), 0);
  close(fd);
  memset(data, 0x5A, sizeof data);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, "-"), 0);
  ck_assert_int_eq(sink_open(&cfg, &cfg.out[0], &o), 0);
  ck_assert_int_eq(o.fd, STDOUT_FILENO);
  ck_assert_int_eq(sink_write(&o, data, sizeof data), 0);
  sink_close(&o);
  ck_assert_int_eq((int)write(STDOUT_FILENO, "Z", 1), 1);
  ck_assert_int_ge(dup2(saved, STDOUT_FILENO), 0);
  close(saved);
  ck_assert_uint_eq(file_size_of(path, back, sizeof back), sizeof data + 1);
  ck_assert_int_eq(memcmp(back, data, sizeof data), 0);
  ck_assert_uint_eq(back[sizeof data], 'Z');
  unlink(path);
}
END_TEST

START_TEST(sink_sends_udp_and_rtp_datagrams) {
  static const char *const schemes[] = {"udp", "rtp"};
  config_t cfg;
  out_sink_t o;
  char group[32];
  char uri[96];
  unsigned char data[2 * 1316 + 188];
  unsigned char buf[2048];
  unsigned port = rec_free_udp_port();
  size_t hdr = _i ? 12 : 0;
  size_t expect[3] = {1316 + hdr, 1316 + hdr, 188 + hdr};
  mcast_t *rx;

  snprintf(group, sizeof group, "239.85.%u.%u", ((unsigned)getpid() >> 8) & 0xFF, 1 + ((unsigned)getpid() & 0xFF) % 250);
  snprintf(uri, sizeof uri, "%s://%s:%u", schemes[_i], group, port);
  rx = mcast_open(AF_INET, group, port, NULL, 1000);
  ck_assert_ptr_nonnull(rx);
  for (size_t i = 0; i < sizeof data; i++) data[i] = (unsigned char)(i * 3 + 7);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, uri), 0);
  ck_assert_int_eq(sink_open(&cfg, &cfg.out[0], &o), 0);
  ck_assert_int_eq(o.kind, _i ? OUT_RTP : OUT_UDP);
  ck_assert_int_eq(sink_write(&o, data, sizeof data), 0);
  ck_assert_int_eq(o.net_had_error, 0);
  ck_assert_uint_eq(o.errors_total, 0u);
  for (int i = 0; i < 3; i++) {
    ssize_t n = mcast_recv(rx, buf, sizeof buf, NULL);
    size_t off = (size_t)i < 2 ? (size_t)i * 1316 : 2 * 1316;

    ck_assert_int_eq((int)n, (int)expect[i]);
    ck_assert_int_eq(memcmp(buf + hdr, data + off, expect[i] - hdr), 0);
    if (hdr) ck_assert_uint_eq(buf[0], 0x80);
  }
  sink_close(&o);
  mcast_close(rx);
}
END_TEST

START_TEST(sink_write_to_full_device_fails) {
  config_t cfg;
  out_sink_t o;
  unsigned char data[188] = {0x47};
  char msg[512];
  int rc;

  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, "/dev/full"), 0);
  ck_assert_int_eq(sink_open(&cfg, &cfg.out[0], &o), 0);
  log_capture_begin();
  rc = sink_write(&o, data, sizeof data);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, -1);
  ck_assert_ptr_nonnull(strstr(msg, "w:"));
  sink_close(&o);
}
END_TEST

START_TEST(note_send_result_logs_only_on_failure_and_recovery_edges) {
  int had_error = 0;
  uint64_t total = 0;
  char msg[1024];

  log_capture_begin();
  note_send_result(1, &had_error, &total, "net");
  note_send_result(0, &had_error, &total, "net");
  note_send_result(0, &had_error, &total, "net");
  note_send_result(0, &had_error, &total, "net");
  note_send_result(1, &had_error, &total, "net");
  note_send_result(1, &had_error, &total, "net");
  note_send_result(0, &had_error, &total, "net");
  log_capture_end(msg, sizeof msg);
  ck_assert_uint_eq(total, 4u);
  ck_assert_int_eq(had_error, 1);
  ck_assert_int_eq(log_count_of(msg, "net output: write failed, will keep retrying"), 2);
  ck_assert_int_eq(log_count_of(msg, "net output: recovered"), 1);
}
END_TEST

static unsigned closed_tcp_port(void) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  unsigned port;

  ck_assert_int_ge(fd, 0);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &len), 0);
  port = ntohs(a.sin_port);
  close(fd);
  return port;
}

START_TEST(rtmp_fanout_open_stops_at_a_failing_target) {
  config_t cfg;
  rtmp_fanout_t r;
  char good[64];

  snprintf(good, sizeof good, "rtmp://127.0.0.1:%u/live/key", closed_tcp_port());
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, "out.ts"), 0);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, good), 0);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, "rtmp://127.0.0.1:99999/live/key"), 0);
  ck_assert_int_eq(rtmp_fanout_open(&cfg, &r), -1);
  ck_assert_int_eq(r.n, 1);
  rtmp_fanout_close(&r);
}
END_TEST

typedef struct {
  int listen_fd;
  atomic_size_t got;
} rtmp_capture_t;

static void *rtmp_capture_thread(void *arg) {
  rtmp_capture_t *c = arg;
  int cfd = accept(c->listen_fd, NULL, NULL);
  struct timeval tv = {3, 0};
  unsigned char buf[2048];
  ssize_t n;

  if (cfd < 0) return NULL;
  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  n = recv(cfd, buf, sizeof buf, 0);
  if (n > 0) c->got = (size_t)n;
  close(cfd);
  return NULL;
}

START_TEST(rtmp_fanout_cb_reaches_every_target) {
  config_t cfg;
  rtmp_fanout_t r;
  rtmp_capture_t cap[2];
  pthread_t th[2];
  unsigned char hdr[5] = {0x17, 0x01, 0x00, 0x00, 0x00};
  unsigned char payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  char url[64];

  rec_cfg_defaults(&cfg);
  for (int i = 0; i < 2; i++) {
    struct sockaddr_in a;
    socklen_t len = sizeof a;

    cap[i].got = 0;
    cap[i].listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    ck_assert_int_ge(cap[i].listen_fd, 0);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ck_assert_int_eq(bind(cap[i].listen_fd, (struct sockaddr *)&a, sizeof a), 0);
    ck_assert_int_eq(listen(cap[i].listen_fd, 1), 0);
    ck_assert_int_eq(getsockname(cap[i].listen_fd, (struct sockaddr *)&a, &len), 0);
    snprintf(url, sizeof url, "rtmp://127.0.0.1:%u/live/key%d", ntohs(a.sin_port), i);
    ck_assert_int_eq(rec_cfg_add_out(&cfg, url), 0);
    ck_assert_int_eq(pthread_create(&th[i], NULL, rtmp_capture_thread, &cap[i]), 0);
  }
  ck_assert_int_eq(rtmp_fanout_open(&cfg, &r), 0);
  ck_assert_int_eq(r.n, 2);
  for (int i = 0; i < 400 && (!cap[0].got || !cap[1].got); i++) {
    struct timespec ts = {0, 5000000L};

    rtmp_fanout_cb(&r, FLV_TAG_VIDEO, (uint32_t)i * 40, hdr, sizeof hdr, payload, sizeof payload);
    nanosleep(&ts, NULL);
  }
  rtmp_fanout_close(&r);
  for (int i = 0; i < 2; i++) {
    pthread_join(th[i], NULL);
    close(cap[i].listen_fd);
    ck_assert_uint_gt(cap[i].got, 0u);
  }
}
END_TEST

START_TEST(rtmp_fanout_counts_errors_per_target) {
  config_t cfg;
  rtmp_fanout_t r;
  unsigned char hdr[5] = {0x17, 0x01, 0x00, 0x00, 0x00};
  unsigned char payload[4] = {1, 2, 3, 4};
  char url[64];
  char msg[1024];

  rec_cfg_defaults(&cfg);
  for (int i = 0; i < 2; i++) {
    snprintf(url, sizeof url, "rtmp://127.0.0.1:%u/live/key%d", closed_tcp_port(), i);
    ck_assert_int_eq(rec_cfg_add_out(&cfg, url), 0);
  }
  ck_assert_int_eq(rtmp_fanout_open(&cfg, &r), 0);
  log_capture_begin();
  for (int i = 0; i < 5; i++) rtmp_fanout_cb(&r, FLV_TAG_VIDEO, 0, hdr, sizeof hdr, payload, sizeof payload);
  log_capture_end(msg, sizeof msg);
  for (int i = 0; i < 2; i++) {
    ck_assert_uint_eq(r.errors_total[i], 5u);
    ck_assert_int_eq(r.had_error[i], 1);
  }
  ck_assert_int_eq(log_count_of(msg, "rtmp[0] output: write failed"), 1);
  ck_assert_int_eq(log_count_of(msg, "rtmp[1] output: write failed"), 1);
  rtmp_fanout_close(&r);
}
END_TEST

typedef struct {
  unsigned char type;
  unsigned pid;
  const unsigned char *desc;
  size_t dlen;
} es_spec_t;

static size_t build_pmt_specs(unsigned char *out, unsigned prog, unsigned pcr, const es_spec_t *es, size_t n) {
  size_t o = 3;
  uint32_t crc;

  out[0] = 0x02;
  out[o++] = (unsigned char)(prog >> 8);
  out[o++] = (unsigned char)prog;
  out[o++] = 0xC1;
  out[o++] = 0x00;
  out[o++] = 0x00;
  out[o++] = (unsigned char)(0xE0 | ((pcr >> 8) & 0x1F));
  out[o++] = (unsigned char)pcr;
  out[o++] = 0xF0;
  out[o++] = 0x00;
  for (size_t i = 0; i < n; i++) {
    out[o++] = es[i].type;
    out[o++] = (unsigned char)(0xE0 | ((es[i].pid >> 8) & 0x1F));
    out[o++] = (unsigned char)es[i].pid;
    out[o++] = (unsigned char)(0xF0 | ((es[i].dlen >> 8) & 0x0F));
    out[o++] = (unsigned char)es[i].dlen;
    if (es[i].dlen) memcpy(out + o, es[i].desc, es[i].dlen);
    o += es[i].dlen;
  }
  out[1] = (unsigned char)(0xB0 | (((o + 4 - 3) >> 8) & 0x0F));
  out[2] = (unsigned char)(o + 4 - 3);
  crc = crc32_mpeg(out, o);
  out[o++] = (unsigned char)(crc >> 24);
  out[o++] = (unsigned char)(crc >> 16);
  out[o++] = (unsigned char)(crc >> 8);
  out[o++] = (unsigned char)crc;
  return o;
}

static void feed_section(psi_t *p, unsigned pid, const unsigned char *sec, size_t len) {
  unsigned char pkt[188];

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, sec, len);
  psi_feed(p, pkt);
}

static psi_t *make_psi(const es_spec_t *es, size_t n) {
  psi_t *p = psi_new();
  unsigned char sec[256];
  size_t len;

  ck_assert_ptr_nonnull(p);
  len = psi_build_pat(1, 0, 1, 0x0100, sec, sizeof sec);
  feed_section(p, 0x0000, sec, len);
  len = build_pmt_specs(sec, 1, 0x0101, es, n);
  feed_section(p, 0x0100, sec, len);
  len = psi_build_sdt(0, 1, 2, 1, 0x01, "Provider", "Chan", sec, sizeof sec);
  feed_section(p, 0x0011, sec, len);
  return p;
}

static void capture_stats(const config_t *cfg, double elapsed, unsigned long long bytes, const psi_t *psi, char *out, size_t cap) {
  int master = posix_openpt(O_RDWR | O_NOCTTY);
  int slave;
  ssize_t n;

  ck_assert_int_ge(master, 0);
  ck_assert_int_eq(grantpt(master), 0);
  ck_assert_int_eq(unlockpt(master), 0);
  slave = open(ptsname(master), O_RDWR | O_NOCTTY);
  ck_assert_int_ge(slave, 0);
  ck_assert_int_ge(dup2(slave, STDERR_FILENO), 0);
  close(slave);
  stats_show(cfg, elapsed, bytes, psi);
  ck_assert_int_eq(fcntl(master, F_SETFL, O_NONBLOCK), 0);
  n = read(master, out, cap - 1);
  out[n < 0 ? 0 : n] = '\0';
  close(master);
}

static const unsigned char aac_only_desc[] = {0};
static const unsigned char ttx_desc[] = {0x56, 0x05, 'e', 'n', 'g', 0x11, 0x00};
static const unsigned char sub_desc[] = {0x59, 0x08, 'e', 'n', 'g', 0x10, 0x00, 0x01, 0x00, 0x01};

typedef struct {
  int audio;
  int ttx;
  int sub;
  const char *want_subs;
} stats_case_t;

static const stats_case_t stats_cases[] = {
  {1, 0, 0, "a=1 s=-"},
  {1, 1, 0, "a=1 s=txt"},
  {1, 0, 1, "a=1 s=sub"},
  {1, 1, 1, "a=1 s=ttx+sub"},
  {0, 0, 0, "a=0 s=-"},
};

START_TEST(stats_show_prints_service_audio_and_subtitle_summary) {
  const stats_case_t *c = &stats_cases[_i];
  es_spec_t es[4];
  size_t n = 0;
  config_t cfg;
  psi_t *p;
  char out[512];

  es[n++] = (es_spec_t){0x1B, 0x0101, aac_only_desc, 0};
  if (c->audio) es[n++] = (es_spec_t){0x0F, 0x0102, aac_only_desc, 0};
  if (c->ttx) es[n++] = (es_spec_t){0x06, 0x0103, ttx_desc, sizeof ttx_desc};
  if (c->sub) es[n++] = (es_spec_t){0x06, 0x0104, sub_desc, sizeof sub_desc};
  p = make_psi(es, n);
  memset(&cfg, 0, sizeof cfg);
  capture_stats(&cfg, 75.0, 3u * 1048576u, p, out, sizeof out);
  ck_assert_ptr_nonnull(strstr(out, "1:15 3.0MB Chan "));
  ck_assert_ptr_nonnull(strstr(out, c->want_subs));
  ck_assert_ptr_null(strstr(out, "stop="));
  psi_free(p);
}
END_TEST

START_TEST(stats_show_without_psi_and_with_duration_limit) {
  config_t cfg;
  char out[512];

  memset(&cfg, 0, sizeof cfg);
  cfg.duration_s = 100;
  capture_stats(&cfg, 50.0, 0, NULL, out, sizeof out);
  ck_assert_ptr_nonnull(strstr(out, "0:50 0.0MB ? a=0 s=-"));
  ck_assert_ptr_nonnull(strstr(out, "50.0% stop="));
}
END_TEST

START_TEST(stats_show_caps_percentage_at_one_hundred) {
  config_t cfg;
  char out[512];

  memset(&cfg, 0, sizeof cfg);
  cfg.duration_s = 100;
  capture_stats(&cfg, 250.0, 0, NULL, out, sizeof out);
  ck_assert_ptr_nonnull(strstr(out, "100.0% stop="));
}
END_TEST

START_TEST(stats_show_prints_nothing_when_stderr_is_not_a_tty) {
  config_t cfg;
  char msg[256];

  memset(&cfg, 0, sizeof cfg);
  log_capture_begin();
  stats_show(&cfg, 5.0, 0, NULL);
  log_capture_end(msg, sizeof msg);
  ck_assert_str_eq(msg, "");
}
END_TEST

START_TEST(push_metrics_reports_bytes_elapsed_limit_and_output_state) {
  sink_t ms;
  seen_t seen;
  config_t cfg;
  out_sink_t sinks[2];
  rtmp_fanout_t rf;
  uint64_t v = 0;
  tssink_t *net;
  tssink_cfg_t tc;

  memset(&tc, 0, sizeof tc);
  tc.kind = TSSINK_FILE;
  tc.file_path = "/dev/null";
  net = tssink_open(&tc);
  ck_assert_ptr_nonnull(net);
  rec_cfg_defaults(&cfg);
  cfg.duration_s = 120;
  memset(sinks, 0, sizeof sinks);
  sinks[0].kind = OUT_FILE;
  sinks[1].kind = OUT_UDP;
  sinks[1].net = net;
  sinks[1].net_had_error = 1;
  sinks[1].errors_total = 7;
  memset(&rf, 0, sizeof rf);
  rf.n = 1;
  rf.had_error[0] = 0;
  rf.errors_total[0] = 3;

  metrics_sink_open(&ms, METRICS_COMPONENT_REC, "rec1", 5.0);
  push_metrics(&ms.mx, &cfg, sinks, 2, &rf, 123456, mono_seconds() - 10.0);
  ck_assert_int_eq(sink_read(&ms, &seen), 1);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_REC_BYTES_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 123456u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_REC_ELAPSED_SECONDS, &v), 1);
  ck_assert_uint_ge(v, 10u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_REC_DURATION_LIMIT_SECONDS, &v), 1);
  ck_assert_uint_eq(v, 120u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_REC_OUTPUT_ERRORS_TOTAL, &v), 1);
  metrics_sink_close(&ms);
  tssink_close(net);
}
END_TEST

#define RAW_PKTS 300

static void make_numbered_ts_file(char *path, size_t cap, unsigned npkts) {
  unsigned char pkt[188];
  int fd;

  snprintf(path, cap, "/tmp/dipirec_raw_XXXXXX");
  fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  for (unsigned i = 0; i < npkts; i++) {
    memset(pkt, (int)(i & 0xFF), sizeof pkt);
    pkt[0] = 0x47;
    pkt[1] = 0x01;
    pkt[2] = 0x00;
    pkt[3] = (unsigned char)(0x10 | (i & 0x0F));
    ck_assert_int_eq((int)write(fd, pkt, sizeof pkt), (int)sizeof pkt);
  }
  close(fd);
}

typedef struct {
  config_t cfg;
  src_t src;
  out_sink_t sink;
  rtmp_fanout_t rf;
  metrics_exporter_t mx;
  rec_insp_t ri;
  unsigned long long bytes;
  char in_path[64];
  char out_path[64];
} raw_run_t;

static void raw_run_open(raw_run_t *r, unsigned npkts) {
  int fd;

  memset(r, 0, sizeof *r);
  make_numbered_ts_file(r->in_path, sizeof r->in_path, npkts);
  snprintf(r->out_path, sizeof r->out_path, "/tmp/dipirec_rawout_XXXXXX");
  fd = mkstemp(r->out_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  rec_cfg_defaults(&r->cfg);
  ck_assert_int_eq(rec_cfg_set_in(&r->cfg, r->in_path), 0);
  ck_assert_int_eq(rec_cfg_add_out(&r->cfg, r->out_path), 0);
  ck_assert_int_eq(rec_cfg_format(&r->cfg, "raw"), 0);
  ck_assert_int_eq(src_open(&r->cfg, &r->src), 0);
  ck_assert_int_eq(sink_open(&r->cfg, &r->cfg.out[0], &r->sink), 0);
}

static void raw_run_close(raw_run_t *r) {
  sink_close(&r->sink);
  src_close(&r->src);
  unlink(r->in_path);
  unlink(r->out_path);
}

START_TEST(run_raw_copies_a_file_byte_for_byte) {
  raw_run_t r;
  unsigned char *in = malloc((size_t)RAW_PKTS * 188);
  unsigned char *out = malloc((size_t)RAW_PKTS * 188 + 1);

  ck_assert_ptr_nonnull(in);
  ck_assert_ptr_nonnull(out);
  raw_run_open(&r, RAW_PKTS);
  ck_assert_int_eq(run_raw(&r.src, &r.cfg, &r.sink, 1, &r.rf, &r.mx, &r.bytes, mono_seconds(), NULL, &r.ri), 0);
  ck_assert_uint_eq(r.bytes, (unsigned long long)RAW_PKTS * 188);
  ck_assert_uint_eq(file_size_of(r.in_path, in, (size_t)RAW_PKTS * 188), (size_t)RAW_PKTS * 188);
  ck_assert_uint_eq(file_size_of(r.out_path, out, (size_t)RAW_PKTS * 188 + 1), (size_t)RAW_PKTS * 188);
  ck_assert_int_eq(memcmp(in, out, (size_t)RAW_PKTS * 188), 0);
  raw_run_close(&r);
  free(in);
  free(out);
}
END_TEST

START_TEST(run_raw_verbose_run_still_copies_everything) {
  raw_run_t r;
  unsigned char *out = malloc((size_t)RAW_PKTS * 188 + 1);

  ck_assert_ptr_nonnull(out);
  raw_run_open(&r, RAW_PKTS);
  r.cfg.verbose = 1;
  ck_assert_int_eq(run_raw(&r.src, &r.cfg, &r.sink, 1, &r.rf, &r.mx, &r.bytes, mono_seconds(), NULL, &r.ri), 0);
  ck_assert_uint_eq(r.bytes, (unsigned long long)RAW_PKTS * 188);
  ck_assert_uint_eq(file_size_of(r.out_path, out, (size_t)RAW_PKTS * 188 + 1), (size_t)RAW_PKTS * 188);
  raw_run_close(&r);
  free(out);
}
END_TEST

START_TEST(run_raw_stops_at_the_duration_limit_on_an_endless_input) {
  raw_run_t r;
  int pipefd[2];
  unsigned char pkt[188];
  struct timespec t0;
  struct timespec t1;
  double elapsed;

  ck_assert_int_eq(pipe(pipefd), 0);
  ck_assert_int_ge(dup2(pipefd[0], STDIN_FILENO), 0);
  close(pipefd[0]);
  memset(&r, 0, sizeof r);
  snprintf(r.out_path, sizeof r.out_path, "/tmp/dipirec_rawout_XXXXXX");
  {
    int fd = mkstemp(r.out_path);

    ck_assert_int_ge(fd, 0);
    close(fd);
  }
  rec_cfg_defaults(&r.cfg);
  ck_assert_int_eq(rec_cfg_set_in(&r.cfg, "-"), 0);
  ck_assert_int_eq(rec_cfg_add_out(&r.cfg, r.out_path), 0);
  ck_assert_int_eq(rec_cfg_format(&r.cfg, "raw"), 0);
  r.cfg.duration_s = 1;
  ck_assert_int_eq(src_open(&r.cfg, &r.src), 0);
  ck_assert_int_eq(sink_open(&r.cfg, &r.cfg.out[0], &r.sink), 0);
  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  for (int i = 0; i < 40; i++) ck_assert_int_eq((int)write(pipefd[1], pkt, sizeof pkt), (int)sizeof pkt);

  clock_gettime(CLOCK_MONOTONIC, &t0);
  ck_assert_int_eq(run_raw(&r.src, &r.cfg, &r.sink, 1, &r.rf, &r.mx, &r.bytes, mono_seconds(), NULL, &r.ri), 0);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  elapsed = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
  ck_assert_double_ge(elapsed, 0.9);
  ck_assert_double_lt(elapsed, 3.0);
  ck_assert_uint_ge(r.bytes, 188u);
  ck_assert_uint_eq(r.bytes % 188, 0u);
  sink_close(&r.sink);
  src_close(&r.src);
  close(pipefd[1]);
  unlink(r.out_path);
}
END_TEST

static void put_psi_pkt(int fd, unsigned pid, const unsigned char *sec, size_t len) {
  unsigned char pkt[188];

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, sec, len);
  ck_assert_int_eq((int)write(fd, pkt, sizeof pkt), (int)sizeof pkt);
}

static void put_pcr_pkt(int fd, unsigned pid, uint64_t base) {
  unsigned char pkt[188];

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x20;
  pkt[4] = 7;
  pkt[5] = 0x10;
  pkt[6] = (unsigned char)(base >> 25);
  pkt[7] = (unsigned char)(base >> 17);
  pkt[8] = (unsigned char)(base >> 9);
  pkt[9] = (unsigned char)(base >> 1);
  pkt[10] = (unsigned char)(((base & 1) << 7) | 0x7E);
  pkt[11] = 0x00;
  ck_assert_int_eq((int)write(fd, pkt, sizeof pkt), (int)sizeof pkt);
}

START_TEST(run_raw_pace_holds_a_file_to_its_pcr_clock) {
  raw_run_t r;
  pace_ctrl_t *pace = pace_new();
  unsigned char sec[128];
  unsigned char filler[188];
  es_spec_t es = {0x1B, 0x0101, aac_only_desc, 0};
  size_t len;
  int fd;
  struct timespec t0;
  struct timespec t1;
  double elapsed;
  unsigned char *in = malloc(188 * 100);
  unsigned char *out = malloc(188 * 100);

  ck_assert_ptr_nonnull(in);
  ck_assert_ptr_nonnull(out);
  ck_assert_ptr_nonnull(pace);
  memset(&r, 0, sizeof r);
  snprintf(r.in_path, sizeof r.in_path, "/tmp/dipirec_pace_XXXXXX");
  fd = mkstemp(r.in_path);
  ck_assert_int_ge(fd, 0);
  len = psi_build_pat(1, 0, 1, 0x0100, sec, sizeof sec);
  put_psi_pkt(fd, 0x0000, sec, len);
  len = build_pmt_specs(sec, 1, 0x0101, &es, 1);
  put_psi_pkt(fd, 0x0100, sec, len);
  memset(filler, 0xFF, sizeof filler);
  filler[0] = 0x47;
  filler[1] = 0x1F;
  filler[2] = 0xFF;
  filler[3] = 0x10;
  for (int k = 0; k < 5; k++) {
    put_pcr_pkt(fd, 0x0101, 1000 + (uint64_t)k * 9000);
    for (int i = 0; i < 15; i++) ck_assert_int_eq((int)write(fd, filler, sizeof filler), (int)sizeof filler);
  }
  close(fd);
  snprintf(r.out_path, sizeof r.out_path, "/tmp/dipirec_rawout_XXXXXX");
  fd = mkstemp(r.out_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  rec_cfg_defaults(&r.cfg);
  ck_assert_int_eq(rec_cfg_set_in(&r.cfg, r.in_path), 0);
  ck_assert_int_eq(rec_cfg_add_out(&r.cfg, r.out_path), 0);
  ck_assert_int_eq(rec_cfg_format(&r.cfg, "raw"), 0);
  r.cfg.pace = 1;
  ck_assert_int_eq(src_open(&r.cfg, &r.src), 0);
  ck_assert_int_eq(sink_open(&r.cfg, &r.cfg.out[0], &r.sink), 0);

  clock_gettime(CLOCK_MONOTONIC, &t0);
  ck_assert_int_eq(run_raw(&r.src, &r.cfg, &r.sink, 1, &r.rf, &r.mx, &r.bytes, mono_seconds(), pace, &r.ri), 0);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  elapsed = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
  ck_assert_double_ge(elapsed, 0.3);
  ck_assert_double_lt(elapsed, 3.0);
  ck_assert_uint_eq(r.bytes, 82u * 188u);
  ck_assert_uint_eq(file_size_of(r.in_path, in, 188 * 100), 82u * 188u);
  ck_assert_uint_eq(file_size_of(r.out_path, out, 188 * 100), 82u * 188u);
  ck_assert_int_eq(memcmp(in, out, 82u * 188u), 0);
  pace_free(pace);
  raw_run_close(&r);
  free(in);
  free(out);
}
END_TEST

START_TEST(run_raw_returns_one_when_the_sink_write_fails) {
  raw_run_t r;

  raw_run_open(&r, RAW_PKTS);
  sink_close(&r.sink);
  unlink(r.out_path);
  ck_assert_int_eq(rec_cfg_add_out(&r.cfg, "/dev/full"), 0);
  ck_assert_int_eq(sink_open(&r.cfg, &r.cfg.out[1], &r.sink), 0);
  ck_assert_int_eq(run_raw(&r.src, &r.cfg, &r.sink, 1, &r.rf, &r.mx, &r.bytes, mono_seconds(), NULL, &r.ri), 1);
  ck_assert_uint_eq(r.bytes, 0u);
  sink_close(&r.sink);
  src_close(&r.src);
  unlink(r.in_path);
}
END_TEST

#define PID_VIDEO_ES 0x0101
#define PID_AUDIO_1 0x0102
#define PID_AUDIO_2 0x0103
#define PID_NIT_ES 0x0010
#define PID_EIT_ES 0x0012
#define STREAM_ROUNDS 3

static void put_es_pkt(int fd, unsigned pid, unsigned cc) {
  unsigned char pkt[188];

  memset(pkt, 0xA5, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = (unsigned char)(0x10 | (cc & 0x0F));
  ck_assert_int_eq((int)write(fd, pkt, sizeof pkt), (int)sizeof pkt);
}

static void make_spts_file(char *path, size_t cap) {
  unsigned char sec[128];
  es_spec_t es[3] = {{0x1B, PID_VIDEO_ES, aac_only_desc, 0}, {0x0F, PID_AUDIO_1, aac_only_desc, 0}, {0x0F, PID_AUDIO_2, aac_only_desc, 0}};
  static const unsigned pids[] = {PID_VIDEO_ES, PID_AUDIO_1, PID_AUDIO_2, PID_NIT_ES, PID_EIT_ES, 0x1FFF};
  size_t len;
  int fd;

  snprintf(path, cap, "/tmp/dipirec_spts_XXXXXX");
  fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  len = psi_build_pat(1, 0, 1, 0x0100, sec, sizeof sec);
  put_psi_pkt(fd, 0x0000, sec, len);
  len = build_pmt_specs(sec, 1, PID_VIDEO_ES, es, 3);
  put_psi_pkt(fd, 0x0100, sec, len);
  for (unsigned round = 0; round < STREAM_ROUNDS; round++)
    for (size_t k = 0; k < sizeof pids / sizeof pids[0]; k++) put_es_pkt(fd, pids[k], round);
  close(fd);
}

static unsigned count_pid_pkts(const unsigned char *buf, size_t len, unsigned pid) {
  unsigned n = 0;

  for (size_t off = 0; off + 188 <= len; off += 188)
    if ((((unsigned)buf[off + 1] & 0x1F) << 8 | buf[off + 2]) == pid) n++;
  return n;
}

typedef struct {
  config_t cfg;
  src_t src;
  out_sink_t sink;
  rtmp_fanout_t rf;
  metrics_exporter_t mx;
  rec_insp_t ri;
  unsigned long long bytes;
  char in_path[64];
  char out_path[64];
  unsigned char out[188 * 64];
  size_t out_len;
} stream_run_t;

static void stream_run_open(stream_run_t *r, const char *out_target) {
  int fd;

  memset(r, 0, sizeof *r);
  make_spts_file(r->in_path, sizeof r->in_path);
  snprintf(r->out_path, sizeof r->out_path, "/tmp/dipirec_tsout_XXXXXX");
  fd = mkstemp(r->out_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  rec_cfg_defaults(&r->cfg);
  ck_assert_int_eq(rec_cfg_set_in(&r->cfg, r->in_path), 0);
  ck_assert_int_eq(rec_cfg_add_out(&r->cfg, out_target ? out_target : r->out_path), 0);
  ck_assert_int_eq(rec_cfg_format(&r->cfg, "ts"), 0);
  ck_assert_int_eq(src_open(&r->cfg, &r->src), 0);
  ck_assert_int_eq(sink_open(&r->cfg, &r->cfg.out[0], &r->sink), 0);
}

static int stream_run_ts(stream_run_t *r) {
  int rc = run_stream(&r->src, &r->cfg, &r->sink, 1, -1, &r->rf, &r->mx, &r->bytes, mono_seconds(), 1, 0, NULL, 0, NULL, &r->ri);

  r->out_len = file_size_of(r->out_path, r->out, sizeof r->out);
  return rc;
}

static void stream_run_close(stream_run_t *r) {
  sink_close(&r->sink);
  src_close(&r->src);
  unlink(r->in_path);
  unlink(r->out_path);
}

START_TEST(run_stream_ts_keeps_only_the_selected_audio_track) {
  stream_run_t r;

  stream_run_open(&r, NULL);
  ck_assert_int_eq(rec_cfg_audio(&r.cfg, "2"), 0);
  ck_assert_int_eq(stream_run_ts(&r), 0);
  ck_assert_uint_eq(count_pid_pkts(r.out, r.out_len, PID_AUDIO_1), 0u);
  ck_assert_uint_eq(count_pid_pkts(r.out, r.out_len, PID_AUDIO_2), STREAM_ROUNDS);
  ck_assert_uint_eq(count_pid_pkts(r.out, r.out_len, PID_VIDEO_ES), STREAM_ROUNDS);
  ck_assert_uint_eq(count_pid_pkts(r.out, r.out_len, 0x0100), 1u);
  stream_run_close(&r);
}
END_TEST

START_TEST(run_stream_ts_keeps_every_audio_track_by_default) {
  stream_run_t r;

  stream_run_open(&r, NULL);
  ck_assert_int_eq(stream_run_ts(&r), 0);
  ck_assert_uint_eq(count_pid_pkts(r.out, r.out_len, PID_AUDIO_1), STREAM_ROUNDS);
  ck_assert_uint_eq(count_pid_pkts(r.out, r.out_len, PID_AUDIO_2), STREAM_ROUNDS);
  stream_run_close(&r);
}
END_TEST

START_TEST(run_stream_ts_reports_a_missing_audio_track) {
  stream_run_t r;
  char msg[1024];
  int rc;

  stream_run_open(&r, NULL);
  ck_assert_int_eq(rec_cfg_audio(&r.cfg, "3"), 0);
  log_capture_begin();
  rc = stream_run_ts(&r);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, 1);
  ck_assert_ptr_nonnull(strstr(msg, "not found"));
  stream_run_close(&r);
}
END_TEST

typedef struct {
  const char *strip;
  unsigned nit;
  unsigned eit;
  unsigned null_pkts;
} strip_case_t;

static const strip_case_t strip_cases[] = {
  {NULL, 0, 0, 0},
  {"none", STREAM_ROUNDS, STREAM_ROUNDS, STREAM_ROUNDS},
  {"NUL", STREAM_ROUNDS, STREAM_ROUNDS, 0},
  {"EIT,NIT", 0, 0, STREAM_ROUNDS},
};

START_TEST(run_stream_ts_strips_the_configured_tables) {
  const strip_case_t *c = &strip_cases[_i];
  stream_run_t r;

  stream_run_open(&r, NULL);
  if (c->strip) ck_assert_int_eq(rec_cfg_strip(&r.cfg, c->strip), 0);
  ck_assert_int_eq(stream_run_ts(&r), 0);
  ck_assert_uint_eq(count_pid_pkts(r.out, r.out_len, PID_NIT_ES), c->nit);
  ck_assert_uint_eq(count_pid_pkts(r.out, r.out_len, PID_EIT_ES), c->eit);
  ck_assert_uint_eq(count_pid_pkts(r.out, r.out_len, 0x1FFF), c->null_pkts);
  ck_assert_uint_eq(count_pid_pkts(r.out, r.out_len, PID_VIDEO_ES), STREAM_ROUNDS);
  stream_run_close(&r);
}
END_TEST

START_TEST(run_stream_ts_returns_one_when_the_sink_write_fails) {
  stream_run_t r;
  int rc;

  stream_run_open(&r, "/dev/full");
  rc = run_stream(&r.src, &r.cfg, &r.sink, 1, -1, &r.rf, &r.mx, &r.bytes, mono_seconds(), 1, 0, NULL, 0, NULL, &r.ri);
  ck_assert_int_eq(rc, 1);
  sink_close(&r.sink);
  src_close(&r.src);
  unlink(r.in_path);
  unlink(r.out_path);
}
END_TEST

#define AAC_FRAMES 12
#define AAC_FRAME_LEN 100
#define AAC_PTS_STEP 1920
#define CONTAINER_BUF 65536

typedef struct {
  config_t cfg;
  src_t src;
  rtmp_fanout_t rf;
  metrics_exporter_t mx;
  rec_insp_t ri;
  unsigned long long bytes;
  char in_path[64];
  char out_path[64];
  int out_fd;
  unsigned char out[CONTAINER_BUF];
  size_t out_len;
} container_run_t;

static void make_aac_ts_file(char *path, size_t cap) {
  unsigned char disc[DISCOVERY_PACKETS][188];
  unsigned char adts[AAC_FRAME_LEN];
  unsigned char pes[256];
  unsigned char pkt[188];
  int fd;

  snprintf(path, cap, "/tmp/dipirec_aac_XXXXXX");
  fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  build_aac_discovery(disc);
  for (size_t i = 0; i < DISCOVERY_PACKETS; i++) ck_assert_int_eq((int)write(fd, disc[i], 188), 188);
  build_adts_frame(adts, sizeof adts);
  for (unsigned i = 0; i < AAC_FRAMES; i++) {
    size_t plen = build_pes_with_pts(pes, 90000 + (uint64_t)i * AAC_PTS_STEP, adts, sizeof adts);

    wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
    ck_assert_int_eq((int)write(fd, pkt, 188), 188);
  }
  close(fd);
}

static void container_run_open(container_run_t *r, const char *fmt) {
  memset(r, 0, sizeof *r);
  make_aac_ts_file(r->in_path, sizeof r->in_path);
  snprintf(r->out_path, sizeof r->out_path, "/tmp/dipirec_cont_XXXXXX");
  r->out_fd = mkstemp(r->out_path);
  ck_assert_int_ge(r->out_fd, 0);
  rec_cfg_defaults(&r->cfg);
  ck_assert_int_eq(rec_cfg_set_in(&r->cfg, r->in_path), 0);
  ck_assert_int_eq(rec_cfg_add_out(&r->cfg, r->out_path), 0);
  ck_assert_int_eq(rec_cfg_format(&r->cfg, fmt), 0);
  ck_assert_int_eq(src_open(&r->cfg, &r->src), 0);
}

static int container_run(container_run_t *r) {
  int rc = run_stream(&r->src, &r->cfg, NULL, 0, r->out_fd, &r->rf, &r->mx, &r->bytes, mono_seconds(), 0, 0, NULL, 0, NULL, &r->ri);

  r->out_len = file_size_of(r->out_path, r->out, sizeof r->out);
  return rc;
}

static void container_run_close(container_run_t *r) {
  close(r->out_fd);
  src_close(&r->src);
  unlink(r->in_path);
  unlink(r->out_path);
}

START_TEST(run_stream_mka_writes_a_matroska_audio_file) {
  container_run_t r;

  container_run_open(&r, "mka");
  ck_assert_int_eq(container_run(&r), 0);
  ck_assert_uint_gt(r.out_len, 4u);
  ck_assert_uint_eq(r.out[0], 0x1Au);
  ck_assert_uint_eq(r.out[1], 0x45u);
  ck_assert_uint_eq(r.out[2], 0xDFu);
  ck_assert_uint_eq(r.out[3], 0xA3u);
  ck_assert_ptr_nonnull(memmem(r.out, r.out_len, "A_AAC", 5));
  ck_assert_uint_gt((unsigned)r.bytes, 0u);
  container_run_close(&r);
}
END_TEST

START_TEST(run_stream_mp4_writes_an_iso_file_with_the_audio_track) {
  container_run_t r;

  container_run_open(&r, "m4a");
  ck_assert_int_eq(container_run(&r), 0);
  ck_assert_uint_gt(r.out_len, 8u);
  ck_assert_int_eq(memcmp(r.out + 4, "ftyp", 4), 0);
  ck_assert_ptr_nonnull(memmem(r.out, r.out_len, "moov", 4));
  ck_assert_ptr_nonnull(memmem(r.out, r.out_len, "mdat", 4));
  ck_assert_ptr_nonnull(memmem(r.out, r.out_len, "mp4a", 4));
  container_run_close(&r);
}
END_TEST

#define MPTS_PMT_A 0x0100
#define MPTS_PMT_B 0x0200
#define MPTS_AUDIO_A 0x0101
#define MPTS_AUDIO_B 0x0201

static void fill_mpts_aac_file(const char *path) {
  static const unsigned pmts[] = {MPTS_PMT_A, MPTS_PMT_B};
  static const unsigned audio[] = {MPTS_AUDIO_A, MPTS_AUDIO_B};
  static const psi_pat_entry_t pat[2] = {{1, MPTS_PMT_A}, {2, MPTS_PMT_B}};
  static const psi_sdt_entry_t sdt[2] = {{1, 0x02, "prov", "svc one"}, {2, 0x02, "prov", "svc two"}};
  unsigned char sec[256];
  unsigned char adts[AAC_FRAME_LEN];
  unsigned char pes[256];
  unsigned char pkt[188];
  size_t len;
  int fd;

  fd = open(path, O_WRONLY | O_TRUNC);
  ck_assert_int_ge(fd, 0);
  len = psi_build_pat_multi(1, 0, pat, 2, sec, sizeof sec);
  put_psi_pkt(fd, 0x0000, sec, len);
  len = psi_build_sdt_multi(0, 1, 1, sdt, 2, sec, sizeof sec);
  put_psi_pkt(fd, 0x0011, sec, len);
  for (unsigned k = 0; k < 2; k++) {
    es_spec_t es = {0x0F, audio[k], aac_only_desc, 0};

    len = build_pmt_specs(sec, k + 1, audio[k], &es, 1);
    put_psi_pkt(fd, pmts[k], sec, len);
  }
  build_adts_frame(adts, sizeof adts);
  for (unsigned i = 0; i < AAC_FRAMES; i++)
    for (unsigned k = 0; k < 2; k++) {
      size_t plen = build_pes_with_pts(pes, 90000 + (uint64_t)i * AAC_PTS_STEP, adts, sizeof adts);

      wrap_ts_packet(pkt, audio[k], 1, pes, plen);
      ck_assert_int_eq((int)write(fd, pkt, 188), 188);
    }
  close(fd);
}

static unsigned count_substr(const unsigned char *buf, size_t len, const char *needle) {
  size_t nl = strlen(needle);
  unsigned n = 0;

  for (size_t i = 0; i + nl <= len; i++)
    if (memcmp(buf + i, needle, nl) == 0) n++;
  return n;
}

START_TEST(run_stream_all_programs_makes_one_track_per_program) {
  static const unsigned all_pids[] = {MPTS_PMT_A, MPTS_PMT_B};
  container_run_t r;
  int rc;

  container_run_open(&r, "mka");
  fill_mpts_aac_file(r.in_path);
  rc = run_stream(&r.src, &r.cfg, NULL, 0, r.out_fd, &r.rf, &r.mx, &r.bytes, mono_seconds(), 0, 0, all_pids, 2, NULL, &r.ri);
  r.out_len = file_size_of(r.out_path, r.out, sizeof r.out);
  ck_assert_int_eq(rc, 0);
  ck_assert_uint_eq(count_substr(r.out, r.out_len, "A_AAC"), 2u);
  container_run_close(&r);
}
END_TEST

START_TEST(run_stream_feeds_flv_tags_to_the_rtmp_target) {
  container_run_t r;
  rtmp_capture_t cap;
  pthread_t th;
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  char url[64];
  int rc;

  container_run_open(&r, "ts");
  cap.got = 0;
  cap.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  ck_assert_int_ge(cap.listen_fd, 0);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(cap.listen_fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(listen(cap.listen_fd, 1), 0);
  ck_assert_int_eq(getsockname(cap.listen_fd, (struct sockaddr *)&a, &len), 0);
  snprintf(url, sizeof url, "rtmp://127.0.0.1:%u/live/key", ntohs(a.sin_port));
  rec_cfg_defaults(&r.cfg);
  ck_assert_int_eq(rec_cfg_set_in(&r.cfg, r.in_path), 0);
  ck_assert_int_eq(rec_cfg_add_out(&r.cfg, url), 0);
  ck_assert_int_eq(rec_cfg_format(&r.cfg, "ts"), 0);
  ck_assert_int_eq(pthread_create(&th, NULL, rtmp_capture_thread, &cap), 0);
  ck_assert_int_eq(rtmp_fanout_open(&r.cfg, &r.rf), 0);
  ck_assert_int_eq(r.rf.n, 1);
  src_close(&r.src);
  ck_assert_int_eq(src_open(&r.cfg, &r.src), 0);
  rc = run_stream(&r.src, &r.cfg, NULL, 0, -1, &r.rf, &r.mx, &r.bytes, mono_seconds(), 0, 0, NULL, 0, NULL, &r.ri);
  ck_assert_int_eq(rc, 0);
  ck_assert_uint_gt((unsigned)r.bytes, 0u);
  rtmp_fanout_close(&r.rf);
  pthread_join(th, NULL);
  close(cap.listen_fd);
  ck_assert_uint_gt(cap.got, 0u);
  container_run_close(&r);
}
END_TEST

typedef enum { SRC_NO_PAT, SRC_SPTS, SRC_MPTS } resolve_src_t;

typedef struct {
  resolve_src_t kind;
  const char *format;
  const char *pmt;
  const char *out;
  int rc;
  unsigned pmt_pid;
  int n_all;
  const char *msg;
} resolve_case_t;

static const resolve_case_t resolve_cases[] = {
  {SRC_MPTS, "raw", "all", NULL, 0, 0, 0, "no effect with -f raw"},
  {SRC_MPTS, "raw", NULL, NULL, 0, 0, 0, ""},
  {SRC_NO_PAT, "ts", NULL, NULL, 1, 0, 0, "no PAT received"},
  {SRC_SPTS, "ts", NULL, NULL, 0, 0, 0, ""},
  {SRC_SPTS, "ts", "all", NULL, 0, 0, 0, "single-program source"},
  {SRC_MPTS, "ts", NULL, NULL, 1, 0, 0, "pick one with -p"},
  {SRC_MPTS, "ts", "all", NULL, 0, 0, 2, ""},
  {SRC_MPTS, "mkv", "all", NULL, 1, 0, 0, "can't hold multiple programs"},
  {SRC_MPTS, "mp4", "all", NULL, 1, 0, 0, "can't hold multiple programs"},
  {SRC_MPTS, "ts", "all", "rtmp://127.0.0.1:1/live/k", 1, 0, 0, "can't hold multiple programs"},
  {SRC_MPTS, "ts", "0x0200", NULL, 0, MPTS_PMT_B, 0, ""},
  {SRC_MPTS, "ts", "0x0300", NULL, 1, 0, 0, "not found in this MPTS"},
};

static void fill_resolve_source(const char *path, resolve_src_t kind) {
  int fd;

  if (kind == SRC_MPTS) {
    fill_mpts_aac_file(path);
    return;
  }
  fd = open(path, O_WRONLY | O_TRUNC);
  ck_assert_int_ge(fd, 0);
  for (unsigned i = 0; i < 20; i++) put_es_pkt(fd, PID_VIDEO_ES, i);
  close(fd);
  if (kind == SRC_SPTS) {
    char tmp[64];
    unsigned char buf[188 * 64];
    size_t n;

    make_spts_file(tmp, sizeof tmp);
    n = file_size_of(tmp, buf, sizeof buf);
    fd = open(path, O_WRONLY | O_TRUNC);
    ck_assert_int_ge(fd, 0);
    ck_assert_int_eq((int)write(fd, buf, n), (int)n);
    close(fd);
    unlink(tmp);
  }
}

START_TEST(resolve_pmt_selection_covers_the_source_and_option_matrix) {
  const resolve_case_t *c = &resolve_cases[_i];
  config_t cfg;
  src_t src;
  char path[64];
  char msg[2048];
  unsigned pmt_pid = 99;
  unsigned all_pids[8];
  int n_all = 99;
  int rc;
  int fd;

  snprintf(path, sizeof path, "/tmp/dipirec_resolve_XXXXXX");
  fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  fill_resolve_source(path, c->kind);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, path), 0);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, c->out ? c->out : "out.ts"), 0);
  ck_assert_int_eq(rec_cfg_format(&cfg, c->format), 0);
  if (c->pmt) ck_assert_int_eq(rec_cfg_pmt(&cfg, c->pmt), 0);
  ck_assert_int_eq(src_open(&cfg, &src), 0);
  log_capture_begin();
  rc = resolve_pmt_selection(&cfg, &src, &pmt_pid, all_pids, &n_all);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, c->rc);
  ck_assert_uint_eq(pmt_pid, c->pmt_pid);
  ck_assert_int_eq(n_all, c->n_all);
  ck_assert_ptr_nonnull(strstr(msg, c->msg));
  src_close(&src);
  unlink(path);
}
END_TEST

typedef struct {
  const char *format;
  int aac_input;
} record_case_t;

static const record_case_t record_cases[] = {
  {"raw", 0},
  {"ts", 0},
  {"mka", 1},
  {"m4a", 1},
};

START_TEST(record_run_copies_a_file_to_a_file_in_every_format) {
  const record_case_t *c = &record_cases[_i];
  config_t cfg;
  metrics_exporter_t mx;
  char in_path[64];
  char out_path[64];
  char msg[2048];
  unsigned char in[CONTAINER_BUF];
  unsigned char out[CONTAINER_BUF];
  size_t in_len;
  size_t out_len;
  int fd;
  int rc;

  if (c->aac_input) make_aac_ts_file(in_path, sizeof in_path);
  else make_spts_file(in_path, sizeof in_path);
  snprintf(out_path, sizeof out_path, "/tmp/dipirec_recout_XXXXXX");
  fd = mkstemp(out_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  memset(&mx, 0, sizeof mx);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, in_path), 0);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, out_path), 0);
  ck_assert_int_eq(rec_cfg_format(&cfg, c->format), 0);
  log_capture_begin();
  rc = record_run(&cfg, &mx);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, 0);
  ck_assert_ptr_nonnull(strstr(msg, "done."));
  in_len = file_size_of(in_path, in, sizeof in);
  out_len = file_size_of(out_path, out, sizeof out);
  ck_assert_uint_gt(out_len, 0u);
  if (strcmp(c->format, "raw") == 0) {
    ck_assert_uint_eq(out_len, in_len);
    ck_assert_int_eq(memcmp(in, out, in_len), 0);
  } else if (strcmp(c->format, "ts") == 0) {
    ck_assert_uint_eq(out[0], 0x47u);
    ck_assert_uint_eq(out_len % 188, 0u);
    ck_assert_uint_eq(count_pid_pkts(out, out_len, PID_VIDEO_ES), STREAM_ROUNDS);
  } else if (strcmp(c->format, "mka") == 0) {
    ck_assert_uint_eq(out[0], 0x1Au);
    ck_assert_ptr_nonnull(memmem(out, out_len, "A_AAC", 5));
  } else {
    ck_assert_int_eq(memcmp(out + 4, "ftyp", 4), 0);
  }
  unlink(in_path);
  unlink(out_path);
}
END_TEST

typedef enum { FAIL_SINK, FAIL_SOURCE, FAIL_RTMP, FAIL_MPTS } record_fail_t;

START_TEST(record_run_open_failures_unwind_and_return_one) {
  record_fail_t kind = (record_fail_t)_i;
  config_t cfg;
  metrics_exporter_t mx;
  char in_path[64];
  char good[64];
  char msg[2048];
  int rc;
  int fd;

  if (kind == FAIL_MPTS) {
    snprintf(in_path, sizeof in_path, "/tmp/dipirec_recin_XXXXXX");
    fd = mkstemp(in_path);
    ck_assert_int_ge(fd, 0);
    close(fd);
    fill_mpts_aac_file(in_path);
  } else {
    make_spts_file(in_path, sizeof in_path);
  }
  snprintf(good, sizeof good, "/tmp/dipirec_recgood_XXXXXX");
  fd = mkstemp(good);
  ck_assert_int_ge(fd, 0);
  close(fd);
  memset(&mx, 0, sizeof mx);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, kind == FAIL_SOURCE ? "/nonexistent-dir/in.ts" : in_path), 0);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, good), 0);
  if (kind == FAIL_SINK) ck_assert_int_eq(rec_cfg_add_out(&cfg, "/nonexistent-dir/out.ts"), 0);
  if (kind == FAIL_RTMP) ck_assert_int_eq(rec_cfg_add_out(&cfg, "rtmp://127.0.0.1:99999/live/key"), 0);
  ck_assert_int_eq(rec_cfg_format(&cfg, "ts"), 0);
  log_capture_begin();
  rc = record_run(&cfg, &mx);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, 1);
  ck_assert_ptr_null(strstr(msg, "done."));
  unlink(in_path);
  unlink(good);
}
END_TEST

static Suite *record_suite(void) {
  Suite *s = suite_create("dipirec_record");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fmt_dur_under_an_hour_omits_hours);
  tcase_add_test(tc, fmt_dur_with_hours_includes_them);
  tcase_add_test(tc, fmt_dur_zero_and_negative_clamp_to_zero);
  tcase_add_test(tc, fmt_dur_caps_at_99_59_59);
  tcase_add_test(tc, stop_now_false_before_duration_elapses);
  tcase_add_test(tc, stop_now_true_once_duration_elapses);
  tcase_add_test(tc, stop_now_false_forever_when_duration_is_zero);
  tcase_add_test(tc, stop_now_true_on_stop_signal_regardless_of_duration);
  tcase_add_test(tc, src_open_reads_a_file);
  tcase_add_test(tc, src_open_missing_file_fails);
  tcase_add_test(tc, src_open_reads_stdin);
  tcase_add_loop_test(tc, src_open_joins_udp_and_rtp_groups, 0, 2);
  tcase_add_test(tc, src_wait_readable_returns_zero_when_interrupted);
  tcase_add_test(tc, src_wait_readable_reports_poll_failure);
  tcase_add_test(tc, sink_writes_a_file);
  tcase_add_test(tc, sink_open_unwritable_file_fails);
  tcase_add_test(tc, sink_stdout_writes_to_fd_one_and_stays_open);
  tcase_add_loop_test(tc, sink_sends_udp_and_rtp_datagrams, 0, 2);
  tcase_add_test(tc, sink_write_to_full_device_fails);
  tcase_add_test(tc, note_send_result_logs_only_on_failure_and_recovery_edges);
  tcase_add_test(tc, rtmp_fanout_open_stops_at_a_failing_target);
  tcase_add_test(tc, rtmp_fanout_cb_reaches_every_target);
  tcase_add_test(tc, rtmp_fanout_counts_errors_per_target);
  tcase_add_loop_test(tc, stats_show_prints_service_audio_and_subtitle_summary, 0, (int)(sizeof stats_cases / sizeof stats_cases[0]));
  tcase_add_test(tc, stats_show_without_psi_and_with_duration_limit);
  tcase_add_test(tc, stats_show_caps_percentage_at_one_hundred);
  tcase_add_test(tc, stats_show_prints_nothing_when_stderr_is_not_a_tty);
  tcase_add_test(tc, push_metrics_reports_bytes_elapsed_limit_and_output_state);
  tcase_add_test(tc, run_raw_copies_a_file_byte_for_byte);
  tcase_add_test(tc, run_raw_verbose_run_still_copies_everything);
  tcase_add_test(tc, run_raw_stops_at_the_duration_limit_on_an_endless_input);
  tcase_add_test(tc, run_raw_pace_holds_a_file_to_its_pcr_clock);
  tcase_add_test(tc, run_raw_returns_one_when_the_sink_write_fails);
  tcase_add_test(tc, run_stream_ts_keeps_only_the_selected_audio_track);
  tcase_add_test(tc, run_stream_ts_keeps_every_audio_track_by_default);
  tcase_add_test(tc, run_stream_ts_reports_a_missing_audio_track);
  tcase_add_loop_test(tc, run_stream_ts_strips_the_configured_tables, 0, (int)(sizeof strip_cases / sizeof strip_cases[0]));
  tcase_add_test(tc, run_stream_ts_returns_one_when_the_sink_write_fails);
  tcase_add_test(tc, run_stream_mka_writes_a_matroska_audio_file);
  tcase_add_test(tc, run_stream_mp4_writes_an_iso_file_with_the_audio_track);
  tcase_add_test(tc, run_stream_all_programs_makes_one_track_per_program);
  tcase_add_test(tc, run_stream_feeds_flv_tags_to_the_rtmp_target);
  tcase_add_loop_test(tc, resolve_pmt_selection_covers_the_source_and_option_matrix, 0, (int)(sizeof resolve_cases / sizeof resolve_cases[0]));
  tcase_add_loop_test(tc, record_run_copies_a_file_to_a_file_in_every_format, 0, (int)(sizeof record_cases / sizeof record_cases[0]));
  tcase_add_loop_test(tc, record_run_open_failures_unwind_and_return_one, 0, FAIL_MPTS + 1);
  tcase_add_loop_test(tc, src_open_with_ret_wires_client_and_unwinds_on_failure, 0, (int)(sizeof ret_cases / sizeof ret_cases[0]));
  tcase_add_loop_test(tc, src_open_reads_http_and_reports_errors, 0, (int)(sizeof http_cases / sizeof http_cases[0]));
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(record_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
