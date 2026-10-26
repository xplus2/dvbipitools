/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/dvbtva.h"
#include "lib/tva/tva_xml.h"

static int same_ns(const char *a, const char *b) { return (!a || !*a) && (!b || !*b) ? 1 : a && b && !strcmp(a, b); }

static const bim_node_t *child(const bim_tvans_t *ns, const bim_node_t *n, const char *name) {
  for (int i = 0; i < n->nkids; i++)
    if (n->kids[i]->name && !strcmp(n->kids[i]->name, name) && same_ns(n->kids[i]->ns, ns->tva)) return n->kids[i];
  return NULL;
}

static const char *attr(const bim_node_t *n, const char *name) { return bim_node_attr_ns(n, NULL, name); }

static const char *text(const bim_node_t *n) { return n && n->text ? n->text : ""; }

static int rule(bim_err_t *err, const char *what) { return bim_fail(err, BIM_DVBTVA_RULE, "bim: DVB-TVA content rule %s", what); }

static int is_crid(const char *s) { return s && !strncmp(s, "crid://", 7) && s[7]; }

/* Synopsis elements below n carry length, 8.3, 8.4 */
static int synopsis_length(const bim_tvans_t *ns, const bim_node_t *n, int only_multiple) {
  int count = 0;
  for (int i = 0; i < n->nkids; i++)
    count += n->kids[i]->name && !strcmp(n->kids[i]->name, "Synopsis") && same_ns(n->kids[i]->ns, ns->tva);
  if (only_multiple && count < 2) return 0;
  for (int i = 0; i < n->nkids; i++)
    if (n->kids[i]->name && !strcmp(n->kids[i]->name, "Synopsis") && same_ns(n->kids[i]->ns, ns->tva) && !attr(n->kids[i], "length")) return -1;
  return 0;
}

static int event_times(const bim_tvans_t *ns, const bim_node_t *ev, long long *start, long long *end) {
  const bim_node_t *s = child(ns, ev, "PublishedStartTime");
  const bim_node_t *d = child(ns, ev, "PublishedDuration");
  long dur;
  if (!s || !d || iso8601_utc_secs(text(s), start) || duration_to_secs(text(d), &dur)) return -1;
  *end = *start + dur;
  return 0;
}

static int check_schedule(const bim_tvans_t *ns, const bim_node_t *sc, bim_err_t *err) {
  long long prev = 0;
  long long first = 0;
  long long last_end = 0;
  long long v;
  int n = 0;
  for (int i = 0; i < sc->nkids; i++) {
    const bim_node_t *ev = sc->kids[i];
    long long s;
    long long e;
    const bim_node_t *p;
    if (!ev->name || strcmp(ev->name, "ScheduleEvent")) continue;
    if (child(ns, ev, "PublishedEndTime")) return rule(err, "8.5 ScheduleEvent with PublishedEndTime");
    if (event_times(ns, ev, &s, &e)) return rule(err, "8.5 ScheduleEvent without usable PublishedStartTime and PublishedDuration");
    p = child(ns, ev, "Program");
    if (!p || !is_crid(attr(p, "crid"))) return rule(err, "8.5 ScheduleEvent without Program CRID");
    if (n && s < prev) return rule(err, "8.5 ScheduleEvent not in chronological order");
    if (!n) first = s;
    prev = s;
    last_end = e > last_end ? e : last_end;
    n++;
  }
  if (!n) return 0;
  if (attr(sc, "start") && (iso8601_utc_secs(attr(sc, "start"), &v) || v > first)) return rule(err, "8.5 Schedule start after first ScheduleEvent");
  if (attr(sc, "end") && (iso8601_utc_secs(attr(sc, "end"), &v) || v < last_end)) return rule(err, "8.5 Schedule end before end of last ScheduleEvent");
  return 0;
}

static int pchar_ok(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-._~!$&'()*+,;=:@%", c);
}

/* 8.9: domain_name "/" readable_name, RFC 3986 encoded */
static int publisher_ok(const char *p) {
  const char *slash = strchr(p, '/');
  if (!slash || slash == p || !slash[1]) return 0;
  for (const char *q = p; q < slash; q++)
    if (!pchar_ok((unsigned char)*q) || *q == '%') return 0;
  for (const char *q = slash + 1; *q; q++) {
    if (!pchar_ok((unsigned char)*q)) return 0;
    if (*q == '%' && !(isxdigit((unsigned char)q[1]) && isxdigit((unsigned char)q[2]))) return 0;
  }
  return 1;
}

static int media_time_unit(const bim_node_t *n) {
  static const char *const ok[] = {"PT1001N24000F", "PT1N24F", "PT1N25F", "PT1001N30000F", "PT1N30F", "PT1N50F", "PT1001N60000F", "PT1N60F", "PT1N1000F"};
  const char *u = attr(n, "mediaTimeUnit");
  int good = !u;
  for (size_t i = 0; u && i < sizeof ok / sizeof *ok; i++) good = good || !strcmp(u, ok[i]);
  for (int i = 0; good && i < n->nkids; i++) good = media_time_unit(n->kids[i]);
  return good;
}

int dvbtva_check_fragment(const bim_tvans_t *ns, const dvbtva_fuu_t *f, bim_err_t *err) {
  const bim_node_t *r = f->root;
  const bim_node_t *bd;
  const bim_node_t *u;
  memset(err, 0, sizeof *err);
  switch (f->code) {
    case 0x0001:
      if (!is_crid(attr(r, "programId"))) return rule(err, "8.3 ProgramInformation programId not a CRID");
      if ((bd = child(ns, r, "BasicDescription")) && synopsis_length(ns, bd, 0)) return rule(err, "8.3 Synopsis without length");
      break;
    case 0x0002:
      if (!is_crid(attr(r, "groupId"))) return rule(err, "8.4 GroupInformation groupId not a CRID");
      if (!(bd = child(ns, r, "BasicDescription")) || !child(ns, bd, "Title")) return rule(err, "8.4 GroupInformation without Title");
      if (synopsis_length(ns, bd, 1)) return rule(err, "8.4 Synopsis without length");
      break;
    case 0x0004:
      if (child(ns, r, "PublishedEndTime")) return rule(err, "8.8 BroadcastEvent with PublishedEndTime");
      if (!(u = child(ns, r, "Program")) || !is_crid(attr(u, "crid"))) return rule(err, "8.8 BroadcastEvent without Program CRID");
      break;
    case 0x0005: return check_schedule(ns, r, err);
    case 0x0006:
      if (!(u = child(ns, r, "ServiceURL")) || strncmp(text(u), "dvb://", 6)) return rule(err, "8.6 ServiceInformation without DVB locator ServiceURL");
      break;
    case 0x000C:
    case 0x000D:
      if (!media_time_unit(r)) return rule(err, "8.7.3 mediaTimeUnit not of table 47");
      break;
    case 0x000E:
      if (attr(r, "publisher") && !publisher_ok(attr(r, "publisher"))) return rule(err, "8.9 TVAMain publisher format");
      break;
    default: break;
  }
  return 0;
}

typedef struct {
  const char *key;
  long long start;
  long long end;
} span_t;

static int span_cmp(const void *a, const void *b) {
  const span_t *x = a;
  const span_t *y = b;
  int c = strcmp(x->key, y->key);
  return c ? c : (x->start > y->start) - (x->start < y->start);
}

typedef struct {
  const char **v;
  size_t n;
} ids_t;

static int strp_cmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* attr_name values of fragments of code, sorted. 0 ok, -1 oom */
static int ids_of(const dvbtva_fuu_t *f, size_t n, unsigned code, const char *attr_name, ids_t *out) {
  out->n = 0;
  if (!(out->v = malloc(sizeof *out->v * (n + 1)))) return -1;
  for (size_t i = 0; i < n; i++)
    if (f[i].code == code && attr(f[i].root, attr_name)) out->v[out->n++] = attr(f[i].root, attr_name);
  qsort(out->v, out->n, sizeof *out->v, strp_cmp);
  return 0;
}

static int has(const ids_t *ids, const char *want) { return ids->n && bsearch(&want, ids->v, ids->n, sizeof *ids->v, strp_cmp); }

static int refs_of(const bim_tvans_t *ns, const bim_node_t *n, const char *elem, const ids_t *groups, bim_err_t *err) {
  for (int i = 0; i < n->nkids; i++) {
    const bim_node_t *k = n->kids[i];
    if (k->name && !strcmp(k->name, elem) && attr(k, "crid") && !has(groups, attr(k, "crid"))) return rule(err, "8.2 group CRID without GroupInformation fragment");
    if (refs_of(ns, k, elem, groups, err)) return -1;
  }
  return 0;
}

int dvbtva_check_set(const bim_tvans_t *ns, const dvbtva_fuu_t *f, size_t n, bim_err_t *err) {
  span_t *sp = calloc(n + 1, sizeof *sp);
  ids_t groups = {NULL, 0};
  ids_t services = {NULL, 0};
  ids_t programs = {NULL, 0};
  size_t ns_n = 0;
  int rc = 0;
  memset(err, 0, sizeof *err);
  if (!sp || ids_of(f, n, 0x0002, "groupId", &groups) || ids_of(f, n, 0x0006, "serviceId", &services) || ids_of(f, n, 0x0001, "programId", &programs)) {
    rc = bim_oom(err);
    goto out;
  }
  for (size_t i = 0; !rc && i < n; i++) {
    const bim_node_t *r = f[i].root;
    if (f[i].code == 0x0001) {
      rc = refs_of(ns, r, "MemberOf", &groups, err) || refs_of(ns, r, "EpisodeOf", &groups, err);
    } else if (f[i].code == 0x0005 || f[i].code == 0x0004) {
      const char *svc = attr(r, "serviceIDRef");
      if (svc && !has(&services, svc)) rc = rule(err, "8.2 serviceID without ServiceInformation fragment");
      if (!rc && f[i].code == 0x0005) {
        for (int k = 0; !rc && k < r->nkids; k++) {
          const bim_node_t *ev = r->kids[k];
          const bim_node_t *p = ev->name && !strcmp(ev->name, "ScheduleEvent") ? child(ns, ev, "Program") : NULL;
          if (p && attr(p, "crid") && !child(ns, ev, "InstanceDescription") && !has(&programs, attr(p, "crid")))
            rc = rule(err, "8.2 ScheduleEvent without ProgramInformation fragment");
        }
        if (!rc && svc && attr(r, "start") && attr(r, "end") && !iso8601_utc_secs(attr(r, "start"), &sp[ns_n].start) && !iso8601_utc_secs(attr(r, "end"), &sp[ns_n].end)) sp[ns_n++].key = svc;
      }
    }
  }
  qsort(sp, ns_n, sizeof *sp, span_cmp);
  for (size_t i = 1; !rc && i < ns_n; i++)
    if (!strcmp(sp[i].key, sp[i - 1].key) && sp[i].start != sp[i - 1].end) rc = rule(err, "8.5 Schedules of a service not contiguous");
out:
  free(sp);
  free(groups.v);
  free(services.v);
  free(programs.v);
  return rc ? -1 : 0;
}
