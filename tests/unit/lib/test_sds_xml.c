/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "lib/sds_xml.h"

START_TEST(sds_broadcast_round_trips_multiple_services) {
  sds_service_t svcs[2], out[8];
  unsigned char buf[4096];
  size_t len;
  int n;

  memset(svcs, 0, sizeof svcs);
  snprintf(svcs[0].name, sizeof svcs[0].name, "Channel One");
  snprintf(svcs[0].address, sizeof svcs[0].address, "239.1.1.1");
  svcs[0].port = 5000;
  svcs[0].rtp = 1;
  svcs[0].tsid = 1;
  svcs[0].onid = 2;
  svcs[0].sid = 101;

  snprintf(svcs[1].name, sizeof svcs[1].name, "Channel 2 Regional");
  snprintf(svcs[1].address, sizeof svcs[1].address, "ff15::1");
  svcs[1].port = 5001;
  svcs[1].rtp = 0;
  svcs[1].tsid = 1;
  svcs[1].onid = 2;
  svcs[1].sid = 102;

  len = sds_build_broadcast("example.invalid", 1, svcs, 2, NULL, NULL, buf, sizeof buf);
  ck_assert_uint_gt(len, 0u);

  n = sds_parse_broadcast((const char *)buf, out, 8, NULL);
  ck_assert_int_eq(n, 2);

  ck_assert_str_eq(out[0].name, "Channel One");
  ck_assert_str_eq(out[0].address, "239.1.1.1");
  ck_assert_uint_eq(out[0].port, 5000u);
  ck_assert_int_eq(out[0].rtp, 1);
  ck_assert_uint_eq(out[0].tsid, 1u);
  ck_assert_uint_eq(out[0].onid, 2u);
  ck_assert_uint_eq(out[0].sid, 101u);

  ck_assert_str_eq(out[1].name, "Channel 2 Regional");
  ck_assert_str_eq(out[1].address, "ff15::1");
  ck_assert_uint_eq(out[1].port, 5001u);
  ck_assert_int_eq(out[1].rtp, 0);
  ck_assert_uint_eq(out[1].sid, 102u);
}
END_TEST

START_TEST(sds_broadcast_includes_ret_and_fcc_elements_when_present) {
  sds_service_t svc;
  sds_ret_t ret;
  sds_fcc_t fcc;
  unsigned char buf[4096];
  size_t len;

  memset(&svc, 0, sizeof svc);
  snprintf(svc.name, sizeof svc.name, "Test");
  snprintf(svc.address, sizeof svc.address, "239.1.1.1");
  svc.port = 5000;
  svc.rtp = 1;

  memset(&ret, 0, sizeof ret);
  snprintf(ret.addr, sizeof ret.addr, "10.0.0.1");
  ret.port = 6000;
  ret.rtx_time_ms = 2000;
  ret.rtx_pt = 99;

  memset(&fcc, 0, sizeof fcc);
  snprintf(fcc.addr, sizeof fcc.addr, "10.0.0.2");
  fcc.port = 6001;
  fcc.rtx_time_ms = 3000;
  fcc.rtx_pt = 98;

  len = sds_build_broadcast("example.invalid", 1, &svc, 1, &ret, &fcc, buf, sizeof buf);
  ck_assert_uint_gt(len, 0u);

  ck_assert_ptr_nonnull(strstr((char *)buf, "<RTPRetransmission>"));
  ck_assert_ptr_nonnull(strstr((char *)buf, "DestinationAddress=\"10.0.0.1\""));
  ck_assert_ptr_nonnull(strstr((char *)buf, "<ServerBasedEnhancementServiceInfo>"));
  ck_assert_ptr_nonnull(strstr((char *)buf, "DestinationAddress=\"10.0.0.2\""));
}
END_TEST

START_TEST(sds_broadcast_omits_dvb_rsi_mc_ret_by_default) {
  sds_service_t svc;
  sds_ret_t ret;
  unsigned char buf[4096];
  size_t len;

  memset(&svc, 0, sizeof svc);
  snprintf(svc.address, sizeof svc.address, "239.1.1.1");
  svc.port = 5000;

  memset(&ret, 0, sizeof ret);
  snprintf(ret.addr, sizeof ret.addr, "10.0.0.1");
  ret.port = 6000;

  len = sds_build_broadcast("example.invalid", 1, &svc, 1, &ret, NULL, buf, sizeof buf);
  ck_assert_uint_gt(len, 0u);
  ck_assert_ptr_null(strstr((char *)buf, "dvb-rsi-mc-ret"));
}
END_TEST

START_TEST(sds_broadcast_includes_dvb_rsi_mc_ret_when_set) {
  sds_service_t svc;
  sds_ret_t ret;
  unsigned char buf[4096];
  size_t len;

  memset(&svc, 0, sizeof svc);
  snprintf(svc.address, sizeof svc.address, "239.1.1.1");
  svc.port = 5000;

  memset(&ret, 0, sizeof ret);
  snprintf(ret.addr, sizeof ret.addr, "10.0.0.1");
  ret.port = 6000;
  ret.mc = 1;
  ret.rsi_mc_ret = 1;

  len = sds_build_broadcast("example.invalid", 1, &svc, 1, &ret, NULL, buf, sizeof buf);
  ck_assert_uint_gt(len, 0u);
  ck_assert_ptr_nonnull(strstr((char *)buf, "dvb-rsi-mc-ret=\"true\""));
}
END_TEST

START_TEST(sds_broadcast_fcc_resolve_by_port_stays_in_range) {
  sds_service_t svc;
  sds_fcc_t fcc;
  unsigned char buf[4096];
  char needle[32];
  size_t len, i;
  unsigned port = 0;

  memset(&svc, 0, sizeof svc);
  svc.family = AF_INET;
  snprintf(svc.address, sizeof svc.address, "239.1.1.1");
  svc.port = 5000;

  memset(&fcc, 0, sizeof fcc);
  fcc.resolve_by_port = 1;
  fcc.resolve_base_port = 7000;
  fcc.resolve_max_channels = 16;

  len = sds_build_broadcast("example.invalid", 1, &svc, 1, NULL, &fcc, buf, sizeof buf);
  ck_assert_uint_gt(len, 0u);

  for (i = 0; i < fcc.resolve_max_channels; i++) {
    snprintf(needle, sizeof needle, "DestinationPort=\"%u\"", (unsigned)(fcc.resolve_base_port + i));
    if (strstr((char *)buf, needle)) {
      port = fcc.resolve_base_port + (unsigned)i;
      break;
    }
  }
  ck_assert_uint_ne(port, 0u);
  ck_assert_uint_ge(port, fcc.resolve_base_port);
  ck_assert_uint_lt(port, fcc.resolve_base_port + fcc.resolve_max_channels);
  ck_assert_ptr_null(strstr((char *)buf, "DestinationPort=\"0\""));
}
END_TEST

START_TEST(sds_broadcast_fcc_resolve_by_port_is_deterministic) {
  sds_service_t svc;
  sds_fcc_t fcc;
  unsigned char buf1[4096], buf2[4096];
  size_t len1, len2;

  memset(&svc, 0, sizeof svc);
  svc.family = AF_INET;
  snprintf(svc.address, sizeof svc.address, "239.5.6.7");
  svc.port = 5001;

  memset(&fcc, 0, sizeof fcc);
  fcc.resolve_by_port = 1;
  fcc.resolve_base_port = 7000;
  fcc.resolve_max_channels = 384;

  len1 = sds_build_broadcast("example.invalid", 1, &svc, 1, NULL, &fcc, buf1, sizeof buf1);
  len2 = sds_build_broadcast("example.invalid", 1, &svc, 1, NULL, &fcc, buf2, sizeof buf2);
  ck_assert_uint_eq(len1, len2);
  ck_assert_mem_eq(buf1, buf2, len1);
}
END_TEST

START_TEST(sds_broadcast_fcc_resolve_by_port_ignores_literal_port) {
  sds_service_t svc;
  sds_fcc_t fcc;
  unsigned char buf[4096];
  size_t len;

  memset(&svc, 0, sizeof svc);
  svc.family = AF_INET;
  snprintf(svc.address, sizeof svc.address, "239.1.1.1");
  svc.port = 5000;

  memset(&fcc, 0, sizeof fcc);
  fcc.port = 6001; /* must not appear: resolve_by_port overrides it */
  fcc.resolve_by_port = 1;
  fcc.resolve_base_port = 7000;
  fcc.resolve_max_channels = 16;

  len = sds_build_broadcast("example.invalid", 1, &svc, 1, NULL, &fcc, buf, sizeof buf);
  ck_assert_uint_gt(len, 0u);
  ck_assert_ptr_null(strstr((char *)buf, "DestinationPort=\"6001\""));
}
END_TEST

START_TEST(sds_broadcast_omits_ret_fcc_elements_when_absent) {
  sds_service_t svc;
  unsigned char buf[4096];
  size_t len;

  memset(&svc, 0, sizeof svc);
  snprintf(svc.name, sizeof svc.name, "Test");
  snprintf(svc.address, sizeof svc.address, "239.1.1.1");
  svc.port = 5000;

  len = sds_build_broadcast("example.invalid", 1, &svc, 1, NULL, NULL, buf, sizeof buf);
  ck_assert_uint_gt(len, 0u);

  ck_assert_ptr_null(strstr((char *)buf, "RTPRetransmission"));
  ck_assert_ptr_null(strstr((char *)buf, "ServerBasedEnhancementServiceInfo"));
}
END_TEST

START_TEST(sds_build_broadcast_rejects_small_cap) {
  sds_service_t svc;
  unsigned char buf[8];
  memset(&svc, 0, sizeof svc);
  snprintf(svc.address, sizeof svc.address, "239.1.1.1");
  ck_assert_uint_eq(sds_build_broadcast("example.invalid", 1, &svc, 1, NULL, NULL, buf, sizeof buf), 0u);
}
END_TEST

START_TEST(sds_build_sp_contains_expected_fields) {
  unsigned char buf[2048];
  size_t len = sds_build_sp("example.invalid", "My Provider", "deu", 1, "239.9.9.9", 7000, buf, sizeof buf);
  ck_assert_uint_gt(len, 0u);
  ck_assert_ptr_nonnull(strstr((char *)buf, "DomainName=\"example.invalid\""));
  ck_assert_ptr_nonnull(strstr((char *)buf, "Language=\"deu\""));
  ck_assert_ptr_nonnull(strstr((char *)buf, "My Provider"));
  ck_assert_ptr_nonnull(strstr((char *)buf, "Address=\"239.9.9.9\" Port=\"7000\""));
}
END_TEST

START_TEST(sds_build_sp_rejects_small_cap) {
  unsigned char buf[8];
  ck_assert_uint_eq(sds_build_sp("example.invalid", "My Provider", "deu", 1, "239.9.9.9", 7000, buf, sizeof buf), 0u);
}
END_TEST

START_TEST(sds_parse_broadcast_defaults_missing_ids) {
  /* no OrigNetId/TSId/ServiceId attributes at all */
  static const char xml[] =
      "<SingleService><ServiceLocation><IPMulticastAddress Address=\"239.1.1.1\" Port=\"5000\" Streaming=\"udp\"/>"
      "</ServiceLocation><TextualIdentifier ServiceName=\"X\"/></SingleService>";
  sds_service_t out[4];
  int n = sds_parse_broadcast(xml, out, 4, NULL);
  ck_assert_int_eq(n, 1);
  ck_assert_uint_eq(out[0].onid, 1u);
  ck_assert_uint_eq(out[0].tsid, 1u);
  ck_assert_uint_eq(out[0].sid, 1u); /* 1-based index */
}
END_TEST

START_TEST(sds_parse_broadcast_reports_truncation) {
  static const char xml[] =
      "<SingleService><ServiceLocation><IPMulticastAddress Address=\"239.1.1.1\" Port=\"5000\"/>"
      "</ServiceLocation></SingleService>"
      "<SingleService><ServiceLocation><IPMulticastAddress Address=\"239.1.1.2\" Port=\"5001\"/>"
      "</ServiceLocation></SingleService>"
      "<SingleService><ServiceLocation><IPMulticastAddress Address=\"239.1.1.3\" Port=\"5002\"/>"
      "</ServiceLocation></SingleService>";
  sds_service_t out[2];
  int truncated = -1;
  int n = sds_parse_broadcast(xml, out, 2, &truncated);
  ck_assert_int_eq(n, 2);
  ck_assert_int_eq(truncated, 1);
}
END_TEST

START_TEST(sds_parse_broadcast_not_truncated_when_it_fits) {
  static const char xml[] =
      "<SingleService><ServiceLocation><IPMulticastAddress Address=\"239.1.1.1\" Port=\"5000\"/>"
      "</ServiceLocation></SingleService>";
  sds_service_t out[2];
  int truncated = -1;
  int n = sds_parse_broadcast(xml, out, 2, &truncated);
  ck_assert_int_eq(n, 1);
  ck_assert_int_eq(truncated, 0);
}
END_TEST

static Suite *sds_xml_suite(void) {
  Suite *s = suite_create("sds_xml");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, sds_broadcast_round_trips_multiple_services);
  tcase_add_test(tc, sds_broadcast_includes_ret_and_fcc_elements_when_present);
  tcase_add_test(tc, sds_broadcast_fcc_resolve_by_port_stays_in_range);
  tcase_add_test(tc, sds_broadcast_fcc_resolve_by_port_is_deterministic);
  tcase_add_test(tc, sds_broadcast_fcc_resolve_by_port_ignores_literal_port);
  tcase_add_test(tc, sds_broadcast_omits_ret_fcc_elements_when_absent);
  tcase_add_test(tc, sds_broadcast_omits_dvb_rsi_mc_ret_by_default);
  tcase_add_test(tc, sds_broadcast_includes_dvb_rsi_mc_ret_when_set);
  tcase_add_test(tc, sds_build_broadcast_rejects_small_cap);
  tcase_add_test(tc, sds_build_sp_contains_expected_fields);
  tcase_add_test(tc, sds_build_sp_rejects_small_cap);
  tcase_add_test(tc, sds_parse_broadcast_defaults_missing_ids);
  tcase_add_test(tc, sds_parse_broadcast_reports_truncation);
  tcase_add_test(tc, sds_parse_broadcast_not_truncated_when_it_fits);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(sds_xml_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
