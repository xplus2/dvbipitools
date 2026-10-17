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

static int progtext_idx_cmp(const void *a, const void *b) {
  return strcmp(((const progtext_idx_t *)a)->crid, ((const progtext_idx_t *)b)->crid);
}

/* 0 ok, -1 OOM. pl->items must not change after this: idx entries are built from it */
int progtext_list_build_index(progtext_list_t *pl) {
  if (!pl->n) return 0;
  pl->idx = malloc(sizeof *pl->idx * (size_t)pl->n);
  if (!pl->idx) return -1;
  for (int i = 0; i < pl->n; i++) {
    pl->idx[i].crid = pl->items[i].crid;
    pl->idx[i].idx = i;
  }
  qsort(pl->idx, (size_t)pl->n, sizeof *pl->idx, progtext_idx_cmp);
  return 0;
}

bcg_progtext_t *find_progtext(const progtext_list_t *pl, const char *crid) {
  int lo = 0;
  int hi = pl->n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(crid, pl->idx[mid].crid);
    if (c == 0) return &pl->items[pl->idx[mid].idx];
    if (c < 0) hi = mid - 1;
    else       lo = mid + 1;
  }
  return NULL;
}

/* grows pl->items to fit one more entry if needed. 0 ok, -1 OOM */
static int progtext_grow(progtext_list_t *pl) {
  void *np;
  if (pl->n < pl->cap) return 0;
  np = array_grow(pl->items, &pl->cap, pl->n + 1, sizeof *pl->items);
  if (!np) return -1;
  pl->items = np;
  return 0;
}

static int program_text_cb(const char *tag, const char *blk_end, void *ctx) {
  progtext_list_t *pl = ctx;
  char crid[BCG_ID_LEN * 3 + 64];
  bcg_progtext_t *pt;
  xml_span_t genre;
  if (bcg_tag_attr(tag, blk_end, "programId", crid, sizeof crid) == 0) {
    if (progtext_grow(pl) != 0) return -1;
    pt = &pl->items[pl->n++];
    memset(pt, 0, sizeof *pt);
    pt->crid = strdup(crid);
    if (!pt->crid || bcg_elem_text(tag, blk_end, "Title", &pt->title) || bcg_elem_text(tag, blk_end, "Synopsis", &pt->desc))
      return -1;
    if (xml_find_elem(tag, blk_end, "Genre", &genre) == 0 && bcg_elem_text(genre.tag, genre.end, "Name", &pt->category)) return -1;
  }
  return 0;
}

/* scan ProgramInformation blocks into pl (growing it as needed). 0 ok, -1 OOM */
int parse_program_texts(const char *buf, const char *end, progtext_list_t *pl) {
  return for_each_xml_elem(buf, end, "ProgramInformation", program_text_cb, pl);
}

typedef struct {
  bcg_channel_t *c;
  const char *svc;
  const char *svc_end;
  const char *pos;
  int inside;
} service_name_ctx_t;

static int in_genre_cb(const char *tag, const char *blk_end, void *ctx) {
  service_name_ctx_t *n = ctx;
  if (n->pos > tag && n->pos < blk_end) n->inside = 1;
  return 0;
}

static int service_name_cb(const char *tag, const char *blk_end, void *ctx) {
  service_name_ctx_t *n = ctx;
  bcg_channel_t *c = n->c;
  char name[BCG_ID_LEN];
  int cut = 0;
  n->pos = tag;
  n->inside = 0;
  for_each_xml_elem(n->svc, n->svc_end, "ServiceGenre", in_genre_cb, n);
  if (n->inside) return 0;
  if (xml_span_text_chk(tag, blk_end, name, sizeof name, &cut) == 0) {
    if (cut) log_code("LW400186", "bcg: <Name> text truncated to %zu bytes", strlen(name));
    bcg_channel_add_name(c, name);
  }
  return 0;
}

typedef struct {
  bcg_channel_t *c;
  int have_iptv;
  int have_dtt;
} service_url_ctx_t;

static int service_url_cb(const char *tag, const char *blk_end, void *vctx) {
  service_url_ctx_t *u = vctx;
  char kind[16];
  char text[sizeof u->c->uri];
  unsigned onid;
  unsigned tsid;
  unsigned sid;
  int cut = 0;

  if (xml_tag_attr(tag, blk_end, "name", kind, sizeof kind) || xml_span_text_chk(tag, blk_end, text, sizeof text, &cut)) return 0;
  if (!u->have_iptv && !strcmp(kind, "IPTV")) {
    u->have_iptv = 1;
    if (cut) log_code("LW400187", "bcg: <ServiceURL> text truncated to %zu bytes", strlen(text));
    bufcpy(u->c->uri, sizeof u->c->uri, text);
  } else if (!u->have_dtt && !strcmp(kind, "DTT")) {
    u->have_dtt = 1;
    if (sscanf(text, "dvb://%x.%x.%x", &onid, &tsid, &sid) == 3) {
      u->c->onid = onid;
      u->c->tsid = tsid;
      u->c->sid = sid;
    }
  }
  return 0;
}

static int service_info_cb(const char *tag, const char *blk_end, void *ctx) {
  bcg_doc_t *doc = ctx;
  bcg_channel_t *c;
  char sid[BCG_ID_LEN];
  service_url_ctx_t uctx = {0};
  service_name_ctx_t nctx = {0};
  if (bcg_tag_attr(tag, blk_end, "serviceId", sid, sizeof sid) == 0) {
    c = bcg_add_channel(doc);
    if (!c) return -1;
    bufcpy(c->id, sizeof c->id, sid);
    uctx.c = c;
    nctx.c = c;
    nctx.svc = tag;
    nctx.svc_end = blk_end;
    for_each_xml_elem(tag, blk_end, "Name", service_name_cb, &nctx);
    for_each_xml_elem(tag, blk_end, "ServiceURL", service_url_cb, &uctx);
  }
  return 0;
}

/* scan ServiceInformation blocks into doc's channels. 0 ok, -1 OOM */
int parse_service_information(const char *buf, const char *end, bcg_doc_t *doc) {
  return for_each_xml_elem(buf, end, "ServiceInformation", service_info_cb, doc);
}
