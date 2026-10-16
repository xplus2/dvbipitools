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
#include "tva_xml.h"

#define CONTENTCS_2011 "urn:tva:metadata:cs:ContentCS:2011:"

typedef struct {
  const char *name;
  const char *href;
} genre_map_t;

static const genre_map_t genre_map[] = {
{"news", CONTENTCS_2011 "3.1.1"},
{"nachrichten", CONTENTCS_2011 "3.1.1"},
{"religion", CONTENTCS_2011 "3.1.2"},
{"documentary", CONTENTCS_2011 "3.1.3"},
{"dokumentation", CONTENTCS_2011 "3.1.3"},
{"arts", CONTENTCS_2011 "3.1.4"},
{"culture", CONTENTCS_2011 "3.1.4"},
{"science", CONTENTCS_2011 "3.1.6"},
{"information", CONTENTCS_2011 "3.1"},
{"non-fiction", CONTENTCS_2011 "3.1"},
{"sports", CONTENTCS_2011 "3.2"},
{"sport", CONTENTCS_2011 "3.2"},
{"fiction", CONTENTCS_2011 "3.4"},
{"drama", CONTENTCS_2011 "3.4"},
{"movie", CONTENTCS_2011 "3.4"},
{"film", CONTENTCS_2011 "3.4"},
{"series", CONTENTCS_2011 "3.4"},
{"soap", CONTENTCS_2011 "3.4.2"},
{"romance", CONTENTCS_2011 "3.4.3"},
{"action", CONTENTCS_2011 "3.4.6"},
{"fantasy", CONTENTCS_2011 "3.4.7"},
{"musical", CONTENTCS_2011 "3.4.10"},
{"entertainment", CONTENTCS_2011 "3.5"},
{"quiz", CONTENTCS_2011 "3.5.2"},
{"game show", CONTENTCS_2011 "3.5.2"},
{"variety", CONTENTCS_2011 "3.5.3"},
{"reality", CONTENTCS_2011 "3.5.5"},
{"comedy", CONTENTCS_2011 "3.5.7"},
{"talk", CONTENTCS_2011 "3.5.16"},
{"music", CONTENTCS_2011 "3.6"},
{"classical music", CONTENTCS_2011 "3.6.1"},
{"jazz", CONTENTCS_2011 "3.6.2"},
{"pop", CONTENTCS_2011 "3.6.4"},
{"rock", CONTENTCS_2011 "3.6.4"},
{"games", CONTENTCS_2011 "3.7"},
{"video games", CONTENTCS_2011 "3.7.1"},
{"lifestyle", CONTENTCS_2011 "3.8"},
{"leisure", CONTENTCS_2011 "3.8"},
{"technology", CONTENTCS_2011 "3.8.2"},
{"cooking", CONTENTCS_2011 "3.8.3"},
{"food", CONTENTCS_2011 "3.8.3"},
{"travel", CONTENTCS_2011 "3.8.9"},
{"adult", CONTENTCS_2011 "3.9"},
};

const char *tva_genre_href(const char *category) {
  for (size_t i = 0; i < sizeof genre_map / sizeof genre_map[0]; i++)
    if (!strcasecmp(category, genre_map[i].name)) return genre_map[i].href;
  return CONTENTCS_2011 "3.0";
}

/* "YYYY-MM-DDTHH:MM:SS..." -> "YYYYMMDDHHMMSS", truncates rest */
static void iso8601_compact_prefix(const char *iso, char *out, size_t outcap) {
  size_t oi = 0;
  for (size_t i = 0; iso[i] && oi + 1 < outcap; i++) {
    if (iso[i] != '-' && iso[i] != ':' && iso[i] != 'T') out[oi++] = iso[i];
  }
  out[oi] = '\0';
}

static long days_from_civil(long y, int m, int d) {
  long era;
  long yoe;
  long doy;
  long doe;
  y -= m <= 2;
  era = (y >= 0 ? y : y - 399) / 400;
  yoe = y - era * 400;
  doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void civil_from_days(long z, int *y, int *m, int *d) {
  long era;
  long doe;
  long yoe;
  long doy;
  long mp;
  long yy;
  z += 719468;
  era = (z >= 0 ? z : z - 146096) / 146097;
  doe = z - era * 146097;
  yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  yy = yoe + era * 400;
  doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  mp = (5 * doy + 2) / 153;
  *d = (int)(doy - (153 * mp + 2) / 5 + 1);
  *m = (int)(mp < 10 ? mp + 3 : mp - 9);
  *y = (int)(yy + (*m <= 2));
}

/* "YYYYMMDDHHMMSS" in UTC: offset applied so equal instants give equal ids. 0 ok, -1 unparseable */
static int iso8601_utc_compact(const char *iso, char *out, size_t outcap) {
  iso8601_t t;
  long mins;
  long days;
  int y;
  int mo;
  int d;
  int rem;
  if (iso8601_split(iso, &t) != 0) return -1;
  mins = days_from_civil(t.y, t.mo, t.d) * 1440L + t.h * 60 + t.mi;
  if (t.offset_kind == ISO8601_OFF_NUMERIC) mins -= t.off_min;
  days = mins >= 0 ? mins / 1440 : -((-mins + 1439) / 1440);
  rem = (int)(mins - days * 1440);
  civil_from_days(days, &y, &mo, &d);
  snprintf(out, outcap, "%04d%02d%02d%02d%02d%02d", y, mo, d, rem / 60, rem % 60, t.s);
  return 0;
}

void tva_build_crid(const char *channel_id, const char *start_iso, char *out, size_t outcap) {
  char enc[BCG_ID_LEN * 3];
  char ts[BCG_TIME_LEN];
  pct_encode_seg(channel_id, enc, sizeof enc);
  if (iso8601_utc_compact(start_iso, ts, sizeof ts) != 0) {
    iso8601_compact_prefix(start_iso, ts, sizeof ts);
    if (strlen(ts) >= 14) ts[14] = '\0';
  }
  snprintf(out, outcap, "crid://dipixmltv.invalid/%s/%s", enc, ts);
}

int tva_programme_cache_crid(bcg_programme_t *pr) {
  char crid[TVA_CRID_BUF];
  char *dup;
  tva_build_crid(pr->channel_id, pr->start, crid, sizeof crid);
  dup = strdup(crid);
  if (!dup) return -1;
  free(pr->crid);
  pr->crid = dup;
  return 0;
}

const char *tva_programme_crid(const bcg_programme_t *pr, char *buf, size_t cap) {
  if (pr->crid) return pr->crid;
  tva_build_crid(pr->channel_id, pr->start, buf, cap);
  return buf;
}

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

static bcg_progtext_t *find_progtext(const progtext_list_t *pl, const char *crid) {
  int lo = 0, hi = pl->n - 1;
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
static int parse_program_texts(const char *buf, const char *end, progtext_list_t *pl) {
  return for_each_xml_elem(buf, end, "ProgramInformation", program_text_cb, pl);
}

typedef struct {
  bcg_channel_t *c;
  const char *svc, *svc_end;
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
  unsigned onid, tsid, sid;
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
static int parse_service_information(const char *buf, const char *end, bcg_doc_t *doc) {
  return for_each_xml_elem(buf, end, "ServiceInformation", service_info_cb, doc);
}

/* strips surrounding XML whitespace in place */
static void trim_ws(char *s) {
  size_t n = strlen(s);
  size_t a = 0;
  while (n && isspace((unsigned char)s[n - 1])) n--;
  while (a < n && isspace((unsigned char)s[a])) a++;
  memmove(s, s + a, n - a);
  s[n - a] = '\0';
}

/* xs:duration "[-]PnDTnHnMn[.f]S" to seconds. years/months/negative/empty rejected. 0 ok, -1 bad */
static int duration_to_secs(const char *s, long *out) {
  long total = 0;
  long n;
  int in_time = 0;
  int any = 0;
  if (*s++ != 'P') return -1;
  while (*s) {
    if (*s == 'T' && !in_time) {
      in_time = 1;
      s++;
      continue;
    }
    if (!isdigit((unsigned char)*s)) return -1;
    for (n = 0; isdigit((unsigned char)*s); s++) {
      n = n * 10 + (*s - '0');
      if (n > 100000000L) return -1;
    }
    if (in_time && *s == '.' && isdigit((unsigned char)s[1])) {
      s++;
      while (isdigit((unsigned char)*s)) s++;
    }
    if (*s == 'D' && !in_time) n *= 86400;
    else if (*s == 'H' && in_time) n *= 3600;
    else if (*s == 'M' && in_time) n *= 60;
    else if (*s != 'S' || !in_time) return -1;
    total += n;
    any = 1;
    s++;
  }
  if (!any) return -1;
  *out = total;
  return 0;
}

/* start_iso + secs, same offset notation, fraction dropped. 0 ok, -1 bad start */
static int iso8601_add_secs(const char *start_iso, long secs, char *out, size_t outcap) {
  iso8601_t t;
  long tot;
  int y;
  int mo;
  int d;
  int n;
  if (iso8601_split(start_iso, &t) != 0) return -1;
  tot = days_from_civil(t.y, t.mo, t.d) * 86400L + t.h * 3600L + t.mi * 60L + t.s + secs;
  civil_from_days(tot / 86400, &y, &mo, &d);
  tot %= 86400;
  n = snprintf(out, outcap, "%04d-%02d-%02dT%02d:%02d:%02d", y, mo, d, (int)(tot / 3600), (int)(tot % 3600 / 60), (int)(tot % 60));
  if (n < 0 || (size_t)n >= outcap) return -1;
  if (t.offset_kind == ISO8601_OFF_Z) {
    snprintf(out + n, outcap - (size_t)n, "Z");
  } else if (t.offset_kind == ISO8601_OFF_NUMERIC) {
    int a = t.off_min < 0 ? -t.off_min : t.off_min;
    snprintf(out + n, outcap - (size_t)n, "%c%02d:%02d", t.off_min < 0 ? '-' : '+', a / 60, a % 60);
  }
  return 0;
}

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
