/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "dipixy/core/route.h"

START_TEST(rtp_ts_direct_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/rtp/239.0.0.1:8000/ts", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_RTP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
  ck_assert_int_eq(r.family, AF_INET);
  ck_assert_str_eq(r.addr, "239.0.0.1");
  ck_assert_uint_eq(r.port, 8000u);
}
END_TEST

START_TEST(udp_spts_direct_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/udp/239.0.0.1:8000/spts", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_UDP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_SPTS);
}
END_TEST

START_TEST(udp_rawaudio_direct_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/udp/239.0.0.1:8000/rawaudio", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_UDP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_RAWAUDIO);
}
END_TEST

START_TEST(named_list_name_spts_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/mylist/name/BBC%20One/spts", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_LIST_NAME);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_SPTS);
  ck_assert_str_eq(r.src_name, "mylist");
  ck_assert_str_eq(r.item_name, "BBC One");
}
END_TEST

START_TEST(udp_hls_direct_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/udp/239.0.0.1:8000/hls", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_UDP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_HLS);
  ck_assert_str_eq(r.hls_file, "index.m3u8");
}
END_TEST

START_TEST(hls_segment_filename_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/rtp/239.0.0.1:8000/seg00042.ts", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_RTP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_HLS);
  ck_assert_str_eq(r.hls_file, "seg00042.ts");
}
END_TEST

START_TEST(hls_segment_filename_route_parses_for_list) {
  route_t r;
  ck_assert_int_eq(route_parse("/list/1/item/3/seg7.ts", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_LIST_ITEM);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_HLS);
  ck_assert_str_eq(r.hls_file, "seg7.ts");
}
END_TEST

START_TEST(malformed_segment_filename_rejected) {
  route_t r;
  ck_assert_int_ne(route_parse("/rtp/239.0.0.1:8000/seg.ts", &r), 0);
  ck_assert_int_ne(route_parse("/rtp/239.0.0.1:8000/seg1.mp4", &r), 0);
}
END_TEST

START_TEST(udp_hls_fmp4_direct_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/udp/239.0.0.1:8000/hls-fmp4", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_UDP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_HLS_FMP4);
  ck_assert_str_eq(r.hls_file, "index.m3u8");
}
END_TEST

START_TEST(udp_mp4_direct_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/udp/239.0.0.1:8000/mp4", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_UDP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_MP4);
}
END_TEST

START_TEST(hls_fmp4_init_filename_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/rtp/239.0.0.1:8000/init.mp4", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_RTP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_HLS_FMP4);
  ck_assert_str_eq(r.hls_file, "init.mp4");
}
END_TEST

START_TEST(hls_fmp4_segment_filename_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/list/1/item/3/seg7.m4s", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_LIST_ITEM);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_HLS_FMP4);
  ck_assert_str_eq(r.hls_file, "seg7.m4s");
}
END_TEST

START_TEST(malformed_fmp4_segment_filename_rejected) {
  route_t r;
  ck_assert_int_ne(route_parse("/rtp/239.0.0.1:8000/seg.m4s", &r), 0);
  ck_assert_int_ne(route_parse("/rtp/239.0.0.1:8000/init.mp5", &r), 0);
}
END_TEST

START_TEST(udp_llhls_direct_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/udp/239.0.0.1:8000/llhls", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_UDP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_LLHLS);
  ck_assert_str_eq(r.hls_file, "index_ll.m3u8");
}
END_TEST

START_TEST(llhls_part_filename_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/rtp/239.0.0.1:8000/seg00042.3.ts", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_RTP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_LLHLS);
  ck_assert_str_eq(r.hls_file, "seg00042.3.ts");
}
END_TEST

START_TEST(malformed_llhls_part_filename_rejected) {
  route_t r;
  ck_assert_int_ne(route_parse("/rtp/239.0.0.1:8000/seg42..ts", &r), 0);
  ck_assert_int_ne(route_parse("/rtp/239.0.0.1:8000/seg42.3.m4s", &r), 0);
}
END_TEST

START_TEST(udp_dash_direct_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/udp/239.0.0.1:8000/dash", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_UDP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_DASH);
  ck_assert_str_eq(r.hls_file, "manifest.mpd");
}
END_TEST

START_TEST(udp_lldash_direct_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/udp/239.0.0.1:8000/lldash", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_UDP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_LLDASH);
  ck_assert_str_eq(r.hls_file, "manifest.mpd");
}
END_TEST

START_TEST(dash_seg_filename_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/list/1/item/3/dseg12000.m4s", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_LIST_ITEM);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_DASH);
  ck_assert_str_eq(r.hls_file, "dseg12000.m4s");
}
END_TEST

START_TEST(malformed_dash_seg_filename_rejected) {
  route_t r;
  ck_assert_int_ne(route_parse("/rtp/239.0.0.1:8000/dseg.m4s", &r), 0);
  ck_assert_int_ne(route_parse("/rtp/239.0.0.1:8000/dseg12000.ts", &r), 0);
}
END_TEST

START_TEST(rtp_ipv6_bracket_address_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/rtp/[ff3e::1]:8000/llhls", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_RTP);
  ck_assert_int_eq(r.family, AF_INET6);
  ck_assert_str_eq(r.addr, "ff3e::1");
  ck_assert_int_eq(r.fmt, ROUTE_FMT_LLHLS);
}
END_TEST

START_TEST(srt_ts_direct_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/srt/192.0.2.1:9000/ts", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_SRT);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
  ck_assert_int_eq(r.family, AF_INET);
  ck_assert_str_eq(r.addr, "192.0.2.1");
  ck_assert_uint_eq(r.port, 9000u);
}
END_TEST

START_TEST(srt_ipv6_bracket_address_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/srt/[2001:db8::1]:9000/hls", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_SRT);
  ck_assert_int_eq(r.family, AF_INET6);
  ck_assert_str_eq(r.addr, "2001:db8::1");
}
END_TEST

START_TEST(srt_uri_numeric_ipv4_parses) {
  char host[64];
  unsigned port;
  ck_assert_int_eq(route_parse_srt_uri("srt://192.0.2.1:9000", host, sizeof host, &port), 0);
  ck_assert_str_eq(host, "192.0.2.1");
  ck_assert_uint_eq(port, 9000u);
}
END_TEST

START_TEST(srt_uri_bracketed_ipv6_parses) {
  char host[64];
  unsigned port;
  ck_assert_int_eq(route_parse_srt_uri("srt://[2001:db8::1]:9000", host, sizeof host, &port), 0);
  ck_assert_str_eq(host, "2001:db8::1");
  ck_assert_uint_eq(port, 9000u);
}
END_TEST

START_TEST(srt_uri_hostname_parses) {
  char host[64];
  unsigned port;
  ck_assert_int_eq(route_parse_srt_uri("srt://source.example.com:9000", host, sizeof host, &port), 0);
  ck_assert_str_eq(host, "source.example.com");
  ck_assert_uint_eq(port, 9000u);
}
END_TEST

START_TEST(srt_uri_leading_at_sign_parses) {
  char host[64];
  unsigned port;
  ck_assert_int_eq(route_parse_srt_uri("srt://@192.0.2.1:9000", host, sizeof host, &port), 0);
  ck_assert_str_eq(host, "192.0.2.1");
  ck_assert_uint_eq(port, 9000u);
}
END_TEST

START_TEST(srt_uri_missing_port_rejected) {
  char host[64];
  unsigned port;
  ck_assert_int_ne(route_parse_srt_uri("srt://source.example.com", host, sizeof host, &port), 0);
}
END_TEST

START_TEST(srt_uri_wrong_scheme_rejected) {
  char host[64];
  unsigned port;
  ck_assert_int_ne(route_parse_srt_uri("rtp://192.0.2.1:9000", host, sizeof host, &port), 0);
}
END_TEST

START_TEST(rist_bare_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/rist/ts", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_RIST);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
}
END_TEST

START_TEST(stdin_bare_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/stdin/hls", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_STDIN);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_HLS);
}
END_TEST

START_TEST(rist_with_address_rejected) {
  route_t r;
  ck_assert_int_ne(route_parse("/rist/192.0.2.1:9000/ts", &r), 0);
}
END_TEST

START_TEST(list_item_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/list/1/item/3/dash", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_LIST_ITEM);
  ck_assert_uint_eq(r.list_num, 1u);
  ck_assert_uint_eq(r.item_num, 3u);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_DASH);
}
END_TEST

START_TEST(list_name_route_parses_and_decodes_percent_escapes) {
  route_t r;
  ck_assert_int_eq(route_parse("/list/2/name/BBC%20One/ts", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_LIST_NAME);
  ck_assert_uint_eq(r.list_num, 2u);
  ck_assert_str_eq(r.item_name, "BBC One");
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
}
END_TEST

START_TEST(invalid_format_suffix_rejected) {
  route_t r;
  ck_assert_int_ne(route_parse("/rtp/239.0.0.1:8000/bogus", &r), 0);
}
END_TEST

START_TEST(missing_leading_slash_rejected) {
  route_t r;
  ck_assert_int_ne(route_parse("rtp/239.0.0.1:8000/ts", &r), 0);
}
END_TEST

START_TEST(unknown_prefix_rejected) {
  route_t r;
  ck_assert_int_ne(route_parse("/rtsp/239.0.0.1:8000/ts", &r), 0);
}
END_TEST

START_TEST(list_index_zero_rejected) {
  route_t r;
  ck_assert_int_ne(route_parse("/list/0/item/1/ts", &r), 0);
}
END_TEST

START_TEST(malformed_percent_escape_rejected) {
  route_t r;
  ck_assert_int_ne(route_parse("/list/1/name/bad%2/ts", &r), 0);
}
END_TEST

START_TEST(direct_route_without_format_defaults_to_ts) {
  route_t r;
  ck_assert_int_eq(route_parse("/udp/239.0.0.1:8000", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_UDP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
  ck_assert_str_eq(r.addr, "239.0.0.1");
  ck_assert_uint_eq(r.port, 8000u);
}
END_TEST

START_TEST(direct_route_with_trailing_slash_defaults_to_ts) {
  route_t r;
  ck_assert_int_eq(route_parse("/rtp/239.0.0.1:8000/", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_RTP);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
}
END_TEST

START_TEST(srt_route_without_format_defaults_to_ts) {
  route_t r;
  ck_assert_int_eq(route_parse("/srt/192.0.2.1:9000", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_SRT);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
}
END_TEST

START_TEST(rist_bare_without_format_defaults_to_ts) {
  route_t r;
  ck_assert_int_eq(route_parse("/rist", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_RIST);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
}
END_TEST

START_TEST(stdin_bare_without_format_defaults_to_ts) {
  route_t r;
  ck_assert_int_eq(route_parse("/stdin", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_STDIN);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
}
END_TEST

START_TEST(list_item_without_format_defaults_to_ts) {
  route_t r;
  ck_assert_int_eq(route_parse("/list/1/item/3", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_LIST_ITEM);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
  ck_assert_uint_eq(r.list_num, 1u);
  ck_assert_uint_eq(r.item_num, 3u);
}
END_TEST

START_TEST(list_name_without_format_defaults_to_ts) {
  route_t r;
  ck_assert_int_eq(route_parse("/list/2/name/BBC%20One", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_LIST_NAME);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
  ck_assert_str_eq(r.item_name, "BBC One");
}
END_TEST

START_TEST(named_bare_route_without_format_defaults_to_ts) {
  route_t r;
  ck_assert_int_eq(route_parse("/mystdin", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_NAMED_BARE);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
  ck_assert_str_eq(r.src_name, "mystdin");
}
END_TEST

START_TEST(named_bare_route_with_format_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/myrist/hls", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_NAMED_BARE);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_HLS);
  ck_assert_str_eq(r.src_name, "myrist");
}
END_TEST

START_TEST(named_bare_route_decodes_percent_escapes_in_name) {
  route_t r;
  ck_assert_int_eq(route_parse("/A1%20HD/item/1/ts", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_LIST_ITEM);
  ck_assert_str_eq(r.src_name, "A1 HD");
  ck_assert_uint_eq(r.item_num, 1u);
}
END_TEST

START_TEST(named_list_item_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/mylist/item/3/dash", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_LIST_ITEM);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_DASH);
  ck_assert_str_eq(r.src_name, "mylist");
  ck_assert_uint_eq(r.item_num, 3u);
}
END_TEST

START_TEST(named_list_item_route_without_format_defaults_to_ts) {
  route_t r;
  ck_assert_int_eq(route_parse("/mylist/item/3", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_LIST_ITEM);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_TS);
}
END_TEST

START_TEST(named_list_name_route_parses) {
  route_t r;
  ck_assert_int_eq(route_parse("/mylist/name/BBC%20One/llhls", &r), 0);
  ck_assert_int_eq(r.kind, ROUTE_LIST_NAME);
  ck_assert_int_eq(r.fmt, ROUTE_FMT_LLHLS);
  ck_assert_str_eq(r.src_name, "mylist");
  ck_assert_str_eq(r.item_name, "BBC One");
}
END_TEST

START_TEST(named_route_reserved_word_as_name_rejected) {
  route_t r;
  ck_assert_int_ne(route_parse("/list/item/3/ts", &r), 0);
  ck_assert_int_ne(route_parse("/metrics/ts", &r), 0);
  ck_assert_int_ne(route_parse("/api", &r), 0);
  ck_assert_int_ne(route_parse("/dlna", &r), 0);
}
END_TEST

START_TEST(named_route_leading_dot_rejected) {
  route_t r;
  ck_assert_int_ne(route_parse("/.hidden/ts", &r), 0);
  ck_assert_int_ne(route_parse("/.hidden", &r), 0);
}
END_TEST

START_TEST(route_name_valid_enforces_rules) {
  ck_assert_int_eq(route_name_valid("mylist"), 1);
  ck_assert_int_eq(route_name_valid(""), 0);
  ck_assert_int_eq(route_name_valid("foo/bar"), 0);
  ck_assert_int_eq(route_name_valid(".hidden"), 0);
  ck_assert_int_eq(route_name_valid("list"), 0);
  ck_assert_int_eq(route_name_valid("api"), 0);
  ck_assert_int_eq(route_name_valid("dlna"), 0);
}
END_TEST

static Suite *route_suite(void) {
  Suite *s = suite_create("dipixy_route");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rtp_ts_direct_route_parses);
  tcase_add_test(tc, udp_spts_direct_route_parses);
  tcase_add_test(tc, udp_rawaudio_direct_route_parses);
  tcase_add_test(tc, named_list_name_spts_route_parses);
  tcase_add_test(tc, udp_hls_direct_route_parses);
  tcase_add_test(tc, hls_segment_filename_route_parses);
  tcase_add_test(tc, hls_segment_filename_route_parses_for_list);
  tcase_add_test(tc, malformed_segment_filename_rejected);
  tcase_add_test(tc, udp_hls_fmp4_direct_route_parses);
  tcase_add_test(tc, udp_mp4_direct_route_parses);
  tcase_add_test(tc, hls_fmp4_init_filename_route_parses);
  tcase_add_test(tc, hls_fmp4_segment_filename_route_parses);
  tcase_add_test(tc, malformed_fmp4_segment_filename_rejected);
  tcase_add_test(tc, udp_llhls_direct_route_parses);
  tcase_add_test(tc, llhls_part_filename_route_parses);
  tcase_add_test(tc, malformed_llhls_part_filename_rejected);
  tcase_add_test(tc, udp_dash_direct_route_parses);
  tcase_add_test(tc, udp_lldash_direct_route_parses);
  tcase_add_test(tc, dash_seg_filename_route_parses);
  tcase_add_test(tc, malformed_dash_seg_filename_rejected);
  tcase_add_test(tc, rtp_ipv6_bracket_address_parses);
  tcase_add_test(tc, srt_ts_direct_route_parses);
  tcase_add_test(tc, srt_ipv6_bracket_address_parses);
  tcase_add_test(tc, srt_uri_numeric_ipv4_parses);
  tcase_add_test(tc, srt_uri_bracketed_ipv6_parses);
  tcase_add_test(tc, srt_uri_hostname_parses);
  tcase_add_test(tc, srt_uri_leading_at_sign_parses);
  tcase_add_test(tc, srt_uri_missing_port_rejected);
  tcase_add_test(tc, srt_uri_wrong_scheme_rejected);
  tcase_add_test(tc, rist_bare_route_parses);
  tcase_add_test(tc, stdin_bare_route_parses);
  tcase_add_test(tc, rist_with_address_rejected);
  tcase_add_test(tc, list_item_route_parses);
  tcase_add_test(tc, list_name_route_parses_and_decodes_percent_escapes);
  tcase_add_test(tc, invalid_format_suffix_rejected);
  tcase_add_test(tc, missing_leading_slash_rejected);
  tcase_add_test(tc, unknown_prefix_rejected);
  tcase_add_test(tc, list_index_zero_rejected);
  tcase_add_test(tc, malformed_percent_escape_rejected);
  tcase_add_test(tc, direct_route_without_format_defaults_to_ts);
  tcase_add_test(tc, direct_route_with_trailing_slash_defaults_to_ts);
  tcase_add_test(tc, srt_route_without_format_defaults_to_ts);
  tcase_add_test(tc, rist_bare_without_format_defaults_to_ts);
  tcase_add_test(tc, stdin_bare_without_format_defaults_to_ts);
  tcase_add_test(tc, list_item_without_format_defaults_to_ts);
  tcase_add_test(tc, list_name_without_format_defaults_to_ts);
  tcase_add_test(tc, named_bare_route_without_format_defaults_to_ts);
  tcase_add_test(tc, named_bare_route_with_format_parses);
  tcase_add_test(tc, named_bare_route_decodes_percent_escapes_in_name);
  tcase_add_test(tc, named_list_item_route_parses);
  tcase_add_test(tc, named_list_item_route_without_format_defaults_to_ts);
  tcase_add_test(tc, named_list_name_route_parses);
  tcase_add_test(tc, named_route_reserved_word_as_name_rejected);
  tcase_add_test(tc, named_route_leading_dot_rejected);
  tcase_add_test(tc, route_name_valid_enforces_rules);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(route_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
