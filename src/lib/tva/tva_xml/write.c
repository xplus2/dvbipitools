/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/xml_util.h"
#include "lib/tva/tva_xml.h"
#include "lib/tva/tva_xml/tva_xml_int.h"

typedef struct {
  const char *id;
  int idx;
} chan_idx_t;

static int chan_idx_cmp(const void *a, const void *b) { return strcmp(((const chan_idx_t *)a)->id, ((const chan_idx_t *)b)->id); }

static int chan_idx_find(const chan_idx_t *idx, int n, const char *id) {
  int lo = 0;
  int hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(id, idx[mid].id);
    if (c == 0) return idx[mid].idx;
    if (c < 0) hi = mid - 1;
    else       lo = mid + 1;
  }
  return -1;
}

/* -1 not found. cidx NULL (index build failed): falls back to bcg_find_channel */
static int channel_index_of(const bcg_doc_t *doc, const chan_idx_t *cidx, const char *channel_id) {
  const bcg_channel_t *c;
  if (cidx) return chan_idx_find(cidx, doc->channel_count, channel_id);
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
  chan_idx_t *cidx = NULL;
  int *first = NULL;
  int *last = NULL;
  int *next = NULL;
  int have_groups = 0;

  if (doc->channel_count > 0) {
    cidx = malloc(sizeof *cidx * (size_t)doc->channel_count);
    if (cidx) {
      for (int i = 0; i < doc->channel_count; i++) {
        cidx[i].id = doc->channels[i].id;
        cidx[i].idx = i;
      }
      qsort(cidx, (size_t)doc->channel_count, sizeof *cidx, chan_idx_cmp);
    }
    first = malloc(sizeof *first * (size_t)doc->channel_count);
    last = malloc(sizeof *last * (size_t)doc->channel_count);
  }
  if (doc->programme_count > 0) next = malloc(sizeof *next * (size_t)doc->programme_count);
  /* channel-id index + per-channel programme chain, built once. avoids an
     O(channels x programmes) scan below. falls back to the plain scan on OOM */
  if (first && last && (doc->programme_count == 0 || next)) {
    for (int i = 0; i < doc->channel_count; i++) {
      first[i] = -1;
      last[i] = -1;
    }
    for (int j = 0; j < doc->programme_count; j++) {
      int idx = channel_index_of(doc, cidx, doc->programmes[j].channel_id);
      next[j] = -1;
      if (idx < 0) continue;
      if (last[idx] < 0) first[idx] = j;
      else               next[last[idx]] = j;
      last[idx] = j;
    }
    have_groups = 1;
  }

  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<TVAMain xmlns=\"urn:tva:metadata:2004\">\n", f);
  fputs("<MetadataOriginationInformationTable/>\n<ClassificationSchemeTable/>\n<ProgramDescription>\n", f);
  fputs("<ProgramInformationTable>\n", f);
  for (int i = 0; i < doc->programme_count; i++) {
    const bcg_programme_t *pr = &doc->programmes[i];
    int ci = channel_index_of(doc, cidx, pr->channel_id);
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
      any = first[i] != -1;
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
      for (int j = first[i]; j != -1; j = next[j])
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

  free(cidx);
  free(first);
  free(last);
  free(next);

  fputs("<ServiceInformationTable>\n", f);
  for (int i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
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
    if (c->onid || c->tsid || c->sid)
      fprintf(f, "<ServiceURL name=\"DTT\">dvb://%x.%x.%x</ServiceURL>\n", c->onid, c->tsid, c->sid);
    fputs("</ServiceInformation>\n", f);
  }
  fputs("</ServiceInformationTable>\n", f);
  fputs("<CreditsInformationTable/>\n<ProgramReviewTable/>\n"
        "<SegmentInformationTable><SegmentList/><SegmentGroupList/></SegmentInformationTable>\n"
        "<PurchaseInformationTable/>\n", f);
  fputs("</ProgramDescription>\n</TVAMain>\n", f);
}
