/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/bcgfrag.h"
#include "lib/sys/ioutil.h"
#include "lib/tva/tva_xml.h"

#define XML_NS "http://www.w3.org/XML/1998/namespace"

static const bim_sbc_t *sbc_elem(const bim_type_t *t, const char *name) {
  for (int i = 0; t && i < t->nsbc; i++)
    if (t->sbc[i].kind == BIM_SBC_ELEM && !strcmp(t->sbc[i].name, name)) return &t->sbc[i];
  return NULL;
}

/* sets ns of descendants of n from SBC of t. -1 name not in schema */
static int bind(bim_node_t *n, const bim_type_t *t) {
  for (int i = 0; i < n->nkids; i++) {
    bim_node_t *k = n->kids[i];
    const bim_sbc_t *s;
    if (!k->name) continue;
    if (!(s = sbc_elem(t, k->name))) return -1;
    k->ns = s->ns;
    if (k->nkids && bind(k, s->type)) return -1;
  }
  return 0;
}

static bim_node_t *root_of(const bcgau_t *a, unsigned code, bim_tvaroot_t *r) {
  bim_node_t *n;
  if (bim_tvafrag_root(a->c, &a->ns, code, r) || !(n = bim_node_new(r->name))) return NULL;
  n->ns = r->ns;
  return n;
}

static bim_node_t *finish(bim_node_t *root, const bim_tvaroot_t *r) {
  if (root && bind(root, r->type)) {
    bim_node_free(root);
    return NULL;
  }
  return root;
}

bim_node_t *bcgfrag_program(const bcgau_t *a, const bcg_programme_t *pr) {
  char buf[TVA_CRID_BUF];
  bim_tvaroot_t r;
  bim_node_t *root = root_of(a, 0x0001, &r);
  bim_node_t *bd;
  if (!root || bim_node_set_attr(root, "programId", tva_programme_crid(pr, buf, sizeof buf))) goto fail;
  bd = bim_node_add_new(root, "BasicDescription", NULL);
  if (!bd) goto fail;
  if (pr->title && !bim_node_add_new(bd, "Title", pr->title)) goto fail;
  if (pr->desc && !bim_node_add_new(bd, "Synopsis", pr->desc)) goto fail;
  if (pr->category) {
    bim_node_t *g = bim_node_add_new(bd, "Genre", NULL);
    if (!g || bim_node_set_attr(g, "href", tva_genre_href(pr->category)) || !bim_node_add_new(g, "Name", pr->category)) goto fail;
  }
  return finish(root, &r);
fail:
  bim_node_free(root);
  return NULL;
}

typedef struct {
  long long start;
  int idx;
} sched_ord_t;

static int sched_ord_cmp(const void *a, const void *b) {
  const sched_ord_t *x = a;
  const sched_ord_t *y = b;
  if (x->start != y->start) return (x->start > y->start) - (x->start < y->start);
  return (x->idx > y->idx) - (x->idx < y->idx);
}

static const char *event_end(const bcg_programme_t *pr) {
  long secs;
  return pr->stop[0] && !iso8601_diff_secs(pr->start, pr->stop, &secs) && secs >= 0 ? pr->stop : pr->start;
}

const char *bcgau_schedule_end(const bcg_programme_t *programmes, int first, const int *next) {
  const char *end = NULL;
  long long end_s = 0;
  for (int j = first; j != -1; j = next[j]) {
    const char *e = event_end(&programmes[j]);
    long long s;
    if (!iso8601_utc_secs(e, &s) && (!end || s > end_s)) {
      end = e;
      end_s = s;
    }
  }
  return end;
}

/* TS 102 323 8.5: chronological events, PublishedDuration instead of PublishedEndTime, Schedule start/end span all */
static bim_node_t *schedule_tree(bim_node_t *root, const char *channel_id, const bcg_programme_t *programmes, const sched_ord_t *ord, int n, const char *end, int titles) {
  if (!root || bim_node_set_attr(root, "serviceIDRef", channel_id)) goto fail;
  for (int k = 0; k < n; k++) {
    const bcg_programme_t *pr = &programmes[ord[k].idx];
    char buf[TVA_CRID_BUF];
    char dur[40];
    long secs;
    bim_node_t *ev = bim_node_add_new(root, "ScheduleEvent", NULL);
    bim_node_t *p = ev ? bim_node_add_new(ev, "Program", NULL) : NULL;
    if (!p || bim_node_set_attr(p, "crid", tva_programme_crid(pr, buf, sizeof buf))) goto fail;
    if (titles && pr->title) {
      bim_node_t *id = bim_node_add_new(ev, "InstanceDescription", NULL);
      if (!id || !bim_node_add_new(id, "Title", pr->title)) goto fail;
    }
    if (!bim_node_add_new(ev, "PublishedStartTime", pr->start)) goto fail;
    if (event_end(pr) != pr->start && !iso8601_diff_secs(pr->start, pr->stop, &secs) && !secs_to_duration(secs, dur, sizeof dur) &&
        !bim_node_add_new(ev, "PublishedDuration", dur))
      goto fail;
  }
  if (n && ord[0].start != LLONG_MIN && bim_node_set_attr(root, "start", programmes[ord[0].idx].start)) goto fail;
  if (end && bim_node_set_attr(root, "end", end)) goto fail;
  return root;
fail:
  bim_node_free(root);
  return NULL;
}

bim_node_t *bcgfrag_schedule(const bcgau_t *a, const char *channel_id, const bcg_programme_t *programmes, int first, const int *next, int titles) {
  bim_tvaroot_t r;
  sched_ord_t *ord;
  bim_node_t *root;
  int n = 0;
  for (int j = first; j != -1; j = next[j]) n++;
  ord = malloc(sizeof *ord * (size_t)(n ? n : 1));
  if (!ord) return NULL;
  n = 0;
  for (int j = first; j != -1; j = next[j]) {
    ord[n].idx = j;
    if (iso8601_utc_secs(programmes[j].start, &ord[n].start)) ord[n].start = LLONG_MIN;
    n++;
  }
  qsort(ord, (size_t)n, sizeof *ord, sched_ord_cmp);
  root = finish(schedule_tree(root_of(a, 0x0005, &r), channel_id, programmes, ord, n, bcgau_schedule_end(programmes, first, next), titles), &r);
  free(ord);
  return root;
}

bim_node_t *bcgfrag_service(const bcgau_t *a, const bcg_channel_t *c) {
  bim_tvaroot_t r;
  bim_node_t *root = root_of(a, 0x0006, &r);
  if (!root || bim_node_set_attr(root, "serviceId", c->id)) goto fail;
  if (c->name_count == 0 && !bim_node_add_new(root, "Name", c->id)) goto fail;
  for (int j = 0; j < c->name_count; j++)
    if (!bim_node_add_new(root, "Name", c->names[j])) goto fail;
  if (!bim_node_add_new(root, "ServiceURL", c->uri)) goto fail;
  return finish(root, &r);
fail:
  bim_node_free(root);
  return NULL;
}

static int encodes(const bcgau_t *a, const bim_type_t *t, const bim_node_t *n) {
  bitwriter_t bw;
  strrepo_writer_t sw;
  int ok;
  bitwriter_init(&bw);
  ok = !strrepo_writer_init(&sw);
  if (ok) {
    ok = !bim_encode_payload_opts(t, n, &a->eo, &bw, &sw);
    strrepo_writer_free(&sw);
  }
  bitwriter_free(&bw);
  return ok;
}

/* adds empty s if the schema allows it, ProgramDescription always. 0 ok or absent, -1 oom */
static int add_table(const bcgau_t *a, bim_node_t *parent, const bim_type_t *t, const char *name, bim_node_t **out) {
  const bim_sbc_t *s = sbc_elem(t, name);
  *out = NULL;
  if (!s) return 0;
  if (strcmp(name, "ProgramDescription")) {
    bim_node_t *probe = bim_node_new(s->name);
    int ok;
    if (!probe) return -1;
    probe->ns = s->ns;
    ok = encodes(a, s->type, probe);
    bim_node_free(probe);
    if (!ok) return 0;
  }
  *out = bim_node_add_new(parent, name, NULL);
  return *out ? 0 : -1;
}

bim_node_t *bcgfrag_tva_main(const bcgau_t *a, const char *publisher, const char *lang) {
  static const char *const tables[] = {"ProgramInformationTable", "GroupInformationTable", "ProgramLocationTable", "ServiceInformationTable",
                                       "CreditsInformationTable", "ProgramReviewTable", "SegmentInformationTable", "PurchaseInformationTable"};
  bim_tvaroot_t r;
  bim_node_t *root = root_of(a, 0x000E, &r);
  const bim_sbc_t *pds = root ? sbc_elem(r.type, "ProgramDescription") : NULL;
  bim_node_t *k;
  bim_node_t *pd;
  if (!root || (publisher && bim_node_set_attr(root, "publisher", publisher))) goto fail;
  if (lang) {
    char *v = strdup(lang);
    if (!v || bim_node_add_attr_ns(root, XML_NS, "lang", v)) goto fail;
  }
  if (add_table(a, root, r.type, "ClassificationSchemeTable", &k) || add_table(a, root, r.type, "ProgramDescription", &pd)) goto fail;
  for (size_t i = 0; pd && pds && i < sizeof tables / sizeof tables[0]; i++) {
    const bim_sbc_t *ts = sbc_elem(pds->type, tables[i]);
    if (add_table(a, pd, pds->type, tables[i], &k)) goto fail;
    if (!k && ts && !strcmp(tables[i], "SegmentInformationTable")) {
      bim_node_t *sl;
      k = bim_node_add_new(pd, tables[i], NULL);
      if (!k || add_table(a, k, ts->type, "SegmentList", &sl) || add_table(a, k, ts->type, "SegmentGroupList", &sl)) goto fail;
      k->ns = ts->ns;
      if (bind(k, ts->type) || !encodes(a, ts->type, k)) bim_node_free(pd->kids[--pd->nkids]);
    }
  }
  return finish(root, &r);
fail:
  bim_node_free(root);
  return NULL;
}

int bcgfrag_read_program(const bim_node_t *root, char *crid_out, size_t crid_cap, bcg_programme_t *pr_out) {
  const bim_node_t *bd;
  const char *id;
  memset(pr_out, 0, sizeof *pr_out);
  id = bim_node_attr(root, "programId");
  bd = bim_node_child(root, "BasicDescription");
  if (!id || !bd || strlen(id) >= crid_cap) return -1;
  memcpy(crid_out, id, strlen(id) + 1);
  for (int i = 0; i < bd->nkids; i++) {
    const bim_node_t *k = bd->kids[i];
    if (!k->name) continue;
    if (!strcmp(k->name, "Title") && !pr_out->title) {
      if (bcg_set_text(&pr_out->title, k->text)) goto fail;
    } else if (!strcmp(k->name, "Synopsis") && !pr_out->desc) {
      if (bcg_set_text(&pr_out->desc, k->text)) goto fail;
    } else if (!strcmp(k->name, "Genre") && !pr_out->category) {
      const bim_node_t *name = bim_node_child(k, "Name");
      if (name && bcg_set_text(&pr_out->category, name->text)) goto fail;
    }
  }
  return 0;
fail:
  bcg_programme_free_text(pr_out);
  return -1;
}

int bcgfrag_read_schedule(const bim_node_t *root, bcg_doc_t *doc, bcgfrag_text_lookup_fn lookup, void *ctx) {
  const char *channel = bim_node_attr(root, "serviceIDRef");
  if (!channel || strlen(channel) >= BCG_ID_LEN) return -1;
  for (int i = 0; i < root->nkids; i++) {
    const bim_node_t *ev = root->kids[i];
    const bim_node_t *prog = ev->name ? bim_node_child(ev, "Program") : NULL;
    const bim_node_t *start = ev->name ? bim_node_child(ev, "PublishedStartTime") : NULL;
    const bim_node_t *stop = ev->name ? bim_node_child(ev, "PublishedEndTime") : NULL;
    const bim_node_t *dur = ev->name ? bim_node_child(ev, "PublishedDuration") : NULL;
    const char *crid = prog ? bim_node_attr(prog, "crid") : NULL;
    bcg_programme_t *pr;
    if (!crid || !start || !start->text || strlen(start->text) >= BCG_TIME_LEN) continue;
    pr = bcg_add_programme(doc);
    if (!pr) return -1;
    bufcpy(pr->channel_id, sizeof pr->channel_id, channel);
    bufcpy(pr->start, sizeof pr->start, start->text);
    if (stop && stop->text && strlen(stop->text) < BCG_TIME_LEN) {
      bufcpy(pr->stop, sizeof pr->stop, stop->text);
    } else if (dur && dur->text) {
      long secs;
      if (duration_to_secs(dur->text, &secs) || iso8601_add_secs(pr->start, secs, pr->stop, sizeof pr->stop)) pr->stop[0] = '\0';
    }
    if (lookup && lookup(ctx, crid, pr) == -2) return -1;
  }
  return 0;
}

int bcgfrag_read_service(const bim_node_t *root, const char *dtt, int no_names, bcg_channel_t *c_out) {
  const char *id = bim_node_attr(root, "serviceId");
  const bim_node_t *url;
  int dropped = 0;
  memset(c_out, 0, sizeof *c_out);
  if (!id || strlen(id) >= sizeof c_out->id) return -1;
  memcpy(c_out->id, id, strlen(id) + 1);
  url = bim_node_child(root, "ServiceURL");
  if (url && url->text && strlen(url->text) < sizeof c_out->uri) memcpy(c_out->uri, url->text, strlen(url->text) + 1);
  if (dtt && *dtt) bcg_channel_dtt_parse(c_out, dtt);
  for (int i = 0; !no_names && i < root->nkids; i++) {
    if (!root->kids[i]->name || strcmp(root->kids[i]->name, "Name") || !root->kids[i]->text) continue;
    if (strlen(root->kids[i]->text) >= BCG_ID_LEN || bcg_channel_add_name(c_out, root->kids[i]->text)) dropped++;
  }
  if (dropped) log_code("LW400234", "bcg: channel %s: %d <Name> dropped (max %d names, %d bytes each)", c_out->id, dropped, BCG_MAX_NAMES, BCG_ID_LEN - 1);
  return 0;
}

int bcgfrag_read_tva_main(const bim_node_t *root, bcg_doc_t *doc) {
  const char *pub = bim_node_attr(root, "publisher");
  const char *lang = bim_node_attr_ns(root, XML_NS, "lang");
  if ((pub && strlen(pub) >= sizeof doc->publisher) || (lang && strlen(lang) >= sizeof doc->lang)) return -1;
  bufcpy(doc->publisher, sizeof doc->publisher, pub ? pub : "");
  bufcpy(doc->lang, sizeof doc->lang, lang ? lang : "");
  return 0;
}
