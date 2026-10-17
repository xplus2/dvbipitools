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

/* stop from PublishedEndTime, else start + PublishedDuration, else empty */
static void read_stop(const char *etag, const char *eend, const char *start, char *stop, size_t stopsz) {
  char dur[64];
  long secs;
  stop[0] = '\0';
  if (xml_elem_text(etag, eend, "PublishedEndTime", stop, stopsz) == 0) {
    trim_ws(stop);
    return;
  }
  stop[0] = '\0';
  if (xml_elem_text(etag, eend, "PublishedDuration", dur, sizeof dur) != 0) return;
  trim_ws(dur);
  if (duration_to_secs(dur, &secs) != 0 || iso8601_add_secs(start, secs, stop, stopsz) != 0) stop[0] = '\0';
}

/* processes one ScheduleEvent [etag,eend) for channel. 0 ok, -1 OOM */
static int parse_schedule_event(bcg_doc_t *doc, const progtext_list_t *pl, const char *channel, const char *etag, const char *eend) {
  char crid[BCG_ID_LEN * 3 + 64];
  char start[BCG_TIME_LEN], stop[BCG_TIME_LEN];
  const bcg_progtext_t *pt;
  bcg_programme_t *pr;

  if (bcg_attr(etag, eend, "crid", crid, sizeof crid) != 0 || xml_elem_text(etag, eend, "PublishedStartTime", start, sizeof start) != 0)
    return 0;
  trim_ws(start);
  pr = bcg_add_programme(doc);
  if (!pr) return -1;
  bufcpy(pr->channel_id, sizeof pr->channel_id, channel);
  bufcpy(pr->start, sizeof pr->start, start);
  read_stop(etag, eend, start, stop, sizeof stop);
  bufcpy(pr->stop, sizeof pr->stop, stop);
  pt = find_progtext(pl, crid);
  if (pt && (bcg_set_text(&pr->title, pt->title) || bcg_set_text(&pr->desc, pt->desc) || bcg_set_text(&pr->category, pt->category)))
    return -1;
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
  return for_each_xml_elem(tag, blk_end, "ScheduleEvent", schedule_event_cb, &ctx);
}

typedef struct {
  bcg_doc_t *doc;
  const progtext_list_t *pl;
} schedule_ctx_t;

static int schedule_cb(const char *tag, const char *blk_end, void *vctx) {
  schedule_ctx_t *ctx = vctx;
  char channel[BCG_ID_LEN];
  if (bcg_tag_attr(tag, blk_end, "serviceIDRef", channel, sizeof channel) == 0 &&
      parse_schedule_events(ctx->doc, ctx->pl, channel, tag, blk_end) != 0)
    return -1;
  return 0;
}

/* scan Schedule blocks, adding programmes to doc. 0 ok, -1 OOM */
static int parse_schedule(const char *buf, const char *end, bcg_doc_t *doc, const progtext_list_t *pl) {
  schedule_ctx_t ctx = {doc, pl};
  return for_each_xml_elem(buf, end, "Schedule", schedule_cb, &ctx);
}

int tva_xml_read(FILE *f, bcg_doc_t *doc) {
  char *buf;
  size_t len;
  const char *end;
  progtext_list_t pl = {0};
  int rc;

  if (read_all(f, &buf, &len)) {
    log_code("LE500230", "tva: cannot read input (read error or out of memory)");
    return -1;
  }
  end = buf + len;
  rc = parse_program_texts(buf, end, &pl) == 0 && progtext_list_build_index(&pl) == 0 && parse_service_information(buf, end, doc) == 0 &&
               parse_schedule(buf, end, doc, &pl) == 0 ? 0 : -1;
  if (rc) log_code("LE500231", "tva: out of memory parsing input");

  free(buf);
  for (int i = 0; i < pl.n; i++) bcg_progtext_free(&pl.items[i]);
  free(pl.items);
  free(pl.idx);
  return rc;
}
