/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/ioutil.h"
#include "lib/xml_util.h"
#include "tva_xml.h"

static const char *unreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";

static void percent_encode(const char *s, char *out, size_t outcap) {
  size_t oi = 0;
  for (; *s && oi + 1 < outcap; s++) {
    if (strchr(unreserved, *s)) {
      out[oi++] = *s;
    } else {
      if (oi + 4 > outcap)
        break;
      snprintf(out + oi, 4, "%%%02X", (unsigned char)*s);
      oi += 3;
    }
  }
  out[oi] = '\0';
}

/* "YYYY-MM-DDTHH:MM:SS..." -> "YYYYMMDDHHMMSS", truncates the rest */
static void iso8601_compact_prefix(const char *iso, char *out, size_t outcap) {
  size_t i, oi = 0;
  for (i = 0; iso[i] && oi + 1 < outcap; i++)
    if (iso[i] != '-' && iso[i] != ':' && iso[i] != 'T')
      out[oi++] = iso[i];
  out[oi] = '\0';
}

void tva_build_crid(const char *channel_id, const char *start_iso, char *out, size_t outcap) {
  char enc[EPG_ID_LEN * 3];
  char ts[EPG_TIME_LEN];
  percent_encode(channel_id, enc, sizeof enc);
  iso8601_compact_prefix(start_iso, ts, sizeof ts);
  if (strlen(ts) >= 14)
    ts[14] = '\0';
  snprintf(out, outcap, "crid://dipixmltv.invalid/%s/%s", enc, ts);
}

void tva_xml_write(FILE *f, const epg_doc_t *doc) {
  int i, j;

  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<TVAMain xmlns=\"urn:tva:metadata:2004\">\n<ProgramDescription>\n", f);
  fputs("<MetadataOriginationInformationTable/>\n<ClassificationSchemeTable/>\n", f);
  fputs("<ProgramInformationTable>\n", f);
  for (i = 0; i < doc->programme_count; i++) {
    const epg_programme_t *pr = &doc->programmes[i];
    const epg_channel_t *c = epg_find_channel(doc, pr->channel_id);
    char crid[EPG_ID_LEN * 3 + 64];
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
      fputs("<Genre href=\"urn:tva:metadata:cs:ContentCS:2011:3.0\"><Name>", f);
      xml_escape(f, pr->category);
      fputs("</Name></Genre>", f);
    }
    fputs("</BasicDescription></ProgramInformation>\n", f);
  }
  fputs("</ProgramInformationTable>\n<GroupInformationTable/>\n", f);

  fputs("<ProgramLocationTable>\n", f);
  for (i = 0; i < doc->channel_count; i++) {
    const epg_channel_t *c = &doc->channels[i];
    int any = 0;
    if (!c->uri[0])
      continue;
    for (j = 0; j < doc->programme_count; j++)
      if (!strcmp(doc->programmes[j].channel_id, c->id)) {
        any = 1;
        break;
      }
    if (!any)
      continue;
    fputs("<Schedule serviceIDRef=\"", f);
    xml_escape(f, c->id);
    fputs("\">\n", f);
    for (j = 0; j < doc->programme_count; j++) {
      const epg_programme_t *pr = &doc->programmes[j];
      char crid[EPG_ID_LEN * 3 + 64];
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
  for (i = 0; i < doc->channel_count; i++) {
    const epg_channel_t *c = &doc->channels[i];
    if (!c->uri[0])
      continue;
    fputs("<ServiceInformation serviceId=\"", f);
    xml_escape(f, c->id);
    fputs("\">\n", f);
    for (j = 0; j < c->name_count; j++) {
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
  char crid[EPG_ID_LEN * 3 + 64];
  char title[EPG_TEXT_LEN];
  char desc[EPG_TEXT_LEN];
  char category[EPG_ID_LEN];
} progtext_t;

static progtext_t *find_progtext(progtext_t *arr, int n, const char *crid) {
  int i;
  for (i = 0; i < n; i++)
    if (!strcmp(arr[i].crid, crid))
      return &arr[i];
  return NULL;
}

int tva_xml_read(FILE *f, epg_doc_t *doc) {
  char *buf;
  size_t len;
  const char *p, *end;
  progtext_t *ptext = NULL;
  int ptext_n = 0, ptext_cap = 0;

  if (read_all(f, &buf, &len))
    return -1;
  end = buf + len;

  p = buf;
  for (;;) {
    const char *tag = strstr(p, "<ProgramInformation ");
    const char *blk_end;
    char crid[EPG_ID_LEN * 3 + 64];
    progtext_t *pt;
    if (!tag || tag >= end)
      break;
    blk_end = strstr(tag, "</ProgramInformation>");
    if (!blk_end)
      break;
    if (xml_attr(tag, blk_end, "programId", crid, sizeof crid) == 0) {
      if (ptext_n >= ptext_cap) {
        int newcap = ptext_cap ? ptext_cap * 2 : 32;
        void *np = realloc(ptext, (size_t)newcap * sizeof *ptext);
        if (!np) {
          free(buf);
          free(ptext);
          return -1;
        }
        ptext = np;
        ptext_cap = newcap;
      }
      pt = &ptext[ptext_n++];
      memset(pt, 0, sizeof *pt);
      snprintf(pt->crid, sizeof pt->crid, "%s", crid);
      if (xml_elem_text(tag, blk_end, "Title", pt->title, sizeof pt->title))
        pt->title[0] = '\0';
      if (xml_elem_text(tag, blk_end, "Synopsis", pt->desc, sizeof pt->desc))
        pt->desc[0] = '\0';
      if (xml_elem_text(tag, blk_end, "Name", pt->category, sizeof pt->category))
        pt->category[0] = '\0';
    }
    p = blk_end + 1;
  }

  p = buf;
  for (;;) {
    const char *tag = strstr(p, "<ServiceInformation ");
    const char *blk_end;
    epg_channel_t *c;
    char sid[EPG_ID_LEN], name[EPG_ID_LEN];
    const char *np;
    if (!tag || tag >= end)
      break;
    blk_end = strstr(tag, "</ServiceInformation>");
    if (!blk_end)
      break;
    if (xml_attr(tag, blk_end, "serviceId", sid, sizeof sid) == 0) {
      c = epg_add_channel(doc);
      if (!c) {
        free(buf);
        free(ptext);
        return -1;
      }
      snprintf(c->id, sizeof c->id, "%s", sid);
      np = tag;
      for (;;) {
        const char *hit = strstr(np, "<Name>");
        if (!hit || hit >= blk_end)
          break;
        if (xml_elem_text(hit, blk_end, "Name", name, sizeof name))
          break;
        epg_channel_add_name(c, name);
        np = hit + 1;
      }
      {
        const char *u1 = strstr(tag, "<ServiceURL name=\"IPTV\">");
        if (u1 && u1 < blk_end) {
          if (xml_elem_text(u1, blk_end, "ServiceURL", c->uri, sizeof c->uri))
            c->uri[0] = '\0';
        }
      }
      {
        const char *u2 = strstr(tag, "<ServiceURL name=\"DTT\">");
        char dtt[64];
        if (u2 && u2 < blk_end && xml_elem_text(u2, blk_end, "ServiceURL", dtt, sizeof dtt) == 0) {
          unsigned onid, tsid, sid;
          if (sscanf(dtt, "dvb://%u.%u.%u", &onid, &tsid, &sid) == 3) {
            c->onid = onid;
            c->tsid = tsid;
            c->sid = sid;
          }
        }
      }
    }
    p = blk_end + 1;
  }

  p = buf;
  for (;;) {
    const char *tag = strstr(p, "<Schedule ");
    const char *blk_end;
    char channel[EPG_ID_LEN];
    const char *ep;
    if (!tag || tag >= end)
      break;
    blk_end = strstr(tag, "</Schedule>");
    if (!blk_end)
      break;
    if (xml_attr(tag, blk_end, "serviceIDRef", channel, sizeof channel) == 0) {
      ep = tag;
      for (;;) {
        const char *etag = strstr(ep, "<ScheduleEvent>");
        const char *eend;
        char crid[EPG_ID_LEN * 3 + 64];
        char start[EPG_TIME_LEN], stop[EPG_TIME_LEN];
        progtext_t *pt;
        epg_programme_t *pr;
        if (!etag || etag >= blk_end)
          break;
        eend = strstr(etag, "</ScheduleEvent>");
        if (!eend || eend > blk_end)
          break;
        if (xml_attr(etag, eend, "crid", crid, sizeof crid) == 0 &&
            xml_elem_text(etag, eend, "PublishedStartTime", start, sizeof start) == 0) {
          pr = epg_add_programme(doc);
          if (!pr) {
            free(buf);
            free(ptext);
            return -1;
          }
          snprintf(pr->channel_id, sizeof pr->channel_id, "%s", channel);
          snprintf(pr->start, sizeof pr->start, "%s", start);
          if (xml_elem_text(etag, eend, "PublishedEndTime", stop, sizeof stop) == 0)
            snprintf(pr->stop, sizeof pr->stop, "%s", stop);
          else
            pr->stop[0] = '\0';
          pt = find_progtext(ptext, ptext_n, crid);
          if (pt) {
            snprintf(pr->title, sizeof pr->title, "%s", pt->title);
            snprintf(pr->desc, sizeof pr->desc, "%s", pt->desc);
            snprintf(pr->category, sizeof pr->category, "%s", pt->category);
          }
        }
        ep = eend + 1;
      }
    }
    p = blk_end + 1;
  }

  free(buf);
  free(ptext);
  return 0;
}
