/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "lib/bim/sdns.h"
#include "lib/helper/sds_xml.h"

static void put_bits(bitwriter_t *bw, const char *bits) {
  for (const char *p = bits; *p; p++)
    if (*p == '0' || *p == '1') bitwriter_put(bw, *p == '1', 1);
}

/* TS 102 822-3-2 4.4.2.4.4 */
static void put_zchunk(bitwriter_t *bw, const char *strs, size_t len) {
  unsigned char z[512];
  uLongf zl = sizeof z;
  ck_assert_int_eq(compress(z, &zl, (const unsigned char *)strs, (uLong)len), Z_OK);
  bitwriter_put_vluimsbf8(bw, zl);
  bitwriter_put_bytes(bw, z, zl);
}

/* TS 102 323 9.4.2.3 */
static const unsigned char *wrap_au(bitwriter_t *au, bitwriter_t *payload, unsigned nfuu, unsigned ctx, size_t *len) {
  size_t plen;
  const unsigned char *p;
  bitwriter_flush(payload);
  p = bitwriter_data(payload, &plen);
  bitwriter_put_vluimsbf8(au, nfuu);
  bitwriter_put_vluimsbf8(au, plen);
  bitwriter_put(au, ctx, 16);
  bitwriter_put_bytes(au, p, plen);
  return bitwriter_data(au, len);
}

/* 2008-1 BroadcastDiscovery, one service 239.1.1.1:5000 rtp, Ch1 */
static void put_broadcast(bitwriter_t *bw) {
  static const char strs[] = "example.com\0" "239.1.1.1\0" "Ch1";
  bitwriter_put(bw, 0x0F, 8);
  put_bits(bw, "0 1 001 00000"); /* @Version absent, record present, BroadcastDiscovery, count 1 */
  put_zchunk(bw, strs, sizeof strs); /* @DomainName */
  put_bits(bw, "0 00000"); /* @Version absent, ServiceList count 1 */
  put_bits(bw, "0 00000"); /* ServicesDescriptionLocation absent, SingleService count 1 */
  put_bits(bw, "00000 0"); /* ServiceLocation choice count 1, IPMulticastAddress */
  put_bits(bw, "0"); /* @Address member 0, string from buffer */
  put_bits(bw, "0 0 0"); /* @FECMaxBlockSize, @FECMaxBlockTime, @FECOTI absent */
  bitwriter_put(bw, 5000, 16); /* @Port */
  put_bits(bw, "0 1 0 0"); /* @Source absent, @Streaming rtp, content absent */
  put_bits(bw, "0"); /* TextualIdentifier @DomainName absent, @ServiceName from buffer */
  bitwriter_put(bw, 2, 16); /* DVBTriplet @OrigNetId */
  bitwriter_put(bw, 0x1234, 16); /* @ServiceId */
  bitwriter_put(bw, 7, 16); /* @TSId */
  put_bits(bw, "0 0 0 0 0"); /* MaxBitrate, SI, AudioAttributes, VideoAttributes, ServiceAvailability absent */
}

START_TEST(sdns_decodes_broadcast_discovery) {
  static const char *const ns[] = {"urn:dvb:metadata:iptv:sdns:2008-1", NULL};
  for (int i = 0; i < 2; i++) {
    bitwriter_t pl;
    bitwriter_t au;
    const unsigned char *a;
    size_t alen;
    char *xml = NULL;
    size_t xlen;
    sds_service_t s[2];
    bitwriter_init(&pl);
    bitwriter_init(&au);
    put_broadcast(&pl);
    a = wrap_au(&au, &pl, 1, SDNS_CTXPATH_SERVICE_DISCOVERY, &alen);
    ck_assert_int_eq(sdns_bim_to_xml(a, alen, ns[i], &xml, &xlen), 0);
    ck_assert_uint_eq(strlen(xml), xlen);
    ck_assert_ptr_nonnull(strstr(xml, "xmlns=\"urn:dvb:metadata:iptv:sdns:2008-1\""));
    ck_assert_int_eq(sds_parse_broadcast(xml, s, 2, NULL), 1);
    ck_assert_str_eq(s[0].address, "239.1.1.1");
    ck_assert_uint_eq(s[0].port, 5000);
    ck_assert_str_eq(s[0].name, "Ch1");
    ck_assert_int_eq(s[0].rtp, 1);
    ck_assert_uint_eq(s[0].onid, 2);
    ck_assert_uint_eq(s[0].tsid, 7);
    ck_assert_uint_eq(s[0].sid, 0x1234);
    free(xml);
    bitwriter_free(&pl);
    bitwriter_free(&au);
  }
}
END_TEST

/* TS 102 034 5.5.2.3 */
START_TEST(sdns_rejects_bad_framing) {
  static const struct {
    unsigned nfuu;
    unsigned ctx;
    const char *tail;
  } cases[] = {{2, SDNS_CTXPATH_SERVICE_DISCOVERY, ""}, {1, 0x0031, ""}, {1, SDNS_CTXPATH_SERVICE_DISCOVERY, "00000000 1"}};
  for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
    bitwriter_t pl;
    bitwriter_t au;
    const unsigned char *a;
    size_t alen;
    char *xml = NULL;
    size_t xlen;
    bitwriter_init(&pl);
    bitwriter_init(&au);
    put_broadcast(&pl);
    put_bits(&pl, cases[c].tail);
    a = wrap_au(&au, &pl, cases[c].nfuu, cases[c].ctx, &alen);
    ck_assert_int_eq(sdns_bim_to_xml(a, alen, NULL, &xml, &xlen), -1);
    ck_assert_ptr_null(xml);
    bitwriter_free(&pl);
    bitwriter_free(&au);
  }
}
END_TEST

/* string spans two Zlib chunks */
START_TEST(sdns_zlib_string_spans_chunks) {
  static const char a[] = "1.";
  static const char b[] = "5";
  bitwriter_t pl;
  bitwriter_t au;
  const unsigned char *p;
  size_t plen;
  char *xml = NULL;
  size_t xlen;
  bitwriter_init(&pl);
  bitwriter_init(&au);
  bitwriter_put(&pl, 0x0F, 8);
  put_bits(&pl, "1"); /* @Version present */
  put_zchunk(&pl, a, sizeof a - 1);
  put_zchunk(&pl, b, sizeof b);
  put_bits(&pl, "0"); /* no record */
  p = wrap_au(&au, &pl, 1, SDNS_CTXPATH_SERVICE_DISCOVERY, &plen);
  ck_assert_int_eq(sdns_bim_to_xml(p, plen, "urn:dvb:metadata:iptv:sdns:2008-1", &xml, &xlen), 0);
  ck_assert_ptr_nonnull(strstr(xml, "<ServiceDiscovery "));
  ck_assert_ptr_nonnull(strstr(xml, "Version=\"1.5\""));
  free(xml);
  bitwriter_free(&pl);
  bitwriter_free(&au);
}
END_TEST

/* ISO 23001-1 7.3.2: hasNoFragmentReference 0 announces a FragmentReference */
START_TEST(sdns_rejects_fragment_reference) {
  static const struct {
    unsigned modes;
    int rc;
  } cases[] = {{0x0F, 0}, {0x07, -1}};
  for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
    bitwriter_t pl;
    bitwriter_t au;
    const unsigned char *p;
    size_t plen;
    char *xml = NULL;
    size_t xlen;
    bitwriter_init(&pl);
    bitwriter_init(&au);
    bitwriter_put(&pl, cases[c].modes, 8);
    put_bits(&pl, "0 0"); /* @Version absent, no record */
    p = wrap_au(&au, &pl, 1, SDNS_CTXPATH_SERVICE_DISCOVERY, &plen);
    ck_assert_int_eq(sdns_bim_to_xml(p, plen, "urn:dvb:metadata:iptv:sdns:2008-1", &xml, &xlen), cases[c].rc);
    free(xml);
    bitwriter_free(&pl);
    bitwriter_free(&au);
  }
}
END_TEST

/* optional attribute `name` reachable on any element named `elem` */
static int has_opt_attr(const bim_type_t *t, const char *elem, const char *name, int depth);

static int part_has_opt_attr(const bim_part_t *p, const char *elem, const char *name, int depth) {
  const char *local;
  if (p->kind != BIM_PART_ELEM) {
    for (int i = 0; i < p->nkids; i++)
      if (part_has_opt_attr(&p->kids[i], elem, name, depth)) return 1;
    return 0;
  }
  if (!p->type) return 0;
  local = strchr(p->name, ':') ? strchr(p->name, ':') + 1 : p->name;
  if (!strcmp(local, elem))
    for (int i = 0; i < p->type->nattrs; i++)
      if (!strcmp(p->type->attrs[i].name, name) && !p->type->attrs[i].required) return 1;
  return has_opt_attr(p->type, elem, name, depth + 1);
}

static int has_opt_attr(const bim_type_t *t, const char *elem, const char *name, int depth) {
  return depth < 16 && t->content == BIM_CONTENT_SEQ && t->top && part_has_opt_attr(t->top, elem, name, depth);
}

/* ISO 23001-1 Cor 1 7.5.3.1: fixed optional dvb12:DVBTriplet@TSIdWildcard coded */
START_TEST(sdns_fixed_optional_attr_coded) {
  static const struct {
    const char *ns;
    int has;
  } cases[] = {{"urn:dvb:ipi:sdns:2006", 0},
               {"urn:dvb:metadata:iptv:sdns:2008-1", 0},
               {"urn:dvb:metadata:iptv:sdns:2012-1", 1},
               {"urn:dvb:metadata:iptv:sdns:2014-1", 1}};
  for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
    const sdns_set_t *set = sdns_set_find(cases[c].ns);
    ck_assert_ptr_nonnull(set);
    ck_assert_int_eq(has_opt_attr(set->root, "DVBTriplet", "TSIdWildcard", 0), cases[c].has);
  }
}
END_TEST

START_TEST(sdns_sets_newest_first) {
  ck_assert_int_eq(sdns_nsets, 4);
  ck_assert_str_eq(sdns_sets[0].ns, "urn:dvb:metadata:iptv:sdns:2014-1");
  ck_assert_str_eq(sdns_sets[3].ns, "urn:dvb:ipi:sdns:2006");
  ck_assert_ptr_eq(sdns_set_find("urn:dvb:metadata:iptv:sdns:2012-1"), &sdns_sets[1]);
  ck_assert_ptr_null(sdns_set_find("urn:dvb:ipisdns:2003"));
}
END_TEST

START_TEST(sdns_segment_and_doc_open_error_paths) {
  bitwriter_t pl;
  bitwriter_t au;
  const unsigned char *a;
  size_t alen;
  const char *xml = NULL;
  size_t xlen = 0;
  char *owned = NULL;
  bim_sdnsdoc_t d;
  char empty[] = "/tmp/dvbipitools_test_sdns_emptyXXXXXX";
  const char *paths[1];
  ck_assert_int_eq(sdns_segment_xml(2, (const unsigned char *)"x", 1, NULL, &xml, &xlen, &owned), -2);
  ck_assert_int_eq(sdns_segment_xml(0, (const unsigned char *)"<x/>", 4, NULL, &xml, &xlen, &owned), 0);
  ck_assert_uint_eq(xlen, 4u);
  ck_assert_int_eq(sdns_segment_xml(1, (const unsigned char *)"garbage", 7, NULL, &xml, &xlen, &owned), -1);
  ck_assert_int_eq(sdns_doc_segment_xml(NULL, 0, (const unsigned char *)"<x/>", 4, NULL, &xml, &xlen, &owned), 0);
  bitwriter_init(&pl);
  bitwriter_init(&au);
  put_broadcast(&pl);
  a = wrap_au(&au, &pl, 1, SDNS_CTXPATH_SERVICE_DISCOVERY, &alen);
  ck_assert_int_eq(sdns_segment_xml(1, a, alen, "urn:unknown:ns", &xml, &xlen, &owned), -1);
  ck_assert_int_eq(sdns_segment_xml(1, a, alen, "urn:dvb:metadata:iptv:sdns:2008-1", &xml, &xlen, &owned), 0);
  free(owned);
  bitwriter_free(&pl);
  bitwriter_free(&au);
  ck_assert_ptr_nonnull(mkdtemp(empty));
  paths[0] = empty;
  ck_assert_int_eq(sdns_doc_open(&d, "AS", paths, 1, NULL), -1);
  ck_assert_int_eq(sdns_doc_open(&d, "AS", paths, 1, "urn:dvb:metadata:iptv:sdns:2008-1"), -1);
  rmdir(empty);
}
END_TEST

static Suite *sdns_suite(void) {
  Suite *s = suite_create("bim_sdns");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, sdns_decodes_broadcast_discovery);
  tcase_add_test(tc, sdns_rejects_bad_framing);
  tcase_add_test(tc, sdns_zlib_string_spans_chunks);
  tcase_add_test(tc, sdns_rejects_fragment_reference);
  tcase_add_test(tc, sdns_fixed_optional_attr_coded);
  tcase_add_test(tc, sdns_sets_newest_first);
  tcase_add_test(tc, sdns_segment_and_doc_open_error_paths);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(sdns_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
