/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sds_xml.h"

START_TEST(sds_broadcast_round_trips_multiple_services) {
  sds_service_t svcs[2], out[8];
  unsigned char buf[4096];
  size_t len;
  int n;

  memset(svcs, 0, sizeof svcs);
  snprintf(svcs[0].name, sizeof svcs[0].name, "ORFeins");
  snprintf(svcs[0].address, sizeof svcs[0].address, "239.1.1.1");
  svcs[0].port = 5000;
  svcs[0].rtp = 1;
  svcs[0].tsid = 1;
  svcs[0].onid = 2;
  svcs[0].sid = 101;

  snprintf(svcs[1].name, sizeof svcs[1].name, "ORF2 Bundesland");
  snprintf(svcs[1].address, sizeof svcs[1].address, "ff15::1");
  svcs[1].port = 5001;
  svcs[1].rtp = 0;
  svcs[1].tsid = 1;
  svcs[1].onid = 2;
  svcs[1].sid = 102;

  len = sds_build_broadcast("example.invalid", 1, svcs, 2, NULL, NULL, buf, sizeof buf);
  ck_assert_uint_gt(len, 0u);

  n = sds_parse_broadcast((const char *)buf, out, 8);
  ck_assert_int_eq(n, 2);

  ck_assert_str_eq(out[0].name, "ORFeins");
  ck_assert_str_eq(out[0].address, "239.1.1.1");
  ck_assert_uint_eq(out[0].port, 5000u);
  ck_assert_int_eq(out[0].rtp, 1);
  ck_assert_uint_eq(out[0].tsid, 1u);
  ck_assert_uint_eq(out[0].onid, 2u);
  ck_assert_uint_eq(out[0].sid, 101u);

  ck_assert_str_eq(out[1].name, "ORF2 Bundesland");
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
  int n = sds_parse_broadcast(xml, out, 4);
  ck_assert_int_eq(n, 1);
  ck_assert_uint_eq(out[0].onid, 1u);
  ck_assert_uint_eq(out[0].tsid, 1u);
  ck_assert_uint_eq(out[0].sid, 1u); /* 1-based index */
}
END_TEST

static Suite *sds_xml_suite(void) {
  Suite *s = suite_create("sds_xml");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, sds_broadcast_round_trips_multiple_services);
  tcase_add_test(tc, sds_broadcast_includes_ret_and_fcc_elements_when_present);
  tcase_add_test(tc, sds_broadcast_omits_ret_fcc_elements_when_absent);
  tcase_add_test(tc, sds_build_broadcast_rejects_small_cap);
  tcase_add_test(tc, sds_build_sp_contains_expected_fields);
  tcase_add_test(tc, sds_build_sp_rejects_small_cap);
  tcase_add_test(tc, sds_parse_broadcast_defaults_missing_ids);
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
