/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/ioutil.h"
#include "lib/xml_util.h"
#include "tva_xml.h"

static const char *unreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
static const char hex_digits[] = "0123456789ABCDEF";

static void percent_encode(const char *s, char *out, size_t outcap) {
  size_t oi = 0;
  for (; *s && oi + 1 < outcap; s++) {
    if (strchr(unreserved, *s)) {
      out[oi++] = *s;
    } else {
      unsigned char c = (unsigned char)*s;
      if (oi + 4 > outcap)
        break;
      out[oi] = '%';
      out[oi + 1] = hex_digits[c >> 4];
      out[oi + 2] = hex_digits[c & 0xF];
      oi += 3;
    }
  }
  out[oi] = '\0';
}

/* "YYYY-MM-DDTHH:MM:SS..." -> "YYYYMMDDHHMMSS", truncates rest */
static void iso8601_compact_prefix(const char *iso, char *out, size_t outcap) {
  size_t oi = 0;
  for (size_t i = 0; iso[i] && oi + 1 < outcap; i++)
    if (iso[i] != '-' && iso[i] != ':' && iso[i] != 'T')
      out[oi++] = iso[i];
  out[oi] = '\0';
}

void tva_build_crid(const char *channel_id, const char *start_iso, char *out, size_t outcap) {
  char enc[BCG_ID_LEN * 3];
  char ts[BCG_TIME_LEN];
  percent_encode(channel_id, enc, sizeof enc);
  iso8601_compact_prefix(start_iso, ts, sizeof ts);
  if (strlen(ts) >= 14)
    ts[14] = '\0';
  snprintf(out, outcap, "crid://dipixmltv.invalid/%s/%s", enc, ts);
}

void tva_xml_write(FILE *f, const bcg_doc_t *doc) {
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<TVAMain xmlns=\"urn:tva:metadata:2004\">\n<ProgramDescription>\n", f);
  fputs("<MetadataOriginationInformationTable/>\n<ClassificationSchemeTable/>\n", f);
  fputs("<ProgramInformationTable>\n", f);
  for (int i = 0; i < doc->programme_count; i++) {
    const bcg_programme_t *pr = &doc->programmes[i];
    const bcg_channel_t *c = bcg_find_channel(doc, pr->channel_id);
    char crid[BCG_ID_LEN * 3 + 64];
    if (!c || !c->uri[0])
      continue;
    tva_build_crid(pr->channel_id, pr->start, crid, sizeof crid);
    fputs("<ProgramInformation programId=\"", f);
    xml_escape(f, crid);
    fputs("\"><BasicDescription>", f);
    if (pr->title[0]) {
      fputs("<Title>", f);
      xml_escape(f, pr->title);
      fputs("</Title>", f);
    }
    if (pr->desc[0]) {
      fputs("<Synopsis>", f);
      xml_escape(f, pr->desc);
      fputs("</Synopsis>", f);
    }
    if (pr->category[0]) {
      /* ContentCS 3.0 = unclassified, real scheme not made-up */
      fputs("<Genre href=\"" TVA_CONTENTCS_2011_URN "\"><Name>", f);
      xml_escape(f, pr->category);
      fputs("</Name></Genre>", f);
    }
    fputs("</BasicDescription></ProgramInformation>\n", f);
  }
  fputs("</ProgramInformationTable>\n<GroupInformationTable/>\n", f);

  fputs("<ProgramLocationTable>\n", f);
  for (int i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    int any = 0;
    if (!c->uri[0])
      continue;
    for (int j = 0; j < doc->programme_count; j++)
      if (!strcmp(doc->programmes[j].channel_id, c->id)) {
        any = 1;
        break;
      }
    if (!any)
      continue;
    fputs("<Schedule serviceIDRef=\"", f);
    xml_escape(f, c->id);
    fputs("\">\n", f);
    for (int j = 0; j < doc->programme_count; j++) {
      const bcg_programme_t *pr = &doc->programmes[j];
      char crid[BCG_ID_LEN * 3 + 64];
      if (strcmp(pr->channel_id, c->id))
        continue;
      tva_build_crid(pr->channel_id, pr->start, crid, sizeof crid);
      fputs("<ScheduleEvent><Program crid=\"", f);
      xml_escape(f, crid);
      fprintf(f, "\"/><PublishedStartTime>%s</PublishedStartTime>", pr->start);
      if (pr->stop[0])
        fprintf(f, "<PublishedEndTime>%s</PublishedEndTime>", pr->stop);
      fputs("</ScheduleEvent>\n", f);
    }
    fputs("</Schedule>\n", f);
  }
  fputs("</ProgramLocationTable>\n", f);

  fputs("<ServiceInformationTable>\n", f);
  for (int i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    if (!c->uri[0])
      continue;
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
    fprintf(f, "<ServiceURL name=\"DTT\">dvb://%u.%u.%u</ServiceURL>\n", c->onid, c->tsid, c->sid);
    fputs("</ServiceInformation>\n", f);
  }
  fputs("</ServiceInformationTable>\n", f);
  fputs("<CreditsInformationTable/>\n<ProgramReviewTable/>\n"
        "<SegmentInformationTable><SegmentList/><SegmentGroupList/></SegmentInformationTable>\n"
        "<PurchaseInformationTable/>\n", f);
  fputs("</ProgramDescription>\n</TVAMain>\n", f);
}

typedef struct {
  const char *crid;
  int idx;
} progtext_idx_t;

typedef struct {
  bcg_progtext_t *items;
  int n, cap;
  progtext_idx_t *idx; /* sorted by crid, built once after parse_program_texts */
} progtext_list_t;

static int progtext_idx_cmp(const void *a, const void *b) {
  return strcmp(((const progtext_idx_t *)a)->crid, ((const progtext_idx_t *)b)->crid);
}

/* 0 ok, -1 OOM. pl->items must not change after this: idx entries are built from it */
static int progtext_list_build_index(progtext_list_t *pl) {
  if (!pl->n)
    return 0;
  pl->idx = malloc(sizeof *pl->idx * (size_t)pl->n);
  if (!pl->idx)
    return -1;
  for (int i = 0; i < pl->n; i++) {
    pl->idx[i].crid = pl->items[i].crid;
    pl->idx[i].idx = i;
  }
  qsort(pl->idx, (size_t)pl->n, sizeof *pl->idx, progtext_idx_cmp);
  return 0;
}

static bcg_progtext_t *find_progtext(const progtext_list_t *pl, const char *crid) {
  int lo = 0, hi = pl->n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(crid, pl->idx[mid].crid);
    if (c == 0)
      return &pl->items[pl->idx[mid].idx];
    if (c < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return NULL;
}

/* grows pl->items to fit one more entry if needed. 0 ok, -1 OOM */
static int progtext_grow(progtext_list_t *pl) {
  void *np;
  if (pl->n < pl->cap)
    return 0;
  np = array_grow(pl->items, &pl->cap, pl->n + 1, sizeof *pl->items);
  if (!np)
    return -1;
  pl->items = np;
  return 0;
}

static int program_text_cb(const char *tag, const char *blk_end, void *ctx) {
  progtext_list_t *pl = ctx;
  char crid[BCG_ID_LEN * 3 + 64];
  bcg_progtext_t *pt;
  if (xml_attr(tag, blk_end, "programId", crid, sizeof crid) == 0) {
    if (progtext_grow(pl) != 0)
      return -1;
    pt = &pl->items[pl->n++];
    memset(pt, 0, sizeof *pt);
    bufcpy(pt->crid, sizeof pt->crid, crid);
    if (xml_elem_text(tag, blk_end, "Title", pt->title, sizeof pt->title))
      pt->title[0] = '\0';
    if (xml_elem_text(tag, blk_end, "Synopsis", pt->desc, sizeof pt->desc))
      pt->desc[0] = '\0';
    if (xml_elem_text(tag, blk_end, "Name", pt->category, sizeof pt->category))
      pt->category[0] = '\0';
  }
  return 0;
}

/* scan ProgramInformation blocks into pl (growing it as needed). 0 ok, -1 OOM */
static int parse_program_texts(const char *buf, const char *end, progtext_list_t *pl) {
  return for_each_xml_block(buf, end, "<ProgramInformation ", "</ProgramInformation>", program_text_cb, pl);
}

/* collects every <Name> for channel c within [tag,blk_end) */
static void collect_service_names(bcg_channel_t *c, const char *tag, const char *blk_end) {
  const char *np = tag;
  char name[BCG_ID_LEN];
  for (;;) {
    const char *hit = strstr(np, "<Name>");
    if (!hit || hit >= blk_end)
      return;
    if (xml_elem_text(hit, blk_end, "Name", name, sizeof name))
      return;
    bcg_channel_add_name(c, name);
    np = hit + 1;
  }
}

/* parses IPTV/DTT <ServiceURL> entries within [tag,blk_end) for channel c */
static void parse_service_urls(bcg_channel_t *c, const char *tag, const char *blk_end) {
  const char *u1 = strstr(tag, "<ServiceURL name=\"IPTV\">");
  const char *u2 = strstr(tag, "<ServiceURL name=\"DTT\">");
  char dtt[64];
  unsigned onid, tsid, sid;

  if (u1 && u1 < blk_end && xml_elem_text(u1, blk_end, "ServiceURL", c->uri, sizeof c->uri))
    c->uri[0] = '\0';
  if (u2 && u2 < blk_end && xml_elem_text(u2, blk_end, "ServiceURL", dtt, sizeof dtt) == 0 &&
      sscanf(dtt, "dvb://%u.%u.%u", &onid, &tsid, &sid) == 3) {
    c->onid = onid;
    c->tsid = tsid;
    c->sid = sid;
  }
}

static int service_info_cb(const char *tag, const char *blk_end, void *ctx) {
  bcg_doc_t *doc = ctx;
  bcg_channel_t *c;
  char sid[BCG_ID_LEN];
  if (xml_attr(tag, blk_end, "serviceId", sid, sizeof sid) == 0) {
    c = bcg_add_channel(doc);
    if (!c)
      return -1;
    bufcpy(c->id, sizeof c->id, sid);
    collect_service_names(c, tag, blk_end);
    parse_service_urls(c, tag, blk_end);
  }
  return 0;
}

/* scan ServiceInformation blocks into doc's channels. 0 ok, -1 OOM */
static int parse_service_information(const char *buf, const char *end, bcg_doc_t *doc) {
  return for_each_xml_block(buf, end, "<ServiceInformation ", "</ServiceInformation>", service_info_cb, doc);
}

/* processes one ScheduleEvent [etag,eend) for channel. 0 ok, -1 OOM */
static int parse_schedule_event(bcg_doc_t *doc, const progtext_list_t *pl, const char *channel, const char *etag, const char *eend) {
  char crid[BCG_ID_LEN * 3 + 64];
  char start[BCG_TIME_LEN], stop[BCG_TIME_LEN];
  const bcg_progtext_t *pt;
  bcg_programme_t *pr;

  if (xml_attr(etag, eend, "crid", crid, sizeof crid) != 0 ||
      xml_elem_text(etag, eend, "PublishedStartTime", start, sizeof start) != 0)
    return 0;
  pr = bcg_add_programme(doc);
  if (!pr)
    return -1;
  bufcpy(pr->channel_id, sizeof pr->channel_id, channel);
  bufcpy(pr->start, sizeof pr->start, start);
  if (xml_elem_text(etag, eend, "PublishedEndTime", stop, sizeof stop) == 0)
    bufcpy(pr->stop, sizeof pr->stop, stop);
  else
    pr->stop[0] = '\0';
  pt = find_progtext(pl, crid);
  if (pt) {
    bufcpy(pr->title, sizeof pr->title, pt->title);
    bufcpy(pr->desc, sizeof pr->desc, pt->desc);
    bufcpy(pr->category, sizeof pr->category, pt->category);
  }
  return 0;
}

typedef struct {
  bcg_doc_t *doc;
  const progtext_list_t *pl;
  const char *channel;
} schedule_event_ctx_t;

static int schedule_event_cb(const char *tag, const char *blk_end, void *vctx) {
  schedule_event_ctx_t *ctx = vctx;
  return parse_schedule_event(ctx->doc, ctx->pl, ctx->channel, tag, blk_end);
}

/* scans ScheduleEvent entries within [tag,blk_end) for channel. 0 ok, -1 OOM */
static int parse_schedule_events(bcg_doc_t *doc, const progtext_list_t *pl, const char *channel, const char *tag, const char *blk_end) {
  schedule_event_ctx_t ctx = {doc, pl, channel};
  return for_each_xml_block(tag, blk_end, "<ScheduleEvent>", "</ScheduleEvent>", schedule_event_cb, &ctx);
}

typedef struct {
  bcg_doc_t *doc;
  const progtext_list_t *pl;
} schedule_ctx_t;

static int schedule_cb(const char *tag, const char *blk_end, void *vctx) {
  schedule_ctx_t *ctx = vctx;
  char channel[BCG_ID_LEN];
  if (xml_attr(tag, blk_end, "serviceIDRef", channel, sizeof channel) == 0 &&
      parse_schedule_events(ctx->doc, ctx->pl, channel, tag, blk_end) != 0)
    return -1;
  return 0;
}

/* scan Schedule blocks, adding programmes to doc. 0 ok, -1 OOM */
static int parse_schedule(const char *buf, const char *end, bcg_doc_t *doc, const progtext_list_t *pl) {
  schedule_ctx_t ctx = {doc, pl};
  return for_each_xml_block(buf, end, "<Schedule ", "</Schedule>", schedule_cb, &ctx);
}

int tva_xml_read(FILE *f, bcg_doc_t *doc) {
  char *buf;
  size_t len;
  const char *end;
  progtext_list_t pl = {0};
  int rc;

  if (read_all(f, &buf, &len))
    return -1;
  end = buf + len;
  rc = parse_program_texts(buf, end, &pl) == 0 && progtext_list_build_index(&pl) == 0 && parse_service_information(buf, end, doc) == 0 &&
               parse_schedule(buf, end, doc, &pl) == 0 ? 0 : -1;

  free(buf);
  free(pl.items);
  free(pl.idx);
  return rc;
}
