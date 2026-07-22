/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/xml_util.h"
#include "lib/ioutil.h"
#include "timefmt.h"
#include "xmltv.h"

static void scan_display_names(const char *s, const char *end, epg_channel_t *c) {
  const char *p = s;
  for (;;) {
    char name[EPG_ID_LEN];
    const char *hit = strstr(p, "<display-name");
    if (!hit || hit >= end)
      break;
    if (xml_elem_text(hit, end, "display-name", name, sizeof name))
      break;
    epg_channel_add_name(c, name);
    p = hit + 1;
  }
}

int xmltv_read(FILE *f, epg_doc_t *doc) {
  char *buf;
  size_t len;
  const char *p, *end;

  if (read_all(f, &buf, &len)) {
    fprintf(stderr, "xmltv: out of memory reading xmltv\n");
    return -1;
  }
  end = buf + len;
  p = buf;

  for (;;) {
    const char *tag = strstr(p, "<channel");
    const char *blk_end;
    epg_channel_t *c;
    char id[EPG_ID_LEN];
    if (!tag || tag >= end)
      break;
    blk_end = strstr(tag, "</channel>");
    if (!blk_end)
      break;
    if (xml_attr(tag, blk_end, "id", id, sizeof id) == 0) {
      c = epg_add_channel(doc);
      if (!c) {
        free(buf);
        return -1;
      }
      snprintf(c->id, sizeof c->id, "%s", id);
      scan_display_names(tag, blk_end, c);
    }
    p = blk_end + 10;
  }

  p = buf;
  for (;;) {
    const char *tag = strstr(p, "<programme");
    const char *blk_end;
    epg_programme_t *pr;
    char start[EPG_TIME_LEN], stop[EPG_TIME_LEN], channel[EPG_ID_LEN];
    if (!tag || tag >= end)
      break;
    blk_end = strstr(tag, "</programme>");
    if (!blk_end)
      break;
    if (xml_attr(tag, blk_end, "start", start, sizeof start) == 0 && xml_attr(tag, blk_end, "channel", channel, sizeof channel) == 0) {
      pr = epg_add_programme(doc);
      if (!pr) {
        free(buf);
        return -1;
      }
      snprintf(pr->channel_id, sizeof pr->channel_id, "%s", channel);
      if (xmltv_time_to_iso8601(start, pr->start, sizeof pr->start)) {
        fprintf(stderr, "xmltv: skipping programme, bad start time: %s\n", start);
        doc->programme_count--;
      } else {
        pr->stop[0] = '\0';
        if (xml_attr(tag, blk_end, "stop", stop, sizeof stop) == 0 && xmltv_time_to_iso8601(stop, pr->stop, sizeof pr->stop))
          pr->stop[0] = '\0';
        if (xml_elem_text(tag, blk_end, "title", pr->title, sizeof pr->title))
          pr->title[0] = '\0';
        if (xml_elem_text(tag, blk_end, "desc", pr->desc, sizeof pr->desc))
          pr->desc[0] = '\0';
        if (xml_elem_text(tag, blk_end, "category", pr->category, sizeof pr->category))
          pr->category[0] = '\0';
      }
    }
    p = blk_end + 12;
  }

  free(buf);
  return 0;
}

void xmltv_write(FILE *f, const epg_doc_t *doc, const char *generator_name) {
  int i;
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n<tv generator-info-name=\"", f);
  xml_escape(f, generator_name);
  fputs("\">\n", f);
  for (i = 0; i < doc->channel_count; i++) {
    const epg_channel_t *c = &doc->channels[i];
    int j;
    fputs("  <channel id=\"", f);
    xml_escape(f, c->id);
    fputs("\">\n", f);
    if (c->name_count == 0) {
      fputs("    <display-name>", f);
      xml_escape(f, c->id);
      fputs("</display-name>\n", f);
    }
    for (j = 0; j < c->name_count; j++) {
      fputs("    <display-name>", f);
      xml_escape(f, c->names[j]);
      fputs("</display-name>\n", f);
    }
    fputs("  </channel>\n", f);
  }
  for (i = 0; i < doc->programme_count; i++) {
    const epg_programme_t *pr = &doc->programmes[i];
    char start[EPG_TIME_LEN], stop[EPG_TIME_LEN];
    if (iso8601_to_xmltv_time(pr->start, start, sizeof start))
      continue;
    fprintf(f, "  <programme start=\"%s\"", start);
    if (pr->stop[0] && iso8601_to_xmltv_time(pr->stop, stop, sizeof stop) == 0)
      fprintf(f, " stop=\"%s\"", stop);
    fputs(" channel=\"", f);
    xml_escape(f, pr->channel_id);
    fputs("\">\n", f);
    fputs("    <title>", f);
    xml_escape(f, pr->title[0] ? pr->title : "(untitled)");
    fputs("</title>\n", f);
    if (pr->desc[0]) {
      fputs("    <desc>", f);
      xml_escape(f, pr->desc);
      fputs("</desc>\n", f);
    }
    if (pr->category[0]) {
      fputs("    <category>", f);
      xml_escape(f, pr->category);
      fputs("</category>\n", f);
    }
    fputs("  </programme>\n", f);
  }
  fputs("</tv>\n", f);
}
