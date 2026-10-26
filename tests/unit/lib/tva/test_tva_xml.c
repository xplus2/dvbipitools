/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/tva/tva_xml.h"
#include "lib/tva/tva_xml/tva_xml_int.h"
#include "lib/sys/ioutil.h"
#include "../../log_capture.h"

START_TEST(tva_xml_write_read_round_trips) {
  bcg_doc_t doc, doc2;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  FILE *f;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "bbc1");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  c->tsid = 1;
  c->onid = 2;
  c->sid = 101;
  bcg_channel_add_name(c, "BBC One");

  pr = bcg_add_programme(&doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "bbc1");
  bufcpy(pr->start, sizeof pr->start, "2024-03-15T12:30:45Z");
  bufcpy(pr->stop, sizeof pr->stop, "2024-03-15T13:00:00Z");
  bcg_set_text(&pr->title, "News & <Weather>");
  bcg_set_text(&pr->desc, "Today's \"top\" stories");
  bcg_set_text(&pr->category, "News");

  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  tva_xml_write(f, &doc);
  rewind(f);

  bcg_doc_init(&doc2);
  ck_assert_int_eq(tva_xml_read(f, &doc2), 0);
  fclose(f);

  ck_assert_int_eq(doc2.channel_count, 1);
  ck_assert_str_eq(doc2.channels[0].id, "bbc1");
  ck_assert_str_eq(doc2.channels[0].uri, "rtp://239.1.1.1:5000");
  ck_assert_uint_eq(doc2.channels[0].onid, 2u);
  ck_assert_uint_eq(doc2.channels[0].tsid, 1u);
  ck_assert_uint_eq(doc2.channels[0].sid, 101u);
  ck_assert_int_eq(doc2.channels[0].name_count, 1);
  ck_assert_str_eq(doc2.channels[0].names[0], "BBC One");

  ck_assert_int_eq(doc2.programme_count, 1);
  ck_assert_str_eq(doc2.programmes[0].channel_id, "bbc1");
  ck_assert_str_eq(doc2.programmes[0].start, "2024-03-15T12:30:45Z");
  ck_assert_str_eq(doc2.programmes[0].stop, "2024-03-15T13:00:00Z");
  ck_assert_str_eq(doc2.programmes[0].title, "News & <Weather>");
  ck_assert_str_eq(doc2.programmes[0].desc, "Today's \"top\" stories");
  ck_assert_str_eq(doc2.programmes[0].category, "News");

  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
}
END_TEST

START_TEST(tva_xml_write_dtt_is_hex_and_omitted_without_triplet) {
  bcg_doc_t doc;
  bcg_channel_t *c;
  char buf[4096];
  size_t n;
  FILE *f;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "a");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  c->onid = 0x22d4;
  c->tsid = 0x1f;
  c->sid = 255;
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "b");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.2:5000");

  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  tva_xml_write(f, &doc);
  rewind(f);
  n = fread(buf, 1, sizeof buf - 1, f);
  buf[n] = '\0';
  fclose(f);

  ck_assert_ptr_nonnull(strstr(buf, "dvb://22d4.1f.ff<"));
  ck_assert_ptr_null(strstr(buf, "dvb://0.0.0"));
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(tva_xml_write_drops_channels_without_uri) {
  bcg_doc_t doc, doc2;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  FILE *f;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc); /* no uri set */
  bufcpy(c->id, sizeof c->id, "noturi");
  pr = bcg_add_programme(&doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "noturi");
  bufcpy(pr->start, sizeof pr->start, "2024-03-15T12:30:45Z");
  bcg_set_text(&pr->title, "Should be dropped");

  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  tva_xml_write(f, &doc);
  rewind(f);

  bcg_doc_init(&doc2);
  ck_assert_int_eq(tva_xml_read(f, &doc2), 0);
  fclose(f);

  ck_assert_int_eq(doc2.channel_count, 0);
  ck_assert_int_eq(doc2.programme_count, 0);

  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
}
END_TEST

START_TEST(tva_build_crid_percent_encodes_and_compacts_time) {
  char out[256];
  tva_build_crid("bbc/one", "2024-03-15T12:30:45Z", out, sizeof out);
  ck_assert_str_eq(out, "crid://dipixmltv.invalid/bbc%2Fone/20240315123045");
}
END_TEST

START_TEST(tva_build_crid_dst_overlap_distinct) {
  char a[256];
  char b[256];
  char c[256];
  tva_build_crid("ch", "2026-10-25T02:30:00+02:00", a, sizeof a);
  tva_build_crid("ch", "2026-10-25T02:30:00+01:00", b, sizeof b);
  ck_assert_str_ne(a, b);
  ck_assert_str_eq(a, "crid://dipixmltv.invalid/ch/20261025003000");
  ck_assert_str_eq(b, "crid://dipixmltv.invalid/ch/20261025013000");
  tva_build_crid("ch", "2026-10-25T00:30:00Z", c, sizeof c);
  ck_assert_str_eq(a, c);
}
END_TEST

START_TEST(tva_build_crid_offset_crosses_day) {
  char out[256];
  tva_build_crid("ch", "2026-01-01T00:30:00+01:00", out, sizeof out);
  ck_assert_str_eq(out, "crid://dipixmltv.invalid/ch/20251231233000");
  tva_build_crid("ch", "2024-02-28T23:30:00-02:00", out, sizeof out);
  ck_assert_str_eq(out, "crid://dipixmltv.invalid/ch/20240229013000");
}
END_TEST

START_TEST(tva_programme_crid_cached_and_fallback) {
  bcg_programme_t pr;
  char buf[TVA_CRID_BUF];
  memset(&pr, 0, sizeof pr);
  strcpy(pr.channel_id, "ch");
  strcpy(pr.start, "2026-01-01T00:30:00Z");
  ck_assert_ptr_eq(tva_programme_crid(&pr, buf, sizeof buf), buf);
  ck_assert_str_eq(buf, "crid://dipixmltv.invalid/ch/20260101003000");
  ck_assert_int_eq(tva_programme_cache_crid(&pr), 0);
  ck_assert_ptr_eq(tva_programme_crid(&pr, buf, sizeof buf), pr.crid);
  ck_assert_str_eq(pr.crid, "crid://dipixmltv.invalid/ch/20260101003000");
  bcg_programme_free_text(&pr);
  ck_assert_ptr_eq(pr.crid, NULL);
}
END_TEST

typedef struct {
  const char *channel;
  const char *title;
  int expect_kept;
} prog_case_t;

static const char *const unsorted_channels[] = {"zeta", "mid", "alpha"};

static const prog_case_t multi_channel_programmes[] = {
    {"zeta", "Z first", 1},
    {"mid", "M first", 1},
    {"alpha", "A only", 1},
    {"zeta", "Z second", 1},
    {"ghost", "No such channel", 0},
    {"mid", "M second", 1},
};

START_TEST(tva_xml_write_groups_programmes_for_unsorted_channels) {
  bcg_doc_t doc, doc2;
  FILE *f;
  int kept = 0;

  bcg_doc_init(&doc);
  for (size_t i = 0; i < sizeof unsorted_channels / sizeof unsorted_channels[0]; i++) {
    bcg_channel_t *c = bcg_add_channel(&doc);

    bufcpy(c->id, sizeof c->id, unsorted_channels[i]);
    bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  }
  for (size_t i = 0; i < sizeof multi_channel_programmes / sizeof multi_channel_programmes[0]; i++) {
    bcg_programme_t *pr = bcg_add_programme(&doc);

    bufcpy(pr->channel_id, sizeof pr->channel_id, multi_channel_programmes[i].channel);
    snprintf(pr->start, sizeof pr->start, "2024-03-15T12:%02zu:00Z", i);
    bcg_set_text(&pr->title, multi_channel_programmes[i].title);
    kept += multi_channel_programmes[i].expect_kept;
  }
  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  tva_xml_write(f, &doc);
  rewind(f);
  bcg_doc_init(&doc2);
  ck_assert_int_eq(tva_xml_read(f, &doc2), 0);
  fclose(f);

  ck_assert_int_eq(doc2.channel_count, 3);
  for (int i = 0; i < 3; i++) ck_assert_str_eq(doc2.channels[i].id, unsorted_channels[i]);
  ck_assert_int_eq(doc2.programme_count, kept);
  for (size_t i = 0; i < sizeof multi_channel_programmes / sizeof multi_channel_programmes[0]; i++) {
    int found = 0;
    for (int j = 0; j < doc2.programme_count; j++)
      if (!strcmp(bcg_str(doc2.programmes[j].title), multi_channel_programmes[i].title) && !strcmp(doc2.programmes[j].channel_id, multi_channel_programmes[i].channel)) found = 1;
    ck_assert_msg(found == multi_channel_programmes[i].expect_kept, "%s: found %d", multi_channel_programmes[i].title, found);
  }
  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
}
END_TEST

static char *slurp_tmp(FILE *f) {
  long n;
  char *buf;

  fseek(f, 0, SEEK_END);
  n = ftell(f);
  rewind(f);
  buf = malloc((size_t)n + 1);
  ck_assert_ptr_nonnull(buf);
  ck_assert_uint_eq(fread(buf, 1, (size_t)n, f), (size_t)n);
  buf[n] = '\0';
  return buf;
}

START_TEST(tva_xml_write_omits_absent_optional_fields) {
  bcg_doc_t doc, doc2;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  FILE *f;
  char *xml;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "ch");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  pr = bcg_add_programme(&doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "ch");
  bufcpy(pr->start, sizeof pr->start, "2024-03-15T12:30:45Z");
  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  tva_xml_write(f, &doc);
  xml = slurp_tmp(f);
  ck_assert_ptr_null(strstr(xml, "<Title>"));
  ck_assert_ptr_null(strstr(xml, "<Synopsis>"));
  ck_assert_ptr_null(strstr(xml, "<Genre"));
  ck_assert_ptr_null(strstr(xml, "<PublishedEndTime>"));
  ck_assert_ptr_nonnull(strstr(xml, "<PublishedStartTime>2024-03-15T12:30:45Z</PublishedStartTime>"));
  free(xml);
  rewind(f);
  bcg_doc_init(&doc2);
  ck_assert_int_eq(tva_xml_read(f, &doc2), 0);
  fclose(f);
  ck_assert_int_eq(doc2.programme_count, 1);
  ck_assert_str_eq(doc2.programmes[0].stop, "");
  ck_assert_ptr_null(doc2.programmes[0].title);
  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
}
END_TEST

START_TEST(tva_xml_write_handles_empty_documents_and_channels_without_programmes) {
  bcg_doc_t doc, doc2;
  bcg_channel_t *c;
  FILE *f;
  char *xml;

  bcg_doc_init(&doc);
  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  tva_xml_write(f, &doc);
  rewind(f);
  bcg_doc_init(&doc2);
  ck_assert_int_eq(tva_xml_read(f, &doc2), 0);
  ck_assert_int_eq(doc2.channel_count, 0);
  ck_assert_int_eq(doc2.programme_count, 0);
  fclose(f);
  bcg_doc_free(&doc2);

  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "quiet");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.2:5000");
  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  tva_xml_write(f, &doc);
  xml = slurp_tmp(f);
  ck_assert_ptr_null(strstr(xml, "<Schedule "));
  ck_assert_ptr_nonnull(strstr(xml, "<ServiceInformation serviceId=\"quiet\">"));
  free(xml);
  fclose(f);
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(tva_xml_read_tolerates_incomplete_elements) {
  static const char xml[] =
    "<TVAMain><ProgramDescription>\n"
    "<ProgramInformationTable>\n"
    "<ProgramInformation foo=\"x\"><BasicDescription><Title>No id</Title></BasicDescription></ProgramInformation>\n"
    "<ProgramInformation programId=\"crid://x/a/1\"><BasicDescription><Title>Known</Title><Synopsis>Desc</Synopsis></BasicDescription></ProgramInformation>\n"
    "</ProgramInformationTable>\n"
    "<ProgramLocationTable>\n"
    "<Schedule serviceIDRef=\"ch1\">\n"
    "<ScheduleEvent><Program crid=\"crid://x/a/1\"/><PublishedStartTime>2024-01-01T10:00:00Z</PublishedStartTime></ScheduleEvent>\n"
    "<ScheduleEvent><Program crid=\"crid://x/unknown\"/><PublishedStartTime>2024-01-01T11:00:00Z</PublishedStartTime>"
    "<PublishedEndTime>2024-01-01T12:00:00Z</PublishedEndTime></ScheduleEvent>\n"
    "<ScheduleEvent><PublishedStartTime>2024-01-01T13:00:00Z</PublishedStartTime></ScheduleEvent>\n"
    "<ScheduleEvent><Program crid=\"crid://x/a/1\"/></ScheduleEvent>\n"
    "</Schedule>\n"
    "<Schedule foo=\"x\"><ScheduleEvent><Program crid=\"c\"/><PublishedStartTime>2024-01-01T14:00:00Z</PublishedStartTime></ScheduleEvent></Schedule>\n"
    "</ProgramLocationTable>\n"
    "<ServiceInformationTable>\n"
    "<ServiceInformation foo=\"x\"><Name>No id</Name></ServiceInformation>\n"
    "<ServiceInformation serviceId=\"ch1\"><Name>One</Name><Name>Uno</Name><ServiceURL name=\"IPTV\">rtp://239.1.1.1:5000</ServiceURL>"
    "<ServiceURL name=\"DTT\">dvb://bogus</ServiceURL></ServiceInformation>\n"
    "<ServiceInformation serviceId=\"ch2\"><ServiceURL name=\"DTT\">dvb://1.2.1f</ServiceURL></ServiceInformation>\n"
    "</ServiceInformationTable>\n"
    "</ProgramDescription></TVAMain>\n";
  bcg_doc_t doc;
  FILE *f = tmpfile();

  ck_assert_ptr_nonnull(f);
  ck_assert_uint_eq(fwrite(xml, 1, sizeof xml - 1, f), sizeof xml - 1);
  rewind(f);
  bcg_doc_init(&doc);
  ck_assert_int_eq(tva_xml_read(f, &doc), 0);
  fclose(f);

  ck_assert_int_eq(doc.channel_count, 2);
  ck_assert_str_eq(doc.channels[0].id, "ch1");
  ck_assert_int_eq(doc.channels[0].name_count, 2);
  ck_assert_str_eq(doc.channels[0].uri, "rtp://239.1.1.1:5000");
  ck_assert_uint_eq(doc.channels[0].sid, 0u);
  ck_assert_str_eq(doc.channels[1].id, "ch2");
  ck_assert_str_eq(doc.channels[1].uri, "");
  ck_assert_uint_eq(doc.channels[1].onid, 1u);
  ck_assert_uint_eq(doc.channels[1].tsid, 2u);
  ck_assert_uint_eq(doc.channels[1].sid, 0x1fu);

  ck_assert_int_eq(doc.programme_count, 2);
  ck_assert_str_eq(doc.programmes[0].title, "Known");
  ck_assert_str_eq(doc.programmes[0].desc, "Desc");
  ck_assert_str_eq(doc.programmes[0].stop, "");
  ck_assert_ptr_null(doc.programmes[1].title);
  ck_assert_str_eq(doc.programmes[1].stop, "2024-01-01T12:00:00Z");
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(tva_xml_read_of_empty_input_yields_an_empty_document) {
  bcg_doc_t doc;
  FILE *f = tmpfile();

  ck_assert_ptr_nonnull(f);
  bcg_doc_init(&doc);
  ck_assert_int_eq(tva_xml_read(f, &doc), 0);
  ck_assert_int_eq(doc.channel_count, 0);
  ck_assert_int_eq(doc.programme_count, 0);
  fclose(f);
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(tva_xml_read_logs_unreadable_input) {
  bcg_doc_t doc;
  char msg[LOG_CAPTURE_BUF];
  FILE *f = fopen("/dev/null", "w");

  ck_assert_ptr_nonnull(f);
  bcg_doc_init(&doc);
  log_capture_begin();
  ck_assert_int_eq(tva_xml_read(f, &doc), -1);
  log_capture_end(msg, sizeof msg);
  ck_assert_ptr_nonnull(strstr(msg, "LE500230"));
  fclose(f);
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(tva_xml_read_accepts_prefixes_single_quotes_and_cdata) {
  static const char xml[] =
      "<tva:TVAMain xmlns:tva='urn:tva'><tva:ProgramDescription>\n"
      "<tva:ProgramInformationTable>\n"
      "<tva:ProgramInformation programId = 'crid://x/1'><tva:BasicDescription>"
      "<tva:Title><![CDATA[Tom & Jerry]]></tva:Title></tva:BasicDescription></tva:ProgramInformation>\n"
      "</tva:ProgramInformationTable>\n"
      "<tva:ServiceInformationTable>"
      "<tva:ServiceInformation serviceId='svc1'><tva:Name>Svc One</tva:Name>"
      "<tva:ServiceURL name='IPTV'>rtp://239.1.1.1:5000</tva:ServiceURL>"
      "<tva:ServiceURL name='DTT'>dvb://1.2.a</tva:ServiceURL></tva:ServiceInformation>"
      "</tva:ServiceInformationTable>\n"
      "<tva:ProgramLocationTable><tva:Schedule serviceIDRef = \"svc1\">"
      "<tva:ScheduleEvent><tva:Program crid='crid://x/1'/>"
      "<tva:PublishedStartTime>2024-03-15T12:30:45Z</tva:PublishedStartTime>"
      "<tva:PublishedEndTime>2024-03-15T13:00:00Z</tva:PublishedEndTime>"
      "</tva:ScheduleEvent></tva:Schedule></tva:ProgramLocationTable>\n"
      "</tva:ProgramDescription></tva:TVAMain>\n";
  bcg_doc_t doc;
  FILE *f = tmpfile();

  ck_assert_ptr_nonnull(f);
  fputs(xml, f);
  rewind(f);
  bcg_doc_init(&doc);
  ck_assert_int_eq(tva_xml_read(f, &doc), 0);
  fclose(f);

  ck_assert_int_eq(doc.channel_count, 1);
  ck_assert_str_eq(doc.channels[0].id, "svc1");
  ck_assert_str_eq(doc.channels[0].names[0], "Svc One");
  ck_assert_str_eq(doc.channels[0].uri, "rtp://239.1.1.1:5000");
  ck_assert_uint_eq(doc.channels[0].sid, 10);
  ck_assert_int_eq(doc.programme_count, 1);
  ck_assert_str_eq(doc.programmes[0].channel_id, "svc1");
  ck_assert_str_eq(doc.programmes[0].start, "2024-03-15T12:30:45Z");
  ck_assert_str_eq(doc.programmes[0].title, "Tom & Jerry");
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(tva_xml_read_trims_whitespace_around_times) {
  const char xml[] =
    "<TVAMain><ProgramDescription><ProgramLocationTable><Schedule serviceIDRef=\"c\">"
    "<ScheduleEvent><Program crid=\"x\"/><PublishedStartTime>\n  2024-03-15T12:30:45.000Z \t</PublishedStartTime>"
    "<PublishedEndTime> 2024-03-15T13:00:00Z\n</PublishedEndTime></ScheduleEvent>"
    "</Schedule></ProgramLocationTable></ProgramDescription></TVAMain>\n";
  bcg_doc_t doc;
  FILE *f = tmpfile();

  ck_assert_ptr_nonnull(f);
  fputs(xml, f);
  rewind(f);
  bcg_doc_init(&doc);
  ck_assert_int_eq(tva_xml_read(f, &doc), 0);
  fclose(f);
  ck_assert_int_eq(doc.programme_count, 1);
  ck_assert_str_eq(doc.programmes[0].start, "2024-03-15T12:30:45.000Z");
  ck_assert_str_eq(doc.programmes[0].stop, "2024-03-15T13:00:00Z");
  bcg_doc_free(&doc);
}
END_TEST

static void read_one_stop(const char *start, const char *tail, char *stop, size_t cap) {
  char xml[512];
  bcg_doc_t doc;
  FILE *f = tmpfile();
  ck_assert_ptr_nonnull(f);
  snprintf(xml, sizeof xml,
           "<TVAMain><ProgramDescription><ProgramLocationTable><Schedule serviceIDRef=\"c\">"
           "<ScheduleEvent><Program crid=\"x\"/><PublishedStartTime>%s</PublishedStartTime>%s"
           "</ScheduleEvent></Schedule></ProgramLocationTable></ProgramDescription></TVAMain>\n",
           start, tail);
  fputs(xml, f);
  rewind(f);
  bcg_doc_init(&doc);
  ck_assert_int_eq(tva_xml_read(f, &doc), 0);
  fclose(f);
  ck_assert_int_eq(doc.programme_count, 1);
  snprintf(stop, cap, "%s", doc.programmes[0].stop);
  bcg_doc_free(&doc);
}

START_TEST(tva_xml_read_derives_stop_from_published_duration) {
  char stop[64];
  read_one_stop("2024-03-15T12:30:45Z", "<PublishedDuration>PT1H30M</PublishedDuration>", stop, sizeof stop);
  ck_assert_str_eq(stop, "2024-03-15T14:00:45Z");
  read_one_stop("2024-12-31T23:30:00.500+01:00", "<PublishedDuration> PT45M10S </PublishedDuration>", stop, sizeof stop);
  ck_assert_str_eq(stop, "2025-01-01T00:15:10+01:00");
  read_one_stop("2024-02-28T10:00:00", "<PublishedDuration>P1DT2H</PublishedDuration>", stop, sizeof stop);
  ck_assert_str_eq(stop, "2024-02-29T12:00:00");
  read_one_stop("2024-03-15T12:00:00-05:30", "<PublishedDuration>PT0.5S</PublishedDuration>", stop, sizeof stop);
  ck_assert_str_eq(stop, "2024-03-15T12:00:00-05:30");
}
END_TEST

START_TEST(tva_xml_read_end_time_wins_and_bad_duration_leaves_stop_empty) {
  char stop[64];
  read_one_stop("2024-03-15T12:00:00Z",
                "<PublishedDuration>PT1H</PublishedDuration><PublishedEndTime>2024-03-15T12:20:00Z</PublishedEndTime>", stop,
                sizeof stop);
  ck_assert_str_eq(stop, "2024-03-15T12:20:00Z");
  read_one_stop("2024-03-15T12:00:00Z", "<PublishedDuration>P1M</PublishedDuration>", stop, sizeof stop);
  ck_assert_str_eq(stop, "");
  read_one_stop("2024-03-15T12:00:00Z", "<PublishedDuration>P</PublishedDuration>", stop, sizeof stop);
  ck_assert_str_eq(stop, "");
  read_one_stop("2024-03-15T12:00:00Z", "<PublishedDuration>1H</PublishedDuration>", stop, sizeof stop);
  ck_assert_str_eq(stop, "");
  read_one_stop("2024-03-15T12:00:00Z", "<PublishedDuration>-PT1H</PublishedDuration>", stop, sizeof stop);
  ck_assert_str_eq(stop, "");
}
END_TEST

START_TEST(tva_genre_href_maps_known_and_falls_back) {
  ck_assert_str_eq(tva_genre_href("News"), "urn:tva:metadata:cs:ContentCS:2011:3.1.1");
  ck_assert_str_eq(tva_genre_href("SPORTS"), "urn:tva:metadata:cs:ContentCS:2011:3.2");
  ck_assert_str_eq(tva_genre_href("Soap"), "urn:tva:metadata:cs:ContentCS:2011:3.4.2");
  ck_assert_str_eq(tva_genre_href("Zorbing"), "urn:tva:metadata:cs:ContentCS:2011:3.0");
}
END_TEST

START_TEST(tva_xml_write_emits_mapped_genre_href) {
  bcg_doc_t doc;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  FILE *f;
  char *xml;
  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "ch");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  pr = bcg_add_programme(&doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "ch");
  bufcpy(pr->start, sizeof pr->start, "2024-03-15T12:30:45Z");
  bcg_set_text(&pr->category, "Comedy");
  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  tva_xml_write(f, &doc);
  xml = slurp_tmp(f);
  ck_assert_ptr_nonnull(strstr(xml, "<Genre href=\"urn:tva:metadata:cs:ContentCS:2011:3.5.7\"><Name>Comedy</Name></Genre>"));
  free(xml);
  fclose(f);
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(tva_xml_read_takes_names_from_the_right_elements) {
  const char xml[] =
    "<TVAMain><ProgramDescription><ProgramInformationTable>"
    "<ProgramInformation programId=\"a\"><BasicDescription><Title>A</Title>"
    "<ParentalGuidance><ParentalRating href=\"x\"><Name>FSK 12</Name></ParentalRating></ParentalGuidance>"
    "<Genre href=\"urn:g\"/></BasicDescription></ProgramInformation>"
    "<ProgramInformation programId=\"b\"><BasicDescription><Title>B</Title>"
    "<ParentalGuidance><ParentalRating href=\"x\"><Name>FSK 12</Name></ParentalRating></ParentalGuidance>"
    "<Genre href=\"urn:g\"><Name>Drama</Name></Genre></BasicDescription></ProgramInformation>"
    "</ProgramInformationTable><ProgramLocationTable><Schedule serviceIDRef=\"c\">"
    "<ScheduleEvent><Program crid=\"a\"/><PublishedStartTime>2024-03-15T12:00:00Z</PublishedStartTime></ScheduleEvent>"
    "<ScheduleEvent><Program crid=\"b\"/><PublishedStartTime>2024-03-15T13:00:00Z</PublishedStartTime></ScheduleEvent>"
    "</Schedule></ProgramLocationTable><ServiceInformationTable>"
    "<ServiceInformation serviceId=\"c\"><ServiceGenre href=\"g1\"><Name>Genre1</Name></ServiceGenre>"
    "<Name>Chan</Name><ServiceGenre href=\"g2\"><Name>Genre2</Name></ServiceGenre>"
    "<ServiceURL name=\"IPTV\">rtp://239.1.1.1:5000</ServiceURL></ServiceInformation>"
    "</ServiceInformationTable></ProgramDescription></TVAMain>\n";
  bcg_doc_t doc;
  FILE *f = tmpfile();

  ck_assert_ptr_nonnull(f);
  fputs(xml, f);
  rewind(f);
  bcg_doc_init(&doc);
  ck_assert_int_eq(tva_xml_read(f, &doc), 0);
  fclose(f);
  ck_assert_int_eq(doc.channel_count, 1);
  ck_assert_int_eq(doc.channels[0].name_count, 1);
  ck_assert_str_eq(doc.channels[0].names[0], "Chan");
  ck_assert_int_eq(doc.programme_count, 2);
  ck_assert_ptr_null(doc.programmes[0].category);
  ck_assert_str_eq(doc.programmes[1].category, "Drama");
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(tva_time_helpers_convert_and_reject) {
  char out[64];
  char small[8];
  long long secs;
  long diff;
  long dur;
  char ws[] = " \t\n x y \r\n";
  char blank[] = "  \t ";
  char empty[] = "";
  iso8601_compact_prefix("2024-05-01T10:20:30Z", out, sizeof out);
  ck_assert_str_eq(out, "20240501102030Z");
  iso8601_compact_prefix("2024-05-01T10:20:30Z", small, sizeof small);
  ck_assert_str_eq(small, "2024050");
  iso8601_compact_prefix("", out, sizeof out);
  ck_assert_str_eq(out, "");
  ck_assert_int_eq(iso8601_utc_secs("1970-01-02T00:00:00Z", &secs), 0);
  ck_assert_int_eq((int)secs, 86400);
  ck_assert_int_eq(iso8601_utc_secs("1970-01-02T01:00:00+01:00", &secs), 0);
  ck_assert_int_eq((int)secs, 86400);
  ck_assert_int_eq(iso8601_utc_secs("1970-01-02T00:00:00", &secs), 0);
  ck_assert_int_eq(iso8601_utc_secs("garbage", &secs), -1);
  ck_assert_int_eq(iso8601_utc_compact("2024-05-01T10:20:30+02:00", out, sizeof out), 0);
  ck_assert_str_eq(out, "20240501082030");
  ck_assert_int_eq(iso8601_utc_compact("1969-12-31T23:00:00-02:00", out, sizeof out), 0);
  ck_assert_str_eq(out, "19700101010000");
  ck_assert_int_eq(iso8601_utc_compact("nope", out, sizeof out), -1);
  trim_ws(ws);
  ck_assert_str_eq(ws, "x y");
  trim_ws(blank);
  ck_assert_str_eq(blank, "");
  trim_ws(empty);
  ck_assert_str_eq(empty, "");
  ck_assert_int_eq(duration_to_secs("P1DT2H3M4S", &dur), 0);
  ck_assert_int_eq((int)dur, 86400 + 7200 + 180 + 4);
  ck_assert_int_eq(duration_to_secs("PT1.5S", &dur), 0);
  ck_assert_int_eq((int)dur, 1);
  ck_assert_int_eq(duration_to_secs("PT0S", &dur), 0);
  ck_assert_int_eq(duration_to_secs("PT5M", &dur), 0);
  ck_assert_int_eq((int)dur, 300);
  ck_assert_int_eq(duration_to_secs("P2D", &dur), 0);
  ck_assert_int_eq(duration_to_secs("", &dur), -1);
  ck_assert_int_eq(duration_to_secs("X", &dur), -1);
  ck_assert_int_eq(duration_to_secs("P", &dur), -1);
  ck_assert_int_eq(duration_to_secs("PT", &dur), -1);
  ck_assert_int_eq(duration_to_secs("PTT1S", &dur), -1);
  ck_assert_int_eq(duration_to_secs("P1H", &dur), -1);
  ck_assert_int_eq(duration_to_secs("PT1D", &dur), -1);
  ck_assert_int_eq(duration_to_secs("P1S", &dur), -1);
  ck_assert_int_eq(duration_to_secs("PT1", &dur), -1);
  ck_assert_int_eq(duration_to_secs("PT1X", &dur), -1);
  ck_assert_int_eq(duration_to_secs("PTS", &dur), -1);
  ck_assert_int_eq(duration_to_secs("P-1D", &dur), -1);
  ck_assert_int_eq(duration_to_secs("P999999999D", &dur), -1);
  ck_assert_int_eq(iso8601_add_secs("2024-05-01T10:00:00Z", 3600, out, sizeof out), 0);
  ck_assert_str_eq(out, "2024-05-01T11:00:00Z");
  ck_assert_int_eq(iso8601_add_secs("2024-05-01T10:00:00+05:30", 90, out, sizeof out), 0);
  ck_assert_ptr_nonnull(strstr(out, "+05:30"));
  ck_assert_int_eq(iso8601_add_secs("2024-05-01T10:00:00-03:15", 86400, out, sizeof out), 0);
  ck_assert_ptr_nonnull(strstr(out, "-03:15"));
  ck_assert_int_eq(iso8601_add_secs("2024-05-01T10:00:00", -60, out, sizeof out), 0);
  ck_assert_int_eq(iso8601_add_secs("bad", 1, out, sizeof out), -1);
  ck_assert_int_eq(iso8601_add_secs("2024-05-01T10:00:00Z", 1, small, 2), -1);
  ck_assert_int_eq(iso8601_diff_secs("2024-05-01T10:00:00Z", "2024-05-01T10:01:00Z", &diff), 0);
  ck_assert_int_eq((int)diff, 60);
  ck_assert_int_eq(iso8601_diff_secs("bad", "2024-05-01T10:01:00Z", &diff), -1);
  ck_assert_int_eq(iso8601_diff_secs("2024-05-01T10:01:00Z", "bad", &diff), -1);
  ck_assert_int_eq(iso8601_diff_secs("1990-01-01T00:00:00Z", "2024-05-01T10:01:00Z", &diff), -1);
  ck_assert_int_eq(iso8601_diff_secs("2024-05-01T10:01:00Z", "1990-01-01T00:00:00Z", &diff), -1);
  ck_assert_int_eq(secs_to_duration(3725, out, sizeof out), 0);
  ck_assert_str_eq(out, "PT1H2M5S");
  ck_assert_int_eq(secs_to_duration(-1, out, sizeof out), -1);
  ck_assert_int_eq(secs_to_duration(3725, small, 4), -1);
}
END_TEST

static void rich_doc(bcg_doc_t *d) {
  bcg_channel_t *c;
  bcg_programme_t *p;
  bcg_doc_init(d);
  c = bcg_add_channel(d);
  bufcpy(c->id, sizeof c->id, "a");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  bcg_channel_add_name(c, "A One");
  c->onid = 1;
  c->tsid = 2;
  c->sid = 3;
  c = bcg_add_channel(d);
  bufcpy(c->id, sizeof c->id, "nouri");
  c = bcg_add_channel(d);
  bufcpy(c->id, sizeof c->id, "idle");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.3:5000");
  for (int i = 0; i < 5; i++) {
    p = bcg_add_programme(d);
    bufcpy(p->channel_id, sizeof p->channel_id, i == 3 ? "ghost" : i == 4 ? "nouri" : "a");
    bufcpy(p->start, sizeof p->start, "2024-05-01T10:00:00Z");
    if (i == 1) bufcpy(p->stop, sizeof p->stop, "2024-05-01T11:00:00Z");
    if (i != 2) bcg_set_text(&p->title, "T & <t>");
    if (i == 0) {
      bcg_set_text(&p->desc, "D");
      bcg_set_text(&p->category, "News");
    }
  }
}

START_TEST(tva_xml_write_covers_optional_parts_and_oom_fallback) {
  bcg_doc_t d;
  char *buf = NULL;
  size_t sz = 0;
  FILE *f;
  rich_doc(&d);
  f = open_memstream(&buf, &sz);
  ck_assert_ptr_nonnull(f);
  tva_xml_write(f, &d);
  fclose(f);
  ck_assert_ptr_nonnull(strstr(buf, "PublishedEndTime"));
  ck_assert_ptr_nonnull(strstr(buf, "<Synopsis>D</Synopsis>"));
  ck_assert_ptr_nonnull(strstr(buf, "name=\"DTT\""));
  ck_assert_ptr_null(strstr(buf, "ghost"));
  free(buf);
  for (long n = 0;; n++) {
    int hit;
    f = tmpfile();
    ck_assert_ptr_nonnull(f);
    alloc_fail_at(n);
    tva_xml_write(f, &d);
    hit = alloc_fail_end();
    fclose(f);
    if (!hit) break;
  }
  bcg_doc_free(&d);
}
END_TEST

START_TEST(tva_xml_read_survives_allocation_failures) {
  bcg_doc_t d;
  char *buf = NULL;
  size_t sz = 0;
  FILE *f;
  rich_doc(&d);
  f = open_memstream(&buf, &sz);
  ck_assert_ptr_nonnull(f);
  tva_xml_write(f, &d);
  fclose(f);
  bcg_doc_free(&d);
  for (long n = 0;; n++) {
    bcg_doc_t back;
    int hit;
    FILE *in = fmemopen(buf, sz, "r");
    ck_assert_ptr_nonnull(in);
    bcg_doc_init(&back);
    alloc_fail_at(n);
    (void)tva_xml_read(in, &back);
    hit = alloc_fail_end();
    fclose(in);
    bcg_doc_free(&back);
    if (!hit) break;
  }
  free(buf);
}
END_TEST

START_TEST(tva_crid_caching_and_unparseable_starts) {
  bcg_programme_t pr;
  char crid[TVA_CRID_BUF];
  int rc;
  memset(&pr, 0, sizeof pr);
  snprintf(pr.channel_id, sizeof pr.channel_id, "ch 1");
  snprintf(pr.start, sizeof pr.start, "2024-05-01T10:20:30+02:00");
  alloc_fail_at(0);
  rc = tva_programme_cache_crid(&pr);
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  ck_assert_ptr_null(pr.crid);
  ck_assert_int_eq(tva_programme_cache_crid(&pr), 0);
  ck_assert_str_eq(pr.crid, "crid://dipixmltv.invalid/ch%201/20240501082030");
  ck_assert_int_eq(tva_programme_cache_crid(&pr), 0);
  free(pr.crid);
  pr.crid = NULL;
  snprintf(pr.start, sizeof pr.start, "20240501T10:20:30garbage!!");
  tva_build_crid(pr.channel_id, pr.start, crid, sizeof crid);
  ck_assert_ptr_nonnull(strstr(crid, "crid://dipixmltv.invalid/ch%201/"));
  snprintf(pr.start, sizeof pr.start, "x");
  tva_build_crid(pr.channel_id, pr.start, crid, sizeof crid);
  ck_assert_ptr_nonnull(strstr(crid, "/x"));
  snprintf(pr.start, sizeof pr.start, "12345678901234567890123456");
  tva_build_crid(pr.channel_id, pr.start, crid, sizeof crid);
  ck_assert_str_eq(tva_genre_href("NEWS"), "urn:tva:metadata:cs:ContentCS:2011:3.1.1");
  ck_assert_str_eq(tva_genre_href("unknown thing"), "urn:tva:metadata:cs:ContentCS:2011:3.0");
}
END_TEST

static Suite *tva_xml_suite(void) {
  Suite *s = suite_create("tva_xml");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, tva_xml_write_read_round_trips);
  tcase_add_test(tc, tva_xml_write_dtt_is_hex_and_omitted_without_triplet);
  tcase_add_test(tc, tva_xml_write_drops_channels_without_uri);
  tcase_add_test(tc, tva_build_crid_percent_encodes_and_compacts_time);
  tcase_add_test(tc, tva_build_crid_dst_overlap_distinct);
  tcase_add_test(tc, tva_build_crid_offset_crosses_day);
  tcase_add_test(tc, tva_programme_crid_cached_and_fallback);
  tcase_add_test(tc, tva_xml_write_groups_programmes_for_unsorted_channels);
  tcase_add_test(tc, tva_xml_write_omits_absent_optional_fields);
  tcase_add_test(tc, tva_xml_write_handles_empty_documents_and_channels_without_programmes);
  tcase_add_test(tc, tva_xml_read_tolerates_incomplete_elements);
  tcase_add_test(tc, tva_xml_read_of_empty_input_yields_an_empty_document);
  tcase_add_test(tc, tva_xml_read_logs_unreadable_input);
  tcase_add_test(tc, tva_xml_read_accepts_prefixes_single_quotes_and_cdata);
  tcase_add_test(tc, tva_xml_read_trims_whitespace_around_times);
  tcase_add_test(tc, tva_xml_read_derives_stop_from_published_duration);
  tcase_add_test(tc, tva_xml_read_end_time_wins_and_bad_duration_leaves_stop_empty);
  tcase_add_test(tc, tva_genre_href_maps_known_and_falls_back);
  tcase_add_test(tc, tva_xml_write_emits_mapped_genre_href);
  tcase_add_test(tc, tva_xml_read_takes_names_from_the_right_elements);
  tcase_add_test(tc, tva_time_helpers_convert_and_reject);
  tcase_add_test(tc, tva_xml_write_covers_optional_parts_and_oom_fallback);
  tcase_add_test(tc, tva_xml_read_survives_allocation_failures);
  tcase_add_test(tc, tva_crid_caching_and_unparseable_starts);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tva_xml_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
