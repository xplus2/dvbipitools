/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/mux/psi_build.h"
#include "dipiscan/scan.h"

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
  ck_assert_uint_eq(cfg.total, 254u);
  inet_ntop(AF_INET, cfg.start, lo, sizeof lo);
  inet_ntop(AF_INET, cfg.end, hi, sizeof hi);
  ck_assert_str_eq(lo, "239.1.1.1");
  ck_assert_str_eq(hi, "239.1.1.254");
}
END_TEST

START_TEST(mcast_parse_cidr_sweeps_host_range) {
  config_t cfg;
  char argv0[] = "dipiscan", argv1[] = "-m", argv2[] = "239.1.0.0/23";
  char *argv[] = {argv0, argv1, argv2, NULL};
  char lo[64], hi[64];

  ck_assert_int_eq(args_parse(3, argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.total, 510u); /* 2^9 - 2 */
  inet_ntop(AF_INET, cfg.start, lo, sizeof lo);
  inet_ntop(AF_INET, cfg.end, hi, sizeof hi);
  ck_assert_str_eq(lo, "239.1.0.1");
  ck_assert_str_eq(hi, "239.1.1.254");
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
  tcase_add_test(tc, probe_common_multi_mode_resolves_every_program);
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
