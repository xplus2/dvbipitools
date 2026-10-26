/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "lib/helper/xml_util.h"
#include "lib/tva/tva_xml.h"

static int channel_index_of(const bcg_doc_t *doc, const bcg_doc_index_t *ix, const char *channel_id) {
  const bcg_channel_t *c;
  if (ix->chan) return bcg_key_index_find(ix->chan, doc->channel_count, channel_id);
  c = bcg_find_channel(doc, channel_id);
  return c ? (int)(c - doc->channels) : -1;
}

static void write_schedule_event(FILE *f, const bcg_programme_t *pr) {
  char buf[TVA_CRID_BUF];
  fputs("<ScheduleEvent><Program crid=\"", f);
  xml_escape(f, tva_programme_crid(pr, buf, sizeof buf));
  fprintf(f, "\"/><PublishedStartTime>%s</PublishedStartTime>", pr->start);
  if (pr->stop[0]) fprintf(f, "<PublishedEndTime>%s</PublishedEndTime>", pr->stop);
  fputs("</ScheduleEvent>\n", f);
}

void tva_xml_write(FILE *f, const bcg_doc_t *doc) {
  bcg_doc_index_t ix;
  int have_groups = !bcg_doc_index_build(&ix, doc);

  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<TVAMain xmlns=\"", f);
  xml_escape(f, bcg_doc_ns(doc));
  fputs("\">\n", f);
  fputs("<MetadataOriginationInformationTable/>\n<ClassificationSchemeTable/>\n<ProgramDescription>\n", f);
  fputs("<ProgramInformationTable>\n", f);
  for (int i = 0; i < doc->programme_count; i++) {
    const bcg_programme_t *pr = &doc->programmes[i];
    int ci = channel_index_of(doc, &ix, pr->channel_id);
    char buf[TVA_CRID_BUF];
    if (ci < 0 || !doc->channels[ci].uri[0]) continue;
    fputs("<ProgramInformation programId=\"", f);
    xml_escape(f, tva_programme_crid(pr, buf, sizeof buf));
    fputs("\"><BasicDescription>", f);
    if (pr->title) {
      fputs("<Title>", f);
      xml_escape(f, pr->title);
      fputs("</Title>", f);
    }
    if (pr->desc) {
      fputs("<Synopsis>", f);
      xml_escape(f, pr->desc);
      fputs("</Synopsis>", f);
    }
    if (pr->category) {
      fputs("<Genre href=\"", f);
      fputs(tva_genre_href(pr->category), f);
      fputs("\"><Name>", f);
      xml_escape(f, pr->category);
      fputs("</Name></Genre>", f);
    }
    fputs("</BasicDescription></ProgramInformation>\n", f);
  }
  fputs("</ProgramInformationTable>\n<GroupInformationTable/>\n", f);

  fputs("<ProgramLocationTable>\n", f);
  for (int i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    int any;
    if (!c->uri[0]) continue;
    if (have_groups) {
      any = ix.first[i] != -1;
    } else {
      any = 0;
      for (int j = 0; j < doc->programme_count; j++)
        if (!strcmp(doc->programmes[j].channel_id, c->id)) {
          any = 1;
          break;
        }
    }
    if (!any) continue;
    fputs("<Schedule serviceIDRef=\"", f);
    xml_escape(f, c->id);
    fputs("\">\n", f);
    if (have_groups) {
      for (int j = ix.first[i]; j != -1; j = ix.next[j])
        write_schedule_event(f, &doc->programmes[j]);
    } else {
      for (int j = 0; j < doc->programme_count; j++) {
        if (strcmp(doc->programmes[j].channel_id, c->id)) continue;
        write_schedule_event(f, &doc->programmes[j]);
      }
    }
    fputs("</Schedule>\n", f);
  }
  fputs("</ProgramLocationTable>\n", f);

  if (have_groups) bcg_doc_index_free(&ix);

  fputs("<ServiceInformationTable>\n", f);
  for (int i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    char dtt[64];
    if (!c->uri[0]) continue;
    fputs("<ServiceInformation serviceId=\"", f);
    xml_escape(f, c->id);
    fputs("\">\n", f);
    for (int j = 0; j < c->name_count; j++) {
      fputs("<Name>", f);
      xml_escape(f, c->names[j]);
      fputs("</Name>\n", f);
    }
    fputs("<ServiceURL name=\"IPTV\">", f);
    xml_escape(f, c->uri);
    fputs("</ServiceURL>\n", f);
    bcg_channel_dtt_format(c, dtt, sizeof dtt);
    if (dtt[0]) fprintf(f, "<ServiceURL name=\"DTT\">%s</ServiceURL>\n", dtt);
    fputs("</ServiceInformation>\n", f);
  }
  fputs("</ServiceInformationTable>\n", f);
  fputs("<CreditsInformationTable/>\n<ProgramReviewTable/>\n"
        "<SegmentInformationTable><SegmentList/><SegmentGroupList/></SegmentInformationTable>\n"
        "<PurchaseInformationTable/>\n", f);
  fputs("</ProgramDescription>\n</TVAMain>\n", f);
}
