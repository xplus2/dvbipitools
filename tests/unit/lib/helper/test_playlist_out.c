/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/playlist_out.h"

static char *capture(playlist_out_fmt_t fmt, int items) {
  char *buf = NULL;
  size_t sz = 0;
  FILE *f = open_memstream(&buf, &sz);
  ck_assert_ptr_nonnull(f);
  playlist_out_init(f, fmt, "dipitool --x", "Services ");
  if (items) {
    playlist_out_m3u_item(f, "Na\tme\x7f,1", "rtp://@239.1.1.1:5000\r", "http://x/\"logo\".png", 1, 2, 3);
    playlist_out_m3u_item(f, "Plain", "udp://@239.1.1.2:5000", NULL, 4, 5, 6);
    playlist_out_m3u_item(f, "Empty icon", "udp://@239.1.1.3:5000", "", 7, 8, 9);
    playlist_out_csv_item(f, "A,B", "rtp://@239.1.1.1:5000", 1, 2, 3);
    playlist_out_csv_item(f, "Q\"t\n", "rtp://@239.1.1.2:5000", 4, 5, 6);
    playlist_out_csv_item(f, "#Hash", "rtp://@239.1.1.3:5000", 7, 8, 9);
    playlist_out_csv_item(f, "Plain\x01", "rtp://@239.1.1.4:5000", 1, 1, 1);
    playlist_out_xspf_item(f, "T<>&", "rtp://@239.1.1.1:5000?a=b&c=d", "http://x/i.png", 1, 2, 3);
    playlist_out_xspf_item(f, "NoIcon", "udp://@239.1.1.2:5000", NULL, 4, 5, 6);
    playlist_out_xspf_item(f, "EmptyIcon", "udp://@239.1.1.3:5000", "", 7, 8, 9);
  }
  playlist_out_close(f, fmt);
  fclose(f);
  return buf;
}

START_TEST(playlist_headers_trailers_and_items) {
  char *m3u = capture(PLAYLIST_OUT_M3U, 1);
  char *xspf = capture(PLAYLIST_OUT_XSPF, 1);
  char *csv = capture(PLAYLIST_OUT_CSV, 0);
  char *xml = capture(PLAYLIST_OUT_XML, 0);
  char *nul = capture(PLAYLIST_OUT_NULL, 0);
  char ts[24];
  playlist_out_stamp(ts, sizeof ts);
  ck_assert_uint_eq(strlen(ts), 16u);
  ck_assert_ptr_eq(strstr(m3u, "#EXTM3U\n# "), m3u);
  ck_assert_ptr_nonnull(strstr(m3u, "# dipitool --x\n"));
  ck_assert_ptr_nonnull(strstr(m3u, "tvg-logo=\"http://x/logo.png\" tsid=\"1\" onid=\"2\" sid=\"3\",Name,1\nrtp://@239.1.1.1:5000\n"));
  ck_assert_ptr_nonnull(strstr(m3u, "#EXTINF:-1 tsid=\"4\" onid=\"5\" sid=\"6\",Plain\n"));
  ck_assert_ptr_nonnull(strstr(m3u, "#EXTINF:-1 tsid=\"7\" onid=\"8\" sid=\"9\",Empty icon\n"));
  ck_assert_ptr_nonnull(strstr(m3u, "\"A,B\",rtp://@239.1.1.1:5000,1,2,3\n"));
  ck_assert_ptr_nonnull(strstr(m3u, "\"Q\"\"t\",rtp://@239.1.1.2:5000,4,5,6\n"));
  ck_assert_ptr_nonnull(strstr(m3u, "\"#Hash\",rtp://@239.1.1.3:5000,7,8,9\n"));
  ck_assert_ptr_nonnull(strstr(m3u, "\nPlain,rtp://@239.1.1.4:5000,1,1,1\n"));
  ck_assert_ptr_nonnull(strstr(xspf, "<title>T&lt;&gt;&amp;</title>"));
  ck_assert_ptr_nonnull(strstr(xspf, "<image>http://x/i.png</image>"));
  ck_assert_ptr_nonnull(strstr(xspf, "<title>NoIcon</title><extension"));
  ck_assert_ptr_nonnull(strstr(xspf, "<title>EmptyIcon</title><extension"));
  ck_assert_ptr_nonnull(strstr(xspf, "</playlist>\n"));
  ck_assert_str_eq(csv, "");
  ck_assert_str_eq(xml, "");
  ck_assert_str_eq(nul, "");
  free(m3u);
  free(xspf);
  free(csv);
  free(xml);
  free(nul);
  {
    char *x = capture(PLAYLIST_OUT_XSPF, 0);
    ck_assert_ptr_nonnull(strstr(x, "<title>Services "));
    free(x);
  }
}
END_TEST

static Suite *playlist_suite(void) {
  Suite *s = suite_create("playlist_out");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, playlist_headers_trailers_and_items);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(playlist_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
