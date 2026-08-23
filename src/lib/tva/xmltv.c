/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/xml_util.h"
#include "lib/ioutil.h"
#include "timefmt.h"
#include "xmltv.h"

static void scan_display_names(const char *s, const char *end, bcg_channel_t *c) {
  const char *p = s;
  for (;;) {
    char name[BCG_ID_LEN];
    const char *hit = strstr(p, "<display-name");
    if (!hit || hit >= end)
      break;
    if (xml_elem_text(hit, end, "display-name", name, sizeof name))
      break;
    bcg_channel_add_name(c, name);
    p = hit + 1;
  }
}

/* fills remaining programme fields (stop/title/desc/category) once start parsed ok */
static void fill_programme_details(bcg_programme_t *pr, const char *tag, const char *blk_end) {
  char stop[BCG_TIME_LEN];
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

static int channel_cb(const char *tag, const char *blk_end, void *ctx) {
  bcg_doc_t *doc = ctx;
  bcg_channel_t *c;
  char id[BCG_ID_LEN];
  if (xml_attr(tag, blk_end, "id", id, sizeof id) == 0) {
    c = bcg_add_channel(doc);
    if (!c)
      return -1;
    bufcpy(c->id, sizeof c->id, id);
    scan_display_names(tag, blk_end, c);
  }
  return 0;
}

static int programme_cb(const char *tag, const char *blk_end, void *ctx) {
  bcg_doc_t *doc = ctx;
  bcg_programme_t *pr;
  char start[BCG_TIME_LEN], channel[BCG_ID_LEN];
  if (xml_attr(tag, blk_end, "start", start, sizeof start) == 0 && xml_attr(tag, blk_end, "channel", channel, sizeof channel) == 0) {
    pr = bcg_add_programme(doc);
    if (!pr)
      return -1;
    bufcpy(pr->channel_id, sizeof pr->channel_id, channel);
    if (xmltv_time_to_iso8601(start, pr->start, sizeof pr->start)) {
      fprintf(stderr, "xmltv: skipping programme, bad start time: %s\n", start);
      doc->programme_count--;
    } else {
      fill_programme_details(pr, tag, blk_end);
    }
  }
  return 0;
}

int xmltv_read(FILE *f, bcg_doc_t *doc) {
  char *buf;
  size_t len;
  const char *end;
  int rc;

  if (read_all(f, &buf, &len)) {
    fprintf(stderr, "xmltv: out of memory reading xmltv\n");
    return -1;
  }
  end = buf + len;

  rc = for_each_xml_block(buf, end, "<channel", "</channel>", channel_cb, doc);
  if (rc == 0)
    rc = for_each_xml_block(buf, end, "<programme", "</programme>", programme_cb, doc);

  free(buf);
  return rc;
}

void xmltv_write(FILE *f, const bcg_doc_t *doc, const char *generator_name) {
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n<tv generator-info-name=\"", f);
  xml_escape(f, generator_name);
  fputs("\">\n", f);
  for (int i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    fputs("  <channel id=\"", f);
    xml_escape(f, c->id);
    fputs("\">\n", f);
    if (c->name_count == 0) {
      fputs("    <display-name>", f);
      xml_escape(f, c->id);
      fputs("</display-name>\n", f);
    }
    for (int j = 0; j < c->name_count; j++) {
      fputs("    <display-name>", f);
      xml_escape(f, c->names[j]);
      fputs("</display-name>\n", f);
    }
    fputs("  </channel>\n", f);
  }
  for (int i = 0; i < doc->programme_count; i++) {
    const bcg_programme_t *pr = &doc->programmes[i];
    char start[BCG_TIME_LEN], stop[BCG_TIME_LEN];
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
