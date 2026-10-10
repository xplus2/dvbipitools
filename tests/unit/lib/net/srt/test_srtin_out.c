/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <srt/srt.h>

#include "lib/net/srt/srtin.h"
#include "lib/net/srt/srtout.h"

#define CHUNK_BYTES 1316
#define CHUNKS 6
#define LINK_DEADLINE_S 15.0
#define READ_BUF 2048

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
  srtin_cfg_t cfg;
  srtin_t *in;
  atomic_int stop;
  atomic_int finished;
} opener_t;

static void *open_thread(void *arg) {
  opener_t *o = arg;

  o->in = srtin_open(&o->cfg);
  atomic_store(&o->finished, 1);
  return NULL;
}

static void in_cfg(srtin_cfg_t *c, unsigned port, int listen) {
  memset(c, 0, sizeof *c);
  c->peers[0].host = "127.0.0.1";
  c->peers[0].port = port;
  c->npeers = 1;
  c->listen = listen;
}

static void out_cfg(srtout_cfg_t *c, unsigned port) {
  memset(c, 0, sizeof *c);
  c->peers[0].host = "127.0.0.1";
  c->peers[0].port = port;
  c->npeers = 1;
}

static int pump_until_connected(srtout_t *out, pthread_t th, opener_t *o) {
  double deadline = now_seconds() + LINK_DEADLINE_S;
  srtout_status_t st;

  memset(&st, 0, sizeof st);
  while (now_seconds() < deadline) {
    srtout_service(out, &st);
    if (st.connected && atomic_load(&o->finished)) {
      pthread_join(th, NULL);
      return o->in != NULL;
    }
    usleep(5000);
  }
  atomic_store(&o->stop, 1);
  pthread_join(th, NULL);
  return 0;
}

static void exchange_payload(srtout_t *out, srtin_t *in) {
  unsigned char payload[CHUNKS * CHUNK_BYTES];
  unsigned char rx[CHUNKS * CHUNK_BYTES];
  size_t got = 0;
  double deadline = now_seconds() + LINK_DEADLINE_S;
  srtout_status_t st;

  for (size_t i = 0; i < sizeof payload; i++) payload[i] = (unsigned char)(i * 7 + 3);
  srtout_write(out, payload, sizeof payload);
  while (got < sizeof rx && now_seconds() < deadline) {
    unsigned char buf[READ_BUF];
    int reconnected = 0;
    int n;
    srtout_service(out, &st);
    n = srtin_read(in, buf, sizeof buf, &reconnected);
    ck_assert_int_ge(n, 0);
    if (n > 0) {
      ck_assert_uint_le(got + (size_t)n, sizeof rx);
      memcpy(rx + got, buf, (size_t)n);
      got += (size_t)n;
    }
  }
  ck_assert_uint_eq(got, sizeof rx);
  ck_assert_mem_eq(rx, payload, sizeof rx);
}

typedef struct {
  const char *passphrase;
  int pbkeylen;
  const char *streamid;
  const char *packetfilter;
  unsigned latency_ms;
} link_case_t;

static const link_case_t link_cases[] = {
  {NULL, 0, NULL, NULL, 0},
  {"0123456789abcdef", 24, "stream-x", NULL, 150},
};

START_TEST(listener_receives_what_the_sender_writes) {
  const link_case_t *c = &link_cases[_i];
  unsigned port = free_udp_port();
  opener_t o;
  pthread_t th;
  srtout_cfg_t oc;
  srtout_t *out;
  srtout_queue_stats_t qs;

  memset(&o, 0, sizeof o);
  in_cfg(&o.cfg, port, 1);
  o.cfg.stop = &o.stop;
  o.cfg.opts.passphrase = c->passphrase;
  o.cfg.opts.pbkeylen = c->pbkeylen;
  o.cfg.opts.streamid = c->streamid;
  o.cfg.opts.latency_ms = c->latency_ms;
  ck_assert_int_eq(pthread_create(&th, NULL, open_thread, &o), 0);
  usleep(100000);

  out_cfg(&oc, port);
  oc.opts = o.cfg.opts;
  oc.queue_metrics = SRT_QUEUE_METRICS_FULL;
  oc.safety_mult = 99;
  out = srtout_open(&oc);
  ck_assert_ptr_nonnull(out);
  ck_assert_int_eq(pump_until_connected(out, th, &o), 1);
  ck_assert_ptr_nonnull(o.in);
  exchange_payload(out, o.in);
  srtout_queue_stats(out, &qs);
  ck_assert_int_eq(strncmp(qs.peer_label, "127.0.0.1:", 10), 0);
  srtout_close(out);
  srtin_close(o.in);
}
END_TEST

START_TEST(rendezvous_pair_links_up) {
  unsigned port_a = free_udp_port();
  unsigned port_b = free_udp_port();
  opener_t o;
  pthread_t th;
  srtout_cfg_t oc;
  srtout_t *out;

  memset(&o, 0, sizeof o);
  in_cfg(&o.cfg, port_b, 0);
  o.cfg.rendezvous = 1;
  o.cfg.local_host = "127.0.0.1";
  o.cfg.local_port = port_a;
  o.cfg.stop = &o.stop;
  ck_assert_int_eq(pthread_create(&th, NULL, open_thread, &o), 0);
  out_cfg(&oc, port_a);
  oc.rendezvous = 1;
  oc.local_host = "127.0.0.1";
  oc.local_port = port_b;
  out = srtout_open(&oc);
  ck_assert_ptr_nonnull(out);
  ck_assert_int_eq(pump_until_connected(out, th, &o), 1);
  exchange_payload(out, o.in);
  srtout_close(out);
  srtin_close(o.in);
}
END_TEST

START_TEST(mismatched_passphrases_never_link) {
  unsigned port = free_udp_port();
  opener_t o;
  pthread_t th;
  srtout_cfg_t oc;
  srtout_t *out;
  srtout_status_t st;
  double deadline;
  memset(&o, 0, sizeof o);
  in_cfg(&o.cfg, port, 1);
  o.cfg.stop = &o.stop;
  o.cfg.opts.passphrase = "0123456789abcdef";
  ck_assert_int_eq(pthread_create(&th, NULL, open_thread, &o), 0);
  usleep(100000);
  out_cfg(&oc, port);
  oc.opts.passphrase = "fedcba9876543210";
  out = srtout_open(&oc);
  ck_assert_ptr_nonnull(out);
  memset(&st, 0, sizeof st);
  deadline = now_seconds() + 2.0;
  while (now_seconds() < deadline) {
    srtout_service(out, &st);
    usleep(5000);
  }
  ck_assert_int_eq(st.connected, 0);
  atomic_store(&o.stop, 1);
  pthread_join(th, NULL);
  ck_assert_ptr_null(o.in);
  srtout_close(out);
}
END_TEST

START_TEST(second_listener_on_a_busy_port_fails) {
  unsigned port = free_udp_port();
  opener_t first;
  opener_t second;
  pthread_t th;

  memset(&first, 0, sizeof first);
  in_cfg(&first.cfg, port, 1);
  first.cfg.stop = &first.stop;
  ck_assert_int_eq(pthread_create(&th, NULL, open_thread, &first), 0);
  usleep(200000);
  memset(&second, 0, sizeof second);
  in_cfg(&second.cfg, port, 1);
  second.cfg.stop = &second.stop;
  atomic_store(&second.stop, 1);
  ck_assert_ptr_null(srtin_open(&second.cfg));
  atomic_store(&first.stop, 1);
  pthread_join(th, NULL);
  ck_assert_ptr_null(first.in);
}
END_TEST

START_TEST(a_stopped_listener_gives_up) {
  opener_t o;

  memset(&o, 0, sizeof o);
  in_cfg(&o.cfg, free_udp_port(), 1);
  o.cfg.stop = &o.stop;
  atomic_store(&o.stop, 1);
  ck_assert_ptr_null(srtin_open(&o.cfg));
}
END_TEST

START_TEST(invalid_configurations_are_refused) {
  srtin_cfg_t ic;
  srtout_cfg_t oc;

  in_cfg(&ic, 1, 0);
  ic.npeers = 0;
  ck_assert_ptr_null(srtin_open(&ic));
  ic.npeers = SRTCOMMON_MAX_PEERS + 1;
  ck_assert_ptr_null(srtin_open(&ic));
  ic.npeers = 2;
  ck_assert_ptr_null(srtin_open(&ic));
  in_cfg(&ic, 1, 0);
  ic.rendezvous = 1;
  ck_assert_ptr_null(srtin_open(&ic));
  in_cfg(&ic, 1, 0);
  ic.peers[0].host = "no.such.host.invalid";
  ck_assert_ptr_null(srtin_open(&ic));
  in_cfg(&ic, 1, 1);
  ic.peers[0].host = "no.such.host.invalid";
  ck_assert_ptr_null(srtin_open(&ic));

  out_cfg(&oc, 1);
  oc.npeers = 0;
  ck_assert_ptr_null(srtout_open(&oc));
  oc.npeers = 2;
  ck_assert_ptr_null(srtout_open(&oc));
  out_cfg(&oc, 1);
  oc.group_mode = SRTGROUP_BROADCAST;
  oc.rendezvous = 1;
  ck_assert_ptr_null(srtout_open(&oc));
  out_cfg(&oc, 1);
  oc.rendezvous = 1;
  ck_assert_ptr_null(srtout_open(&oc));
  out_cfg(&oc, 1);
  oc.peers[0].host = "no.such.host.invalid";
  ck_assert_ptr_null(srtout_open(&oc));
}
END_TEST

START_TEST(bonded_groups_open_only_where_libsrt_supports_them) {
  srtin_cfg_t ic;
  srtout_cfg_t oc;
  srtout_t *out;
  opener_t o;

  memset(&o, 0, sizeof o);
  in_cfg(&ic, free_udp_port(), 0);
  ic.group_mode = SRTGROUP_BACKUP;
  ic.npeers = 2;
  ic.peers[1].host = "127.0.0.1";
  ic.peers[1].port = free_udp_port();
  ck_assert_ptr_null(srtin_open(&ic));

  out_cfg(&oc, free_udp_port());
  oc.group_mode = SRTGROUP_BROADCAST;
  oc.npeers = 2;
  oc.peers[1].host = "127.0.0.1";
  oc.peers[1].port = free_udp_port();
  out = srtout_open(&oc);
  if (out) srtout_close(out);
}
END_TEST

START_TEST(the_queue_drops_oldest_chunks_while_unconnected) {
  srtout_cfg_t oc;
  srtout_t *out;
  srtout_queue_stats_t qs;
  srtout_status_t st;
  unsigned char chunk[CHUNK_BYTES];

  out_cfg(&oc, free_udp_port());
  oc.queue_metrics = SRT_QUEUE_METRICS_FULL;
  out = srtout_open(&oc);
  ck_assert_ptr_nonnull(out);
  memset(chunk, 0x47, sizeof chunk);
  srtout_service(out, &st);
  ck_assert_int_eq(st.connected, 0);
  srtout_queue_stats(out, &qs);
  for (int i = 0; i < qs.capacity + 50; i++) srtout_write(out, chunk, sizeof chunk);
  srtout_write(out, chunk, 100);
  srtout_queue_stats(out, &qs);
  ck_assert_int_eq(qs.chunks, qs.capacity);
  ck_assert_uint_ge(qs.dropped, 50u);
  ck_assert_int_ge(qs.high_watermark, qs.capacity);
  srtout_close(out);
}
END_TEST

START_TEST(a_dead_peer_makes_the_receiver_fail_and_the_sender_reconnect) {
  unsigned port = free_udp_port();
  opener_t o;
  pthread_t th;
  srtout_cfg_t oc;
  srtout_t *out;
  srtout_status_t st;
  double deadline;
  int rc = 0;

  memset(&o, 0, sizeof o);
  in_cfg(&o.cfg, port, 1);
  o.cfg.stop = &o.stop;
  o.cfg.opts.latency_ms = 50;
  ck_assert_int_eq(pthread_create(&th, NULL, open_thread, &o), 0);
  usleep(100000);
  out_cfg(&oc, port);
  oc.opts.latency_ms = 50;
  out = srtout_open(&oc);
  ck_assert_ptr_nonnull(out);
  ck_assert_int_eq(pump_until_connected(out, th, &o), 1);

  srtin_close(o.in);
  memset(&st, 0, sizeof st);
  deadline = now_seconds() + 12.0;
  while (now_seconds() < deadline) {
    unsigned char chunk[CHUNK_BYTES] = {0};

    srtout_write(out, chunk, sizeof chunk);
    srtout_service(out, &st);
    if (!st.connected) {
      rc = 1;
      break;
    }
    usleep(20000);
  }
  ck_assert_int_eq(rc, 1);
  srtout_close(out);
}
END_TEST

START_TEST(the_listener_reaccepts_after_the_sender_vanishes) {
  unsigned port = free_udp_port();
  opener_t o;
  pthread_t th;
  srtout_cfg_t oc;
  srtout_t *out;
  srtout_status_t st;
  unsigned char buf[READ_BUF];
  double deadline;
  int n = 0;
  int reconnected = 0;
  int saw_reconnect = 0;

  memset(&o, 0, sizeof o);
  in_cfg(&o.cfg, port, 1);
  o.cfg.stop = &o.stop;
  ck_assert_int_eq(pthread_create(&th, NULL, open_thread, &o), 0);
  usleep(100000);
  out_cfg(&oc, port);
  out = srtout_open(&oc);
  ck_assert_ptr_nonnull(out);
  ck_assert_int_eq(pump_until_connected(out, th, &o), 1);
  ck_assert_int_eq(srtin_read(o.in, buf, sizeof buf, &reconnected), 0);
  srtout_close(out);

  deadline = now_seconds() + 8.0;
  while (now_seconds() < deadline) {
    n = srtin_read(o.in, buf, sizeof buf, &reconnected);
    ck_assert_int_ge(n, 0);
  }

  out = srtout_open(&oc);
  ck_assert_ptr_nonnull(out);
  memset(&st, 0, sizeof st);
  deadline = now_seconds() + LINK_DEADLINE_S;
  while (now_seconds() < deadline && !saw_reconnect) {
    srtout_service(out, &st);
    n = srtin_read(o.in, buf, sizeof buf, &reconnected);
    ck_assert_int_ge(n, 0);
    if (reconnected) saw_reconnect = 1;
  }
  ck_assert_int_eq(saw_reconnect, 1);
  exchange_payload(out, o.in);
  srtout_close(out);
  srtin_close(o.in);
}
END_TEST

static Suite *srtin_out_suite(void) {
  Suite *s = suite_create("lib_net_srt_srtin_out");
  TCase *tc = tcase_create("core");

  tcase_set_timeout(tc, 90);
  tcase_add_loop_test(tc, listener_receives_what_the_sender_writes, 0, 2);
  tcase_add_test(tc, rendezvous_pair_links_up);
  tcase_add_test(tc, mismatched_passphrases_never_link);
  tcase_add_test(tc, second_listener_on_a_busy_port_fails);
  tcase_add_test(tc, a_stopped_listener_gives_up);
  tcase_add_test(tc, invalid_configurations_are_refused);
  tcase_add_test(tc, bonded_groups_open_only_where_libsrt_supports_them);
  tcase_add_test(tc, the_queue_drops_oldest_chunks_while_unconnected);
  tcase_add_test(tc, a_dead_peer_makes_the_receiver_fail_and_the_sender_reconnect);
  tcase_add_test(tc, the_listener_reaccepts_after_the_sender_vanishes);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(srtin_out_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
