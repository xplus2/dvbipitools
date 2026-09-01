/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/args.h"
#include "dipixy/dlna/dlna.h"
#include "dipixy/ts/channels/channels.h"

static void write_temp_file(char *path, const char *ext, const char *content) {
  char tmpl[64];
  int fd;
  FILE *f;
  snprintf(tmpl, sizeof tmpl, "/tmp/dvbipitools_test_dlna_XXXXXX.%s", ext);
  strcpy(path, tmpl);
  fd = mkstemps(path, (int)strlen(ext) + 1);
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  ck_assert_ptr_nonnull(f);
  fputs(content, f);
  fclose(f);
}

/* cfg/src must outlive returned channels_t: build_didl() looks source back up via cfg->sources */
static channels_t *build_single_list(config_t *cfg, source_def_t *src, source_kind_t kind, const char *path) {
  memset(src, 0, sizeof *src);
  src->kind = kind;
  src->value = path;
  src->ordinal = 1;
  cfg->sources = src;
  cfg->n_sources = 1;
  return channels_build(cfg);
}

static int browse(const config_t *cfg, const channels_t *ch, const char *objid, const char *flag, char **out, size_t *out_len) {
  char body[256];
  snprintf(body, sizeof body,
           "<ObjectID>%s</ObjectID><BrowseFlag>%s</BrowseFlag>"
           "<StartingIndex>0</StartingIndex><RequestedCount>0</RequestedCount>",
           objid, flag);
  return dlna_handle_control(cfg, ch, "cd", "Browse", body, strlen(body), out, out_len);
}

START_TEST(keep_multicast_off_uses_http_proxy_for_rtp_item) {
  config_t cfg;
  source_def_t src;
  channels_t *ch;
  char path[160], *out;
  size_t out_len;

  memset(&cfg, 0, sizeof cfg);
  write_temp_file(path, "m3u", "#EXTINF:-1,RTP Channel\nrtp://@239.1.1.1:5000\n");
  ch = build_single_list(&cfg, &src, SRC_M3U, path);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  strcpy(cfg.dlna_host, "dvb.example:9080");
  cfg.dlna_keep_multicast = 0;

  ck_assert_int_eq(browse(&cfg, ch, "L1I1", "BrowseMetadata", &out, &out_len), 200);
  ck_assert_ptr_null(strstr(out, "dvb-igmp"));
  ck_assert_ptr_nonnull(strstr(out, "http-get"));
  ck_assert_ptr_nonnull(strstr(out, "http://dvb.example:9080/list/1/item/1/spts"));

  channels_free(ch);
}
END_TEST

START_TEST(keep_multicast_rewrites_rtp_item_to_dvb_igmp) {
  config_t cfg;
  source_def_t src;
  channels_t *ch;
  char path[160], *out;
  size_t out_len;

  memset(&cfg, 0, sizeof cfg);
  write_temp_file(path, "m3u", "#EXTINF:-1,RTP Channel\nrtp://@239.1.1.1:5000\n");
  ch = build_single_list(&cfg, &src, SRC_M3U, path);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  strcpy(cfg.dlna_host, "dvb.example:9080");
  cfg.dlna_keep_multicast = 1;

  ck_assert_int_eq(browse(&cfg, ch, "L1I1", "BrowseMetadata", &out, &out_len), 200);
  ck_assert_ptr_nonnull(strstr(out, "protocolInfo=&quot;dvb-igmp:*:33:*&quot;"));
  ck_assert_ptr_nonnull(strstr(out, "rtp://239.1.1.1:5000"));
  ck_assert_ptr_null(strstr(out, "http-get"));

  channels_free(ch);
}
END_TEST

START_TEST(keep_multicast_rewrites_udp_item_to_dvb_igmp) {
  config_t cfg;
  source_def_t src;
  channels_t *ch;
  char path[160], *out;
  size_t out_len;

  memset(&cfg, 0, sizeof cfg);
  write_temp_file(path, "m3u", "#EXTINF:-1,UDP Channel\nudp://@239.1.1.2:6000\n");
  ch = build_single_list(&cfg, &src, SRC_M3U, path);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  strcpy(cfg.dlna_host, "dvb.example:9080");
  cfg.dlna_keep_multicast = 1;

  ck_assert_int_eq(browse(&cfg, ch, "L1I1", "BrowseMetadata", &out, &out_len), 200);
  ck_assert_ptr_nonnull(strstr(out, "protocolInfo=&quot;dvb-igmp:*:33:*&quot;"));
  ck_assert_ptr_nonnull(strstr(out, "udp://239.1.1.2:6000"));

  channels_free(ch);
}
END_TEST

START_TEST(keep_multicast_rewrites_ipv6_rtp_item_to_dvb_mld) {
  config_t cfg;
  source_def_t src;
  channels_t *ch;
  char path[160], *out;
  size_t out_len;

  memset(&cfg, 0, sizeof cfg);
  write_temp_file(path, "m3u", "#EXTINF:-1,RTP6 Channel\nrtp://@[ff0e::1]:6000\n");
  ch = build_single_list(&cfg, &src, SRC_M3U, path);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  strcpy(cfg.dlna_host, "dvb.example:9080");
  cfg.dlna_keep_multicast = 1;

  ck_assert_int_eq(browse(&cfg, ch, "L1I1", "BrowseMetadata", &out, &out_len), 200);
  ck_assert_ptr_nonnull(strstr(out, "protocolInfo=&quot;dvb-mld:*:33:*&quot;"));
  ck_assert_ptr_nonnull(strstr(out, "rtp://[ff0e::1]:6000"));

  channels_free(ch);
}
END_TEST

START_TEST(keep_multicast_leaves_unicast_srt_item_on_http_proxy) {
  config_t cfg;
  source_def_t src;
  channels_t *ch;
  char path[160], *out;
  size_t out_len;

  memset(&cfg, 0, sizeof cfg);
  write_temp_file(path, "m3u", "#EXTINF:-1,SRT Channel\nsrt://192.0.2.1:9000\n");
  ch = build_single_list(&cfg, &src, SRC_M3U, path);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  strcpy(cfg.dlna_host, "dvb.example:9080");
  cfg.dlna_keep_multicast = 1;

  ck_assert_int_eq(browse(&cfg, ch, "L1I1", "BrowseMetadata", &out, &out_len), 200);
  ck_assert_ptr_null(strstr(out, "dvb-igmp"));
  ck_assert_ptr_null(strstr(out, "dvb-mld"));
  ck_assert_ptr_nonnull(strstr(out, "http://dvb.example:9080/list/1/item/1/spts"));

  channels_free(ch);
}
END_TEST

START_TEST(keep_multicast_direct_children_mixes_mcast_and_unicast_res) {
  config_t cfg;
  source_def_t src;
  channels_t *ch;
  char path[160], *out;
  size_t out_len;

  memset(&cfg, 0, sizeof cfg);
  write_temp_file(path, "m3u",
                   "#EXTINF:-1,RTP Channel\nrtp://@239.1.1.1:5000\n"
                   "#EXTINF:-1,SRT Channel\nsrt://192.0.2.1:9000\n");
  ch = build_single_list(&cfg, &src, SRC_M3U, path);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  strcpy(cfg.dlna_host, "dvb.example:9080");
  cfg.dlna_keep_multicast = 1;

  ck_assert_int_eq(browse(&cfg, ch, "L1", "BrowseDirectChildren", &out, &out_len), 200);
  ck_assert_ptr_nonnull(strstr(out, "TotalMatches>2<"));
  ck_assert_ptr_nonnull(strstr(out, "protocolInfo=&quot;dvb-igmp:*:33:*&quot;"));
  ck_assert_ptr_nonnull(strstr(out, "rtp://239.1.1.1:5000"));
  ck_assert_ptr_nonnull(strstr(out, "http://dvb.example:9080/list/1/item/2/spts"));

  channels_free(ch);
}
END_TEST

START_TEST(get_protocol_info_lists_video) {
  config_t cfg;
  char *out;
  size_t out_len;

  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(dlna_handle_control(&cfg, NULL, "cm", "GetProtocolInfo", "", 0, &out, &out_len), 200);
  ck_assert_ptr_nonnull(strstr(out, "video/mpeg"));
  ck_assert_ptr_null(strstr(out, "dvb-igmp"));
}
END_TEST

START_TEST(get_protocol_info_lists_multicast_when_keep_multicast_set) {
  config_t cfg;
  char *out;
  size_t out_len;

  memset(&cfg, 0, sizeof cfg);
  cfg.dlna_keep_multicast = 1;
  ck_assert_int_eq(dlna_handle_control(&cfg, NULL, "cm", "GetProtocolInfo", "", 0, &out, &out_len), 200);
  ck_assert_ptr_nonnull(strstr(out, "dvb-igmp"));
  ck_assert_ptr_nonnull(strstr(out, "dvb-mld"));
}
END_TEST

START_TEST(device_desc_omits_x_dvbdoc) {
  config_t cfg;
  char *out;
  size_t out_len;
  memset(&cfg, 0, sizeof cfg);
  strcpy(cfg.dlna_host, "dvb.example:9080");
  ck_assert_int_eq(dlna_device_desc_xml(&cfg, &out, &out_len), 0);
  ck_assert_ptr_null(strstr(out, "X_DVBDOC"));
}
END_TEST

static Suite *dlna_suite(void) {
  Suite *s = suite_create("dipixy_dlna");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, keep_multicast_off_uses_http_proxy_for_rtp_item);
  tcase_add_test(tc, keep_multicast_rewrites_rtp_item_to_dvb_igmp);
  tcase_add_test(tc, keep_multicast_rewrites_udp_item_to_dvb_igmp);
  tcase_add_test(tc, keep_multicast_rewrites_ipv6_rtp_item_to_dvb_mld);
  tcase_add_test(tc, keep_multicast_leaves_unicast_srt_item_on_http_proxy);
  tcase_add_test(tc, keep_multicast_direct_children_mixes_mcast_and_unicast_res);
  tcase_add_test(tc, device_desc_omits_x_dvbdoc);
  tcase_add_test(tc, get_protocol_info_lists_video);
  tcase_add_test(tc, get_protocol_info_lists_multicast_when_keep_multicast_set);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(dlna_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
