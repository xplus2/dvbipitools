/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/tva/xmltv.h"
#include "lib/sys/ioutil.h"

START_TEST(xmltv_write_read_round_trips) {
  bcg_doc_t doc, doc2;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  FILE *f;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "channel1");
  bcg_channel_add_name(c, "Channel One");
  bcg_channel_add_name(c, "Channel 1 HD");

  pr = bcg_add_programme(&doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "channel1");
  bufcpy(pr->start, sizeof pr->start, "2020-12-15T12:30:45Z");
  bufcpy(pr->stop, sizeof pr->stop, "2020-12-15T13:00:00Z");
  bcg_set_text(&pr->title, "News & <Weather>");
  bcg_set_text(&pr->desc, "Today's \"top\" stories");
  bcg_set_text(&pr->category, "News");

  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  xmltv_write(f, &doc, "dvbipitools-test");
  rewind(f);

  bcg_doc_init(&doc2);
  ck_assert_int_eq(xmltv_read(f, &doc2), 0);
  fclose(f);

  ck_assert_int_eq(doc2.channel_count, 1);
  ck_assert_str_eq(doc2.channels[0].id, "channel1");
  ck_assert_int_eq(doc2.channels[0].name_count, 2);
  ck_assert_str_eq(doc2.channels[0].names[0], "Channel One");
  ck_assert_str_eq(doc2.channels[0].names[1], "Channel 1 HD");

  ck_assert_int_eq(doc2.programme_count, 1);
  ck_assert_str_eq(doc2.programmes[0].channel_id, "channel1");
  ck_assert_str_eq(doc2.programmes[0].start, "2020-12-15T12:30:45Z");
  ck_assert_str_eq(doc2.programmes[0].stop, "2020-12-15T13:00:00Z");
  ck_assert_str_eq(doc2.programmes[0].title, "News & <Weather>");
  ck_assert_str_eq(doc2.programmes[0].desc, "Today's \"top\" stories");
  ck_assert_str_eq(doc2.programmes[0].category, "News");

  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
}
END_TEST

START_TEST(xmltv_read_converts_declared_latin1) {
  static const char doc[] =
      "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><tv><channel id=\"c1\"><display-name>Caf\xE9</display-name></channel></tv>";
  bcg_doc_t d;
  FILE *f = tmpfile();
  ck_assert_ptr_nonnull(f);
  fputs(doc, f);
  rewind(f);
  bcg_doc_init(&d);
  ck_assert_int_eq(xmltv_read(f, &d), 0);
  fclose(f);
  ck_assert_int_eq(d.channel_count, 1);
  ck_assert_str_eq(d.channels[0].names[0], "Caf\xC3\xA9");
  bcg_doc_free(&d);
}
END_TEST

START_TEST(xmltv_read_rejects_unknown_encoding) {
  static const char doc[] = "<?xml version=\"1.0\" encoding=\"no-such-enc\"?><tv/>";
  bcg_doc_t d;
  FILE *f = tmpfile();
  ck_assert_ptr_nonnull(f);
  fputs(doc, f);
  rewind(f);
  bcg_doc_init(&d);
  ck_assert_int_eq(xmltv_read(f, &d), -1);
  fclose(f);
  bcg_doc_free(&d);
}
END_TEST

START_TEST(xmltv_read_keeps_description_past_1k) {
  static char doc[4096];
  bcg_doc_t d;
  FILE *f;
  size_t n = (size_t)snprintf(doc, sizeof doc, "<tv><programme start=\"20201215120000 +0000\" channel=\"c1\"><title>T</title><desc>");
  memset(doc + n, 'd', 3000);
  n += 3000;
  snprintf(doc + n, sizeof doc - n, "</desc></programme></tv>");
  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  fputs(doc, f);
  rewind(f);
  bcg_doc_init(&d);
  ck_assert_int_eq(xmltv_read(f, &d), 0);
  fclose(f);
  ck_assert_int_eq(d.programme_count, 1);
  ck_assert_uint_eq(strlen(d.programmes[0].desc), 3000u);
  ck_assert_ptr_null(d.programmes[0].category);
  bcg_doc_free(&d);
}
END_TEST

START_TEST(xmltv_write_falls_back_to_id_when_no_names) {
  bcg_doc_t doc, doc2;
  bcg_channel_t *c;
  FILE *f;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "channel1");

  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  xmltv_write(f, &doc, "dvbipitools-test");
  rewind(f);

  bcg_doc_init(&doc2);
  ck_assert_int_eq(xmltv_read(f, &doc2), 0);
  fclose(f);

  ck_assert_int_eq(doc2.channels[0].name_count, 1);
  ck_assert_str_eq(doc2.channels[0].names[0], "channel1");

  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
}
END_TEST

START_TEST(xmltv_write_untitled_fallback_for_empty_title) {
  bcg_doc_t doc, doc2;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  FILE *f;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "channel1");
  pr = bcg_add_programme(&doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "channel1");
  bufcpy(pr->start, sizeof pr->start, "2020-12-15T12:30:45Z");

  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  xmltv_write(f, &doc, "dvbipitools-test");
  rewind(f);

  bcg_doc_init(&doc2);
  xmltv_read(f, &doc2);
  fclose(f);

  ck_assert_str_eq(doc2.programmes[0].title, "(untitled)");

  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
}
END_TEST

START_TEST(xmltv_read_accepts_single_quotes_cdata_short_times_and_zone_names) {
  static const char xml[] =
      "<?xml version='1.0'?><tv>\n"
      "<channel id = 'c1'><display-name><![CDATA[Chan & One]]></display-name></channel>\n"
      "<programme start='202012151230 +0200' stop = '202012151300 CET' channel='c1'>\n"
      "<title lang='en'><![CDATA[Tom & Jerry]]></title></programme>\n"
      "</tv>\n";
  bcg_doc_t doc;
  FILE *f = tmpfile();

  ck_assert_ptr_nonnull(f);
  fputs(xml, f);
  rewind(f);
  bcg_doc_init(&doc);
  ck_assert_int_eq(xmltv_read(f, &doc), 0);
  fclose(f);

  ck_assert_int_eq(doc.channel_count, 1);
  ck_assert_str_eq(doc.channels[0].id, "c1");
  ck_assert_str_eq(doc.channels[0].names[0], "Chan & One");
  ck_assert_int_eq(doc.programme_count, 1);
  ck_assert_str_eq(doc.programmes[0].channel_id, "c1");
  ck_assert_str_eq(doc.programmes[0].start, "2020-12-15T12:30:00+02:00");
  ck_assert_str_eq(doc.programmes[0].stop, "2020-12-15T13:00:00+01:00");
  ck_assert_str_eq(doc.programmes[0].title, "Tom & Jerry");
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(xmltv_read_keeps_programmes_before_an_unclosed_one) {
  static const char xml[] =
      "<tv>\n"
      "<programme start='202012151230 +0200' channel='c1'><title>A</title></programme>\n"
      "<programme start='202012151300 +0200' channel='c1'><title>B</title>\n"
      "<programme start='202012151330 +0200' channel='c1'><title>C</title></programme>\n"
      "</tv>\n";
  bcg_doc_t doc;
  FILE *f = tmpfile();

  ck_assert_ptr_nonnull(f);
  fputs(xml, f);
  rewind(f);
  bcg_doc_init(&doc);
  ck_assert_int_eq(xmltv_read(f, &doc), 0);
  fclose(f);
  ck_assert_int_eq(doc.programme_count, 1);
  ck_assert_str_eq(doc.programmes[0].title, "A");
  bcg_doc_free(&doc);
}
END_TEST

static int read_text(const char *text, bcg_doc_t *doc) {
  FILE *f = tmpfile();
  int rc;
  ck_assert_ptr_nonnull(f);
  fputs(text, f);
  rewind(f);
  bcg_doc_init(doc);
  rc = xmltv_read(f, doc);
  fclose(f);
  return rc;
}

START_TEST(xmltv_read_skips_or_clears_bad_times_and_attributes) {
  static const char text[] =
    "<?xml version=\"1.0\"?><tv>"
    "<channel id=\"c1\"><display-name>One</display-name></channel>"
    "<programme start=\"20200101120000 +0000\" stop=\"garbage\" channel=\"c1\"><title>BadStop</title></programme>"
    "<programme start=\"20200101130000 +0000\" stop=\"20200101140000 XYZ\" channel=\"c1\"><title>ZoneStop</title></programme>"
    "<programme start=\"20200101150000 XYZ\" channel=\"c1\"><title>ZoneStart</title></programme>"
    "<programme start=\"junk\" channel=\"c1\"><title>BadStart</title></programme>"
    "<programme channel=\"c1\"><title>NoStart</title></programme>"
    "<programme start=\"20200101160000 +0000\"><title>NoChannel</title></programme>"
    "</tv>";
  bcg_doc_t doc;
  ck_assert_int_eq(read_text(text, &doc), 0);
  ck_assert_int_eq(doc.programme_count, 3);
  ck_assert_str_eq(doc.programmes[0].stop, "");
  ck_assert_str_ne(doc.programmes[1].stop, "");
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(xmltv_write_skips_programmes_with_bad_start) {
  bcg_doc_t doc;
  bcg_programme_t *pr;
  char *buf = NULL;
  size_t size = 0;
  FILE *f;
  bcg_doc_init(&doc);
  pr = bcg_add_programme(&doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "c");
  bufcpy(pr->start, sizeof pr->start, "not-a-time");
  pr = bcg_add_programme(&doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "c");
  bufcpy(pr->start, sizeof pr->start, "2020-01-01T10:00:00Z");
  bufcpy(pr->stop, sizeof pr->stop, "bad-stop");
  bufcpy(doc.publisher, sizeof doc.publisher, "example.com/My%20Guide");
  f = open_memstream(&buf, &size);
  ck_assert_ptr_nonnull(f);
  xmltv_write(f, &doc, "gen");
  fclose(f);
  ck_assert_ptr_nonnull(strstr(buf, "source-info-name=\"My Guide\""));
  ck_assert_ptr_null(strstr(buf, "not-a-time"));
  ck_assert_ptr_null(strstr(buf, "stop="));
  free(buf);
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(xmltv_read_survives_allocation_failures) {
  static const char text[] =
    "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><tv>"
    "<channel id=\"c1\"><display-name>One</display-name><display-name>Zwei</display-name></channel>"
    "<programme start=\"20200101120000 +0000\" stop=\"20200101130000 +0000\" channel=\"c1\"><title>T</title><desc>D</desc><category>C</category></programme>"
    "</tv>";
  for (long n = 0;; n++) {
    bcg_doc_t doc;
    FILE *f = tmpfile();
    int hit;
    ck_assert_ptr_nonnull(f);
    fputs(text, f);
    rewind(f);
    bcg_doc_init(&doc);
    alloc_fail_at(n);
    (void)xmltv_read(f, &doc);
    hit = alloc_fail_end();
    fclose(f);
    bcg_doc_free(&doc);
    if (!hit) break;
  }
}
END_TEST

static Suite *xmltv_suite(void) {
  Suite *s = suite_create("xmltv");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, xmltv_write_read_round_trips);
  tcase_add_test(tc, xmltv_write_falls_back_to_id_when_no_names);
  tcase_add_test(tc, xmltv_read_converts_declared_latin1);
  tcase_add_test(tc, xmltv_read_rejects_unknown_encoding);
  tcase_add_test(tc, xmltv_write_untitled_fallback_for_empty_title);
  tcase_add_test(tc, xmltv_read_keeps_description_past_1k);
  tcase_add_test(tc, xmltv_read_accepts_single_quotes_cdata_short_times_and_zone_names);
  tcase_add_test(tc, xmltv_read_keeps_programmes_before_an_unclosed_one);
  tcase_add_test(tc, xmltv_read_skips_or_clears_bad_times_and_attributes);
  tcase_add_test(tc, xmltv_write_skips_programmes_with_bad_start);
  tcase_add_test(tc, xmltv_read_survives_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(xmltv_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
