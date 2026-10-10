/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../log_capture.h"
#include "dipiscan/config.h"
#include "lib/demux/crc32.h"
#include "lib/sys/signal.h"
#include "lib/mux/psi_build.h"
#include "dipiscan/scan.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1)

START_TEST(addr_at_sweeps_last_octet_ipv4) {
  config_t cfg;
  char buf[64];
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  inet_pton(AF_INET, "239.1.1.1", cfg.start);
  inet_pton(AF_INET, "239.1.1.254", cfg.end);

  addr_at(&cfg, 1, buf, sizeof buf);
  ck_assert_str_eq(buf, "239.1.1.1");
  addr_at(&cfg, 254, buf, sizeof buf);
  ck_assert_str_eq(buf, "239.1.1.254");
}
END_TEST

START_TEST(addr_at_sweeps_last_octet_ipv6) {
  config_t cfg;
  char buf[64];
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET6;
  inet_pton(AF_INET6, "ff15::1", cfg.start);
  inet_pton(AF_INET6, "ff15::fe", cfg.end);

  addr_at(&cfg, 1, buf, sizeof buf);
  ck_assert_str_eq(buf, "ff15::1");
  addr_at(&cfg, 16, buf, sizeof buf);
  ck_assert_str_eq(buf, "ff15::10");
}
END_TEST

START_TEST(addr_at_carries_across_bytes) {
  config_t cfg;
  char buf[64];
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  inet_pton(AF_INET, "239.1.1.250", cfg.start);
  inet_pton(AF_INET, "239.1.2.10", cfg.end);

  addr_at(&cfg, 1, buf, sizeof buf);
  ck_assert_str_eq(buf, "239.1.1.250");
  addr_at(&cfg, 7, buf, sizeof buf); /* carries into the third octet */
  ck_assert_str_eq(buf, "239.1.2.0");
}
END_TEST

START_TEST(mcast_parse_plain_address_sweeps_default_24) {
  config_t cfg;
  char argv0[] = "dipiscan", argv1[] = "-m", argv2[] = "239.1.1.5";
  char *argv[] = {argv0, argv1, argv2, NULL};
  char lo[64], hi[64];

  ck_assert_int_eq(args_parse(3, argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.total, 256u);
  inet_ntop(AF_INET, cfg.start, lo, sizeof lo);
  inet_ntop(AF_INET, cfg.end, hi, sizeof hi);
  ck_assert_str_eq(lo, "239.1.1.0");
  ck_assert_str_eq(hi, "239.1.1.255");
}
END_TEST

START_TEST(mcast_parse_cidr_sweeps_host_range) {
  config_t cfg;
  char argv0[] = "dipiscan", argv1[] = "-m", argv2[] = "239.1.0.0/23";
  char *argv[] = {argv0, argv1, argv2, NULL};
  char lo[64], hi[64];

  ck_assert_int_eq(args_parse(3, argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.total, 512u);
  inet_ntop(AF_INET, cfg.start, lo, sizeof lo);
  inet_ntop(AF_INET, cfg.end, hi, sizeof hi);
  ck_assert_str_eq(lo, "239.1.0.0");
  ck_assert_str_eq(hi, "239.1.1.255");
}
END_TEST

START_TEST(mcast_parse_cidr_rejects_range_over_cap) {
  config_t cfg;
  char argv0[] = "dipiscan", argv1[] = "-m", argv2[] = "239.0.0.0/8";
  char *argv[] = {argv0, argv1, argv2, NULL};

  ck_assert_int_eq(args_parse(3, argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mcast_parse_explicit_range) {
  config_t cfg;
  char argv0[] = "dipiscan", argv1[] = "-m", argv2[] = "239.1.1.10-239.1.1.20";
  char *argv[] = {argv0, argv1, argv2, NULL};
  char lo[64], hi[64];

  ck_assert_int_eq(args_parse(3, argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.total, 11u);
  inet_ntop(AF_INET, cfg.start, lo, sizeof lo);
  inet_ntop(AF_INET, cfg.end, hi, sizeof hi);
  ck_assert_str_eq(lo, "239.1.1.10");
  ck_assert_str_eq(hi, "239.1.1.20");
}
END_TEST

START_TEST(mcast_parse_explicit_range_rejects_reversed) {
  config_t cfg;
  char argv0[] = "dipiscan", argv1[] = "-m", argv2[] = "239.1.1.20-239.1.1.10";
  char *argv[] = {argv0, argv1, argv2, NULL};

  ck_assert_int_eq(args_parse(3, argv, &cfg), ARGS_ERR);
}
END_TEST

/* zero-ES PMT section (table_id 0x02), CRC included */
static size_t build_pmt(unsigned char *out, unsigned prog_num, unsigned pcr_pid) {
  unsigned char body[16];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((pcr_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pcr_pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;

  hdr = n + 4;
  out[0] = 0x02;
  out[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
  out[2] = (unsigned char)hdr;
  memcpy(out + 3, body, n);

  crc_at = 3 + n;
  crc = crc32_mpeg(out, crc_at);
  out[crc_at + 0] = (unsigned char)(crc >> 24);
  out[crc_at + 1] = (unsigned char)(crc >> 16);
  out[crc_at + 2] = (unsigned char)(crc >> 8);
  out[crc_at + 3] = (unsigned char)crc;
  return crc_at + 4;
}

/* wraps one PSI section (pusi=1, pointer_field=0) into a single 188-byte TS packet */
static void wrap_ts_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (size_t i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

START_TEST(multi_all_named_false_without_pat) {
  psi_t *p = psi_new();
  psi_enable_multi_program(p);
  ck_assert_int_eq(multi_all_named(p), 0);
  psi_free(p);
}
END_TEST

START_TEST(multi_all_named_false_when_multi_program_not_enabled) {
  unsigned char sec[64], pkt[188];
  size_t slen;
  psi_t *p = psi_new(); /* psi_enable_multi_program() not called */

  slen = psi_build_pat(1, 0, 1, 0x0100, sec, sizeof sec);
  wrap_ts_packet(pkt, 0x0000, sec, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_pat(p), 1);
  ck_assert_int_eq(multi_all_named(p), 0);
  psi_free(p);
}
END_TEST

START_TEST(multi_all_named_false_until_every_program_resolved_and_named) {
  unsigned char sec[64], pkt[188];
  size_t slen;
  psi_t *p = psi_new();
  psi_enable_multi_program(p);

  slen = psi_build_pat(1, 0, 1, 0x0100, sec, sizeof sec);
  wrap_ts_packet(pkt, 0x0000, sec, slen);
  psi_feed(p, pkt);
  ck_assert_int_eq(multi_all_named(p), 0); /* PMT not seen yet */

  slen = build_pmt(sec, 1, 0x0101);
  wrap_ts_packet(pkt, 0x0100, sec, slen);
  psi_feed(p, pkt);
  ck_assert_int_eq(multi_all_named(p), 0); /* resolved, but no SDT name yet */

  slen = psi_build_sdt(0, 1, 2, 1, 0x01, "Provider", "Channel One", sec, sizeof sec);
  wrap_ts_packet(pkt, 0x0011, sec, slen);
  psi_feed(p, pkt);
  ck_assert_int_eq(multi_all_named(p), 1);

  psi_free(p);
}
END_TEST

START_TEST(multi_all_named_false_when_only_some_programs_named) {
  unsigned char sec[128], pkt[188];
  size_t slen;
  psi_pat_entry_t progs[2];
  psi_t *p = psi_new();
  psi_enable_multi_program(p);

  progs[0].program_number = 1;
  progs[0].pmt_pid = 0x0100;
  progs[1].program_number = 2;
  progs[1].pmt_pid = 0x0200;
  slen = psi_build_pat_multi(1, 0, progs, 2, sec, sizeof sec);
  wrap_ts_packet(pkt, 0x0000, sec, slen);
  psi_feed(p, pkt);

  slen = build_pmt(sec, 1, 0x0101);
  wrap_ts_packet(pkt, 0x0100, sec, slen);
  psi_feed(p, pkt);
  slen = build_pmt(sec, 2, 0x0201);
  wrap_ts_packet(pkt, 0x0200, sec, slen);
  psi_feed(p, pkt);

  slen = psi_build_sdt(0, 1, 2, 1, 0x01, "Provider", "Channel One", sec, sizeof sec);
  wrap_ts_packet(pkt, 0x0011, sec, slen);
  psi_feed(p, pkt);
  ck_assert_int_eq(multi_all_named(p), 0); /* program 2 still unnamed */

  slen = psi_build_sdt(0, 1, 2, 2, 0x01, "Provider", "Channel Two", sec, sizeof sec);
  wrap_ts_packet(pkt, 0x0011, sec, slen);
  psi_feed(p, pkt);
  ck_assert_int_eq(multi_all_named(p), 1);

  psi_free(p);
}
END_TEST

START_TEST(probe_cb_single_mode_stops_once_named) {
  unsigned char sec[64], pkt[188];
  size_t slen;
  probe_ctx_t pc;
  pc.psi = psi_new();
  pc.pkts = 0;
  pc.multi = 0;

  slen = psi_build_pat(1, 0, 1, 0x0100, sec, sizeof sec);
  wrap_ts_packet(pkt, 0x0000, sec, slen);
  ck_assert_int_eq(probe_cb(&pc, pkt), 0);

  slen = build_pmt(sec, 1, 0x0101);
  wrap_ts_packet(pkt, 0x0100, sec, slen);
  ck_assert_int_eq(probe_cb(&pc, pkt), 0);

  slen = psi_build_sdt(0, 1, 2, 1, 0x01, "Provider", "Channel One", sec, sizeof sec);
  wrap_ts_packet(pkt, 0x0011, sec, slen);
  ck_assert_int_eq(probe_cb(&pc, pkt), 1);
  ck_assert_uint_eq(pc.pkts, 3u);

  psi_free(pc.psi);
}
END_TEST

typedef struct {
  const unsigned char *const *pkts;
  const size_t *lens;
  size_t count, next;
} stub_reader_t;

static ssize_t stub_read(void *ctx, unsigned char *buf, size_t cap) {
  stub_reader_t *sr = ctx;
  size_t n;
  if (sr->next >= sr->count)
    return 0; /* no more data this tick, caller keeps polling until deadline */
  n = sr->lens[sr->next];
  ck_assert_uint_le(n, cap);
  memcpy(buf, sr->pkts[sr->next], n);
  sr->next++;
  return (ssize_t)n;
}

START_TEST(probe_common_resolves_named_single_program) {
  unsigned char sec[64];
  unsigned char pat[188], pmt[188], sdt[188];
  size_t slen;
  const unsigned char *pkts[3];
  size_t lens[3];
  stub_reader_t sr;
  probe_result_t r;

  slen = psi_build_pat(0x1234, 0, 7, 0x0100, sec, sizeof sec);
  wrap_ts_packet(pat, 0x0000, sec, slen);
  slen = build_pmt(sec, 7, 0x0101);
  wrap_ts_packet(pmt, 0x0100, sec, slen);
  slen = psi_build_sdt(0, 0x1234, 5, 7, 0x01, "Provider", "Channel One", sec, sizeof sec);
  wrap_ts_packet(sdt, 0x0011, sec, slen);

  pkts[0] = pat; lens[0] = 188;
  pkts[1] = pmt; lens[1] = 188;
  pkts[2] = sdt; lens[2] = 188;
  sr.pkts = pkts;
  sr.lens = lens;
  sr.count = 3;
  sr.next = 0;

  probe_common(stub_read, &sr, 2000, 0, &r);

  ck_assert_int_eq(r.kind, PROBE_NAMED);
  ck_assert_str_eq(r.name, "Channel One");
  ck_assert_uint_eq(r.tsid, 0x1234u);
  ck_assert_uint_eq(r.onid, 5u);
  ck_assert_uint_eq(r.sid, 7u);
  ck_assert_int_eq(r.rtp_wrapped, 0);
  ck_assert_uint_eq(r.pkts, 3u);
}
END_TEST

START_TEST(probe_common_detects_rtp_wrapping_and_strips_header) {
  unsigned char sec[64];
  unsigned char pat[12 + 188], pmt[12 + 188], sdt[12 + 188];
  size_t slen;
  const unsigned char *pkts[3];
  size_t lens[3];
  stub_reader_t sr;
  probe_result_t r;

  memset(pat, 0, 12);
  pat[0] = 0x80; /* RTP v2, no cc/extension */
  slen = psi_build_pat(1, 0, 1, 0x0100, sec, sizeof sec);
  wrap_ts_packet(pat + 12, 0x0000, sec, slen);

  memset(pmt, 0, 12);
  pmt[0] = 0x80;
  slen = build_pmt(sec, 1, 0x0101);
  wrap_ts_packet(pmt + 12, 0x0100, sec, slen);

  memset(sdt, 0, 12);
  sdt[0] = 0x80;
  slen = psi_build_sdt(0, 1, 2, 1, 0x01, "Provider", "Channel One", sec, sizeof sec);
  wrap_ts_packet(sdt + 12, 0x0011, sec, slen);

  pkts[0] = pat; lens[0] = sizeof pat;
  pkts[1] = pmt; lens[1] = sizeof pmt;
  pkts[2] = sdt; lens[2] = sizeof sdt;
  sr.pkts = pkts;
  sr.lens = lens;
  sr.count = 3;
  sr.next = 0;

  probe_common(stub_read, &sr, 2000, 0, &r);

  ck_assert_int_eq(r.rtp_wrapped, 1);
  ck_assert_int_eq(r.kind, PROBE_NAMED);
  ck_assert_str_eq(r.name, "Channel One");
}
END_TEST

START_TEST(probe_common_times_out_with_no_data) {
  stub_reader_t sr;
  probe_result_t r;
  sr.pkts = NULL;
  sr.lens = NULL;
  sr.count = 0;
  sr.next = 0;

  probe_common(stub_read, &sr, 5, 0, &r); /* 5ms: keeps the test fast */

  ck_assert_int_eq(r.kind, PROBE_NONE);
  ck_assert_uint_eq(r.pkts, 0u);
}
END_TEST

typedef struct {
  stub_reader_t base;
  double start, delay;
} delayed_reader_t;

static double test_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static ssize_t delayed_read(void *ctx, unsigned char *buf, size_t cap) {
  delayed_reader_t *dr = ctx;
  if (test_now() - dr->start < dr->delay) {
    struct timespec nap = {0, 1000000};
    nanosleep(&nap, NULL);
    return 0;
  }
  return stub_read(&dr->base, buf, cap);
}

START_TEST(probe_common_first_packet_wait_scales_with_timeout) {
  unsigned char sec[64], pat[188], pmt[188], sdt[188];
  size_t slen;
  const unsigned char *pkts[3];
  size_t lens[3];
  delayed_reader_t dr;
  probe_result_t r;

  slen = psi_build_pat(0x1234, 0, 7, 0x0100, sec, sizeof sec);
  wrap_ts_packet(pat, 0x0000, sec, slen);
  slen = build_pmt(sec, 7, 0x0101);
  wrap_ts_packet(pmt, 0x0100, sec, slen);
  slen = psi_build_sdt(0, 0x1234, 5, 7, 0x01, "Provider", "Channel One", sec, sizeof sec);
  wrap_ts_packet(sdt, 0x0011, sec, slen);
  pkts[0] = pat; lens[0] = 188;
  pkts[1] = pmt; lens[1] = 188;
  pkts[2] = sdt; lens[2] = 188;
  dr.base.pkts = pkts;
  dr.base.lens = lens;
  dr.base.count = 3;
  dr.base.next = 0;
  dr.delay = 0.45;
  dr.start = test_now();

  probe_common(delayed_read, &dr, 5000, 0, &r);

  ck_assert_int_eq(r.kind, PROBE_NAMED);
  ck_assert_str_eq(r.name, "Channel One");
}
END_TEST

START_TEST(probe_common_multi_mode_resolves_every_program) {
  unsigned char sec[128];
  unsigned char pat[188], pmt1[188], pmt2[188], sdt1[188], sdt2[188];
  size_t slen;
  psi_pat_entry_t progs[2];
  const unsigned char *pkts[5];
  size_t lens[5];
  stub_reader_t sr;
  probe_result_t r;

  progs[0].program_number = 1;
  progs[0].pmt_pid = 0x0100;
  progs[1].program_number = 2;
  progs[1].pmt_pid = 0x0200;
  slen = psi_build_pat_multi(1, 0, progs, 2, sec, sizeof sec);
  wrap_ts_packet(pat, 0x0000, sec, slen);

  slen = build_pmt(sec, 1, 0x0101);
  wrap_ts_packet(pmt1, 0x0100, sec, slen);
  slen = build_pmt(sec, 2, 0x0201);
  wrap_ts_packet(pmt2, 0x0200, sec, slen);

  slen = psi_build_sdt(0, 1, 2, 1, 0x01, "Provider", "Channel One", sec, sizeof sec);
  wrap_ts_packet(sdt1, 0x0011, sec, slen);
  slen = psi_build_sdt(0, 1, 2, 2, 0x01, "Provider", "Channel Two", sec, sizeof sec);
  wrap_ts_packet(sdt2, 0x0011, sec, slen);

  pkts[0] = pat; lens[0] = 188;
  pkts[1] = pmt1; lens[1] = 188;
  pkts[2] = pmt2; lens[2] = 188;
  pkts[3] = sdt1; lens[3] = 188;
  pkts[4] = sdt2; lens[4] = 188;
  sr.pkts = pkts;
  sr.lens = lens;
  sr.count = 5;
  sr.next = 0;

  probe_common(stub_read, &sr, 2000, 1, &r);

  ck_assert_int_eq(r.kind, PROBE_NAMED);
  ck_assert_int_eq(r.program_count, 2);
  ck_assert_uint_eq(r.programs[0].sid, 1u);
  ck_assert_str_eq(r.programs[0].name, "Channel One");
  ck_assert_uint_eq(r.programs[1].sid, 2u);
  ck_assert_str_eq(r.programs[1].name, "Channel Two");
}
END_TEST

static void write_cfg(char *path, const char *text) {
  int fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq((int)write(fd, text, strlen(text)), (int)strlen(text));
  close(fd);
}

START_TEST(config_file_provides_settings) {
  char path[] = "/tmp/dipiscan_cfg_XXXXXX";
  char *argv[] = {"dipiscan", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "mcast: 239.5.5.0\nport: 8000-8002\nformat: csv\ntimeout: 3\njets: 4\nmpts: on\nhttp-proxy: 10.0.0.1:8080\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.port_lo, 8000u);
  ck_assert_uint_eq(cfg.port_hi, 8002u);
  ck_assert_int_eq(cfg.format, OUT_CSV);
  ck_assert_int_eq(cfg.timeout_ms, 3000);
  ck_assert_uint_eq(cfg.jets, 4u);
  ck_assert_int_eq(cfg.mpts, 1);
  ck_assert_int_eq(cfg.http_proxy, 1);
  ck_assert_uint_eq(cfg.http_proxy_port, 8080u);
}
END_TEST

START_TEST(cmdline_wins_over_config) {
  char path[] = "/tmp/dipiscan_cfg_XXXXXX";
  char *argv[] = {"dipiscan", "-c", path, "-t", "9", NULL};
  config_t cfg;
  write_cfg(path, "port: 8000\ntimeout: 3\njets: 4\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.timeout_ms, 9000);
  ck_assert_uint_eq(cfg.port_lo, 8000u);
  ck_assert_uint_eq(cfg.jets, 4u);
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipiscan", "-c", "/nonexistent/dipiscan.yaml", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipiscan_cfg_XXXXXX";
  char *argv[] = {"dipiscan", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "jets: 0\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipiscan_cfg_XXXXXX";
  char *argv[] = {"dipiscan", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipiscan", "--configtest", "-c", "/nonexistent/dipiscan.yaml", NULL};
  config_t cfg;
  write_cfg(path, "bogus: 1\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

#define SCAN_STREAM_MAX 1024
#define SCAN_PATHS_MAX 16
#define SCAN_MSG_MAX 8192
#define SCAN_TIMEOUT_MS 3000

typedef struct {
  unsigned sid;
  const char *name;
} scan_service_t;

static size_t build_named_ts(unsigned char *out, const scan_service_t *svc, unsigned n) {
  unsigned char sec[256];
  psi_pat_entry_t pat[4];
  psi_sdt_entry_t sdt[4];
  size_t off = 0;
  size_t slen;

  for (unsigned i = 0; i < n; i++) {
    pat[i].program_number = svc[i].sid;
    pat[i].pmt_pid = 0x0100 + i * 0x10;
    sdt[i].service_id = svc[i].sid;
    sdt[i].service_type = 0x01;
    sdt[i].provider = "Provider";
    sdt[i].service_name = svc[i].name;
  }
  slen = psi_build_pat_multi(1, 0, pat, n, sec, sizeof sec);
  wrap_ts_packet(out + off, 0x0000, sec, slen);
  off += 188;
  for (unsigned i = 0; i < n; i++) {
    slen = build_pmt(sec, svc[i].sid, pat[i].pmt_pid + 1);
    wrap_ts_packet(out + off, pat[i].pmt_pid, sec, slen);
    off += 188;
  }
  slen = psi_build_sdt_multi(0, 1, 2, sdt, n, sec, sizeof sec);
  wrap_ts_packet(out + off, 0x0011, sec, slen);
  off += 188;
  return off;
}

typedef struct {
  int listen_fd;
  int conns;
  size_t (*respond)(const char *path, unsigned char *out, size_t cap, int *status);
  char paths[SCAN_PATHS_MAX][128];
  int n_paths;
} scan_http_server_t;

static int scan_listen_loopback(unsigned *port_out) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 16), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static void *scan_http_server_thread(void *arg) {
  scan_http_server_t *srv = arg;

  for (int i = 0; i < srv->conns; i++) {
    int cfd = accept(srv->listen_fd, NULL, NULL);
    struct timeval tv = {2, 0};
    char req[2048];
    size_t got = 0;
    char path[128] = "";
    unsigned char body[SCAN_STREAM_MAX];
    char head[160];
    size_t blen;
    int status = 200;

    if (cfd < 0) return NULL;
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    while (got < sizeof req - 1) {
      ssize_t n = recv(cfd, req + got, sizeof req - 1 - got, 0);

      if (n <= 0) break;
      got += (size_t)n;
      req[got] = '\0';
      if (strstr(req, "\r\n\r\n")) break;
    }
    req[got] = '\0';
    if (sscanf(req, "GET %127s", path) == 1 && srv->n_paths < SCAN_PATHS_MAX) snprintf(srv->paths[srv->n_paths++], sizeof srv->paths[0], "%s", path);
    blen = srv->respond(path, body, sizeof body, &status);
    if (status == 200)
      snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nConnection: close\r\n\r\n");
    else
      snprintf(head, sizeof head, "HTTP/1.1 %d Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status);
    send(cfd, head, strlen(head), MSG_NOSIGNAL);
    if (blen) send(cfd, body, blen, MSG_NOSIGNAL);
    close(cfd);
  }
  return NULL;
}

static size_t respond_single(const char *path, unsigned char *out, size_t cap, int *status) {
  static const scan_service_t svc[] = {{1, "Channel One"}};

  (void)path;
  (void)cap;
  *status = 200;
  return build_named_ts(out, svc, 1);
}

static size_t respond_mpts(const char *path, unsigned char *out, size_t cap, int *status) {
  static const scan_service_t svc[] = {{1, "Channel One"}, {2, "Channel Two"}};

  (void)path;
  (void)cap;
  *status = 200;
  return build_named_ts(out, svc, 2);
}

static size_t respond_not_found(const char *path, unsigned char *out, size_t cap, int *status) {
  (void)path;
  memset(out, 0, cap);
  *status = 404;
  return 0;
}

static size_t respond_empty_ok(const char *path, unsigned char *out, size_t cap, int *status) {
  (void)path;
  memset(out, 0, cap);
  *status = 200;
  return 0;
}

static size_t respond_by_last_octet(const char *path, unsigned char *out, size_t cap, int *status) {
  static const char *const names[] = {"Chan 0", "Chan 1", "Chan 2", "Chan 3", "Chan 4"};
  scan_service_t svc;
  unsigned octet = 0;

  (void)cap;
  sscanf(path, "/udp/239.1.2.%u:", &octet);
  svc.sid = octet;
  svc.name = names[octet % 5];
  *status = 200;
  return build_named_ts(out, &svc, 1);
}

typedef struct {
  scan_http_server_t srv;
  pthread_t th;
  unsigned port;
} scan_http_fixture_t;

static void scan_http_start(scan_http_fixture_t *fx, int conns, size_t (*respond)(const char *, unsigned char *, size_t, int *)) {
  memset(&fx->srv, 0, sizeof fx->srv);
  fx->srv.listen_fd = scan_listen_loopback(&fx->port);
  fx->srv.conns = conns;
  fx->srv.respond = respond;
  ck_assert_int_eq(pthread_create(&fx->th, NULL, scan_http_server_thread, &fx->srv), 0);
}

static void scan_http_stop(const scan_http_fixture_t *fx) {
  pthread_join(fx->th, NULL);
  close(fx->srv.listen_fd);
}

static void scan_http_cfg(config_t *cfg, const scan_http_fixture_t *fx, const char *range) {
  scan_cfg_defaults(cfg);
  ck_assert_int_eq(scan_cfg_mcast(cfg, range), 0);
  cfg->http_proxy = 1;
  snprintf(cfg->http_proxy_host, sizeof cfg->http_proxy_host, "127.0.0.1");
  cfg->http_proxy_port = fx->port;
  cfg->timeout_ms = SCAN_TIMEOUT_MS;
}

static int run_scan_capture(const config_t *cfg, char **out_buf, char *log, size_t log_cap) {
  size_t out_len = 0;
  FILE *f = open_memstream(out_buf, &out_len);
  int rc;

  ck_assert_ptr_nonnull(f);
  log_capture_begin();
  rc = scan_run(cfg, f);
  log_capture_end(log, log_cap);
  fclose(f);
  return rc;
}

START_TEST(scan_run_http_single_program_stream) {
  scan_http_fixture_t fx;
  config_t cfg;
  char *out = NULL;
  char log[SCAN_MSG_MAX];

  scan_http_start(&fx, 1, respond_single);
  scan_http_cfg(&cfg, &fx, "239.1.2.5-239.1.2.5");
  ck_assert_int_eq(run_scan_capture(&cfg, &out, log, sizeof log), 0);
  scan_http_stop(&fx);
  ck_assert_int_eq(fx.srv.n_paths, 1);
  ck_assert_str_eq(fx.srv.paths[0], "/udp/239.1.2.5:8700/");
  ck_assert_ptr_nonnull(strstr(out, "Channel One"));
  ck_assert_ptr_nonnull(strstr(out, "udp://@239.1.2.5:8700"));
  ck_assert_ptr_nonnull(strstr(out, "sid=\"1\""));
  ck_assert_ptr_nonnull(strstr(log, "found 1 station"));
  free(out);
}
END_TEST

START_TEST(scan_run_http_mpts_reports_every_program) {
  scan_http_fixture_t fx;
  config_t cfg;
  char *out = NULL;
  char log[SCAN_MSG_MAX];

  scan_http_start(&fx, 1, respond_mpts);
  scan_http_cfg(&cfg, &fx, "239.1.2.5-239.1.2.5");
  cfg.mpts = 1;
  ck_assert_int_eq(run_scan_capture(&cfg, &out, log, sizeof log), 0);
  scan_http_stop(&fx);
  ck_assert_ptr_nonnull(strstr(out, "Channel One"));
  ck_assert_ptr_nonnull(strstr(out, "Channel Two"));
  ck_assert_ptr_nonnull(strstr(out, "sid=\"1\""));
  ck_assert_ptr_nonnull(strstr(out, "sid=\"2\""));
  ck_assert_ptr_nonnull(strstr(log, "found 2 stations"));
  free(out);
}
END_TEST

static size_t (*const missing_stream_responders[])(const char *, unsigned char *, size_t, int *) = {respond_not_found, respond_empty_ok};

START_TEST(scan_run_http_missing_stream_reports_no_stream) {
  scan_http_fixture_t fx;
  config_t cfg;
  char *out = NULL;
  char log[SCAN_MSG_MAX];

  scan_http_start(&fx, 1, missing_stream_responders[_i]);
  scan_http_cfg(&cfg, &fx, "239.1.2.5-239.1.2.5");
  ck_assert_int_eq(run_scan_capture(&cfg, &out, log, sizeof log), 0);
  scan_http_stop(&fx);
  ck_assert_ptr_nonnull(strstr(log, "no stream"));
  ck_assert_ptr_nonnull(strstr(log, "found 0 stations"));
  ck_assert_ptr_null(strstr(out, "#EXTINF"));
  free(out);
}
END_TEST

typedef struct {
  out_fmt_t fmt;
  const char *want_in_output;
} format_case_t;

static const format_case_t scan_format_cases[] = {
  {OUT_M3U, "#EXTINF:-1 tsid=\"1\" onid=\"2\" sid=\"1\",Channel One\nudp://@239.1.2.5:8700"},
  {OUT_CSV, "Channel One,udp://@239.1.2.5:8700,1,2,1"},
  {OUT_XSPF, "<location>udp://@239.1.2.5:8700</location>"},
  {OUT_XML, "239.1.2.5"},
  {OUT_NULL, ""},
};

START_TEST(scan_run_http_writes_every_output_format) {
  const format_case_t *c = &scan_format_cases[_i];
  scan_http_fixture_t fx;
  config_t cfg;
  char *out = NULL;
  char log[SCAN_MSG_MAX];

  scan_http_start(&fx, 1, respond_single);
  scan_http_cfg(&cfg, &fx, "239.1.2.5-239.1.2.5");
  cfg.format = c->fmt;
  cfg.provider = "example.org";
  ck_assert_int_eq(run_scan_capture(&cfg, &out, log, sizeof log), 0);
  scan_http_stop(&fx);
  if (c->fmt == OUT_NULL) {
    ck_assert_uint_eq(strlen(out), 0u);
  } else {
    ck_assert_ptr_nonnull(strstr(out, c->want_in_output));
    if (c->fmt == OUT_XML) ck_assert_ptr_nonnull(strstr(out, "Channel One"));
  }
  free(out);
}
END_TEST

START_TEST(scan_run_http_path_template_is_expanded) {
  scan_http_fixture_t fx;
  config_t cfg;
  char *out = NULL;
  char log[SCAN_MSG_MAX];

  scan_http_start(&fx, 1, respond_single);
  scan_http_cfg(&cfg, &fx, "239.1.2.7-239.1.2.7");
  cfg.http_path_tmpl = "/stream/%g/%p/100%%";
  cfg.port_lo = 9001;
  cfg.port_hi = 9001;
  ck_assert_int_eq(run_scan_capture(&cfg, &out, log, sizeof log), 0);
  scan_http_stop(&fx);
  ck_assert_int_eq(fx.srv.n_paths, 1);
  ck_assert_str_eq(fx.srv.paths[0], "/stream/239.1.2.7/9001/100%");
  free(out);
}
END_TEST

START_TEST(scan_run_http_parallel_jets_keep_address_order) {
  scan_http_fixture_t fx;
  config_t cfg;
  char *out = NULL;
  char log[SCAN_MSG_MAX];
  const char *last = NULL;

  scan_http_start(&fx, 4, respond_by_last_octet);
  scan_http_cfg(&cfg, &fx, "239.1.2.1-239.1.2.4");
  cfg.jets = 3;
  ck_assert_int_eq(run_scan_capture(&cfg, &out, log, sizeof log), 0);
  scan_http_stop(&fx);
  for (int i = 1; i <= 4; i++) {
    char name[16];
    const char *at;

    snprintf(name, sizeof name, "Chan %d", i);
    at = strstr(out, name);
    ck_assert_ptr_nonnull(at);
    if (last) ck_assert(at > last);
    last = at;
  }
  ck_assert_ptr_nonnull(strstr(log, "found 4 stations"));
  free(out);
}
END_TEST

START_TEST(scan_run_stop_request_reports_interrupted) {
  config_t cfg;
  char *out = NULL;
  char log[SCAN_MSG_MAX];

  scan_cfg_defaults(&cfg);
  ck_assert_int_eq(scan_cfg_mcast(&cfg, "239.1.2.1-239.1.2.3"), 0);
  signals_install();
  raise(SIGINT);
  ck_assert_int_eq(run_scan_capture(&cfg, &out, log, sizeof log), 1);
  ck_assert_ptr_nonnull(strstr(log, "interrupted: found 0 stations (of 0 probed)"));
  free(out);
}
END_TEST

static unsigned scan_free_udp_port(void) {
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

typedef struct {
  struct sockaddr_in dst;
  unsigned char dgram[12 + SCAN_STREAM_MAX];
  size_t len;
  atomic_int stop;
} mcast_feeder_t;

static void *mcast_feeder_thread(void *arg) {
  mcast_feeder_t *fd = arg;
  int sock = socket(AF_INET, SOCK_DGRAM, 0);

  while (!atomic_load(&fd->stop)) {
    struct timespec ts = {0, 20000000L};

    sendto(sock, fd->dgram, fd->len, 0, (struct sockaddr *)&fd->dst, sizeof fd->dst);
    nanosleep(&ts, NULL);
  }
  close(sock);
  return NULL;
}

START_TEST(scan_run_multicast_probe_udp_and_rtp) {
  static const scan_service_t svc[] = {{1, "Channel One"}};
  mcast_feeder_t feeder;
  pthread_t th;
  config_t cfg;
  char group[32];
  char range[80];
  char want[64];
  char *out = NULL;
  char log[SCAN_MSG_MAX];
  unsigned port = scan_free_udp_port();
  size_t hdr = _i ? 12 : 0;

  snprintf(group, sizeof group, "239.79.%u.%u", ((unsigned)getpid() >> 8) & 0xFF, 1 + ((unsigned)getpid() & 0xFF) % 250);
  snprintf(range, sizeof range, "%s-%s", group, group);
  memset(&feeder, 0, sizeof feeder);
  feeder.dst.sin_family = AF_INET;
  feeder.dst.sin_port = htons((unsigned short)port);
  ck_assert_int_eq(inet_pton(AF_INET, group, &feeder.dst.sin_addr), 1);
  if (hdr) {
    feeder.dgram[0] = 0x80;
    feeder.dgram[1] = 33;
    feeder.dgram[3] = 1;
    feeder.dgram[11] = 9;
  }
  feeder.len = hdr + build_named_ts(feeder.dgram + hdr, svc, 1);
  atomic_init(&feeder.stop, 0);

  scan_cfg_defaults(&cfg);
  ck_assert_int_eq(scan_cfg_mcast(&cfg, range), 0);
  cfg.port_lo = port;
  cfg.port_hi = port;
  cfg.timeout_ms = SCAN_TIMEOUT_MS;
  ck_assert_int_eq(pthread_create(&th, NULL, mcast_feeder_thread, &feeder), 0);
  ck_assert_int_eq(run_scan_capture(&cfg, &out, log, sizeof log), 0);
  atomic_store(&feeder.stop, 1);
  pthread_join(th, NULL);

  snprintf(want, sizeof want, "%s://@%s:%u", hdr ? "rtp" : "udp", group, port);
  ck_assert_ptr_nonnull(strstr(out, want));
  ck_assert_ptr_nonnull(strstr(out, "Channel One"));
  free(out);
}
END_TEST

static Suite *scan_suite(void) {
  Suite *s = suite_create("dipiscan_scan");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, addr_at_sweeps_last_octet_ipv4);
  tcase_add_test(tc, addr_at_sweeps_last_octet_ipv6);
  tcase_add_test(tc, addr_at_carries_across_bytes);
  tcase_add_test(tc, mcast_parse_plain_address_sweeps_default_24);
  tcase_add_test(tc, mcast_parse_cidr_sweeps_host_range);
  tcase_add_test(tc, mcast_parse_cidr_rejects_range_over_cap);
  tcase_add_test(tc, mcast_parse_explicit_range);
  tcase_add_test(tc, mcast_parse_explicit_range_rejects_reversed);
  tcase_add_test(tc, multi_all_named_false_without_pat);
  tcase_add_test(tc, multi_all_named_false_when_multi_program_not_enabled);
  tcase_add_test(tc, multi_all_named_false_until_every_program_resolved_and_named);
  tcase_add_test(tc, multi_all_named_false_when_only_some_programs_named);
  tcase_add_test(tc, probe_cb_single_mode_stops_once_named);
  tcase_add_test(tc, probe_common_resolves_named_single_program);
  tcase_add_test(tc, probe_common_detects_rtp_wrapping_and_strips_header);
  tcase_add_test(tc, probe_common_times_out_with_no_data);
  tcase_add_test(tc, probe_common_first_packet_wait_scales_with_timeout);
  tcase_add_test(tc, probe_common_multi_mode_resolves_every_program);
  tcase_add_test(tc, config_file_provides_settings);
  tcase_add_test(tc, cmdline_wins_over_config);
  tcase_add_test(tc, config_missing_file_is_error);
  tcase_add_test(tc, config_invalid_value_is_error);
  tcase_add_test(tc, configtest_reports_by_exit_status);
  tcase_add_test(tc, scan_run_http_single_program_stream);
  tcase_add_test(tc, scan_run_http_mpts_reports_every_program);
  tcase_add_loop_test(tc, scan_run_http_missing_stream_reports_no_stream, 0, (int)(sizeof missing_stream_responders / sizeof missing_stream_responders[0]));
  tcase_add_loop_test(tc, scan_run_http_writes_every_output_format, 0, (int)(sizeof scan_format_cases / sizeof scan_format_cases[0]));
  tcase_add_test(tc, scan_run_http_path_template_is_expanded);
  tcase_add_test(tc, scan_run_http_parallel_jets_keep_address_order);
  tcase_add_test(tc, scan_run_stop_request_reports_interrupted);
  tcase_add_loop_test(tc, scan_run_multicast_probe_udp_and_rtp, 0, 2);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(scan_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
