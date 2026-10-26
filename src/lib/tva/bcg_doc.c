/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sys/ioutil.h"
#include "../helper/log.h"
#include "../helper/uriparse.h"

#include "bcg_doc.h"

void bcg_doc_init(bcg_doc_t *d) { memset(d, 0, sizeof *d); }

void bcg_doc_free(bcg_doc_t *d) {
  for (int i = 0; i < d->programme_count; i++) bcg_programme_free_text(&d->programmes[i]);
  free(d->channels);
  free(d->programmes);
  memset(d, 0, sizeof *d);
}

int bcg_set_text(char **dst, const char *s) {
  char *c = NULL;
  if (s && s[0]) {
    c = strdup(s);
    if (!c) return -1;
  }
  free(*dst);
  *dst = c;
  return 0;
}

int bcg_elem_text(const char *s, const char *end, const char *tag, char **out) {
  char *t = NULL;
  int rc = xml_elem_text_dup(s, end, tag, &t);
  if (rc == -2) return -1;
  if (rc == 0 && !t[0]) {
    free(t);
    t = NULL;
  }
  free(*out);
  *out = t;
  return 0;
}

void bcg_programme_free_text(bcg_programme_t *pr) {
  free(pr->title);
  free(pr->desc);
  free(pr->category);
  free(pr->crid);
  pr->crid = NULL;
  pr->title = NULL;
  pr->desc = NULL;
  pr->category = NULL;
}

int bcg_programme_copy(bcg_programme_t *dst, const bcg_programme_t *src) {
  *dst = *src;
  dst->title = NULL;
  dst->desc = NULL;
  dst->category = NULL;
  dst->crid = NULL;
  if (bcg_set_text(&dst->title, src->title) || bcg_set_text(&dst->desc, src->desc) || bcg_set_text(&dst->category, src->category)) {
    bcg_programme_free_text(dst);
    return -1;
  }
  return 0;
}

void bcg_progtext_free(bcg_progtext_t *pt) {
  free(pt->crid);
  free(pt->title);
  free(pt->desc);
  free(pt->category);
  memset(pt, 0, sizeof *pt);
}

bcg_channel_t *bcg_add_channel(bcg_doc_t *d) {
  void *p = array_grow(d->channels, &d->channel_cap, d->channel_count + 1, sizeof *d->channels);
  if (!p) return NULL;
  d->channels = p;
  memset(&d->channels[d->channel_count], 0, sizeof *d->channels);
  return &d->channels[d->channel_count++];
}

bcg_programme_t *bcg_add_programme(bcg_doc_t *d) {
  void *p = array_grow(d->programmes, &d->programme_cap, d->programme_count + 1, sizeof *d->programmes);
  if (!p) return NULL;
  d->programmes = p;
  memset(&d->programmes[d->programme_count], 0, sizeof *d->programmes);
  return &d->programmes[d->programme_count++];
}

const bcg_channel_t *bcg_find_channel(const bcg_doc_t *d, const char *id) {
  for (int i = 0; i < d->channel_count; i++) {
    if (!strcmp(d->channels[i].id, id)) return &d->channels[i];
  }
  return NULL;
}

int bcg_channel_add_name(bcg_channel_t *c, const char *name) {
  if (c->name_count >= BCG_MAX_NAMES) {
    log_code("LW400182", "bcg: channel %s has more than %d names, dropping \"%s\"", c->id, BCG_MAX_NAMES, name);
    return -1;
  }
  bufcpy(c->names[c->name_count], sizeof c->names[0], name);
  c->name_count++;
  return 0;
}

static int is_alnum(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

int bcg_publisher_name(const char *pub, char *out, size_t cap) {
  const char *slash = strchr(pub, '/');
  const char *p;
  if (!slash || slash == pub || !slash[1] || strlen(slash + 1) >= cap) return -1;
  for (p = pub; p < slash; p++)
    if (!is_alnum((unsigned char)*p) && *p != '-' && *p != '.') return -1;
  if (*pub == '.' || slash[-1] == '.') return -1;
  for (p = slash + 1; *p; p++)
    if (!is_alnum((unsigned char)*p) && !strchr("-._~!$&'()*+,;=:@%", *p)) return -1;
  memcpy(out, slash + 1, strlen(slash + 1) + 1);
  return uriparse_pct_decode(out);
}

int bcg_publisher_valid(const char *pub) {
  char name[BCG_ID_LEN];
  return strlen(pub) < BCG_ID_LEN && !bcg_publisher_name(pub, name, sizeof name);
}

int bcg_lang_valid(const char *lang) {
  size_t run = 0;
  int first = 1;
  if (strlen(lang) >= BCG_LANG_LEN) return 0;
  for (const char *p = lang;; p++) {
    if (!*p || *p == '-') {
      if (!run) return 0;
      if (!*p) return 1;
      run = 0;
      first = 0;
      continue;
    }
    if (++run > 8 || !(first ? ((*p | 0x20) >= 'a' && (*p | 0x20) <= 'z') : is_alnum((unsigned char)*p))) return 0;
  }
}

void bcg_doc_index_free(bcg_doc_index_t *ix) {
  free(ix->chan);
  free(ix->nprog);
  free(ix->first);
  free(ix->next);
  memset(ix, 0, sizeof *ix);
}

int bcg_doc_index_build(bcg_doc_index_t *ix, const bcg_doc_t *doc) {
  int *last = NULL;
  int rc = -1;
  memset(ix, 0, sizeof *ix);
  if (doc->channel_count) {
    ix->chan = malloc(sizeof *ix->chan * (size_t)doc->channel_count);
    last = malloc(sizeof *last * (size_t)doc->channel_count);
    ix->nprog = calloc((size_t)doc->channel_count, sizeof *ix->nprog);
    ix->first = malloc(sizeof *ix->first * (size_t)doc->channel_count);
    if (!ix->chan || !last || !ix->nprog || !ix->first) goto done;
    for (int i = 0; i < doc->channel_count; i++) {
      ix->chan[i].key = doc->channels[i].id;
      ix->chan[i].idx = i;
      ix->first[i] = -1;
      last[i] = -1;
    }
    bcg_key_index_sort(ix->chan, doc->channel_count);
  }
  if (doc->programme_count) {
    ix->next = malloc(sizeof *ix->next * (size_t)doc->programme_count);
    if (!ix->next) goto done;
    for (int i = 0; i < doc->programme_count; i++) {
      int ch = bcg_key_index_find(ix->chan, doc->channel_count, doc->programmes[i].channel_id);
      ix->next[i] = -1;
      if (ch < 0) continue;
      ix->nprog[ch]++;
      if (last[ch] < 0) ix->first[ch] = i;
      else ix->next[last[ch]] = i;
      last[ch] = i;
    }
  }
  rc = 0;
done:
  free(last);
  if (rc) bcg_doc_index_free(ix);
  return rc;
}

int bcg_progtext_apply(bcg_programme_t *pr, const bcg_progtext_t *pt) {
  return bcg_set_text(&pr->title, pt->title) || bcg_set_text(&pr->desc, pt->desc) || bcg_set_text(&pr->category, pt->category) ? -1 : 0;
}

void bcg_channel_dtt_format(const bcg_channel_t *c, char *out, size_t cap) {
  sbuf_t b;
  sbuf_init(&b, out, cap);
  if (!c->onid && !c->tsid && !c->sid) return;
  sbuf_add(&b, "dvb://");
  sbuf_add_hex(&b, c->onid);
  sbuf_add(&b, ".");
  sbuf_add_hex(&b, c->tsid);
  sbuf_add(&b, ".");
  sbuf_add_hex(&b, c->sid);
}

int bcg_channel_dtt_parse(bcg_channel_t *c, const char *s) {
  unsigned onid;
  unsigned tsid;
  unsigned sid;
  if (sscanf(s, "dvb://%x.%x.%x", &onid, &tsid, &sid) != 3) return -1;
  c->onid = onid;
  c->tsid = tsid;
  c->sid = sid;
  return 0;
}
