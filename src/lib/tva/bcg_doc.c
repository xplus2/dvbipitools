/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sys/ioutil.h"
#include "../helper/log.h"

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
  pr->title = NULL;
  pr->desc = NULL;
  pr->category = NULL;
}

int bcg_programme_copy(bcg_programme_t *dst, const bcg_programme_t *src) {
  *dst = *src;
  dst->title = NULL;
  dst->desc = NULL;
  dst->category = NULL;
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

void bcg_channel_add_name(bcg_channel_t *c, const char *name) {
  if (c->name_count >= BCG_MAX_NAMES) {
    log_code("LW400182", "bcg: channel %s has more than %d names, dropping \"%s\"", c->id, BCG_MAX_NAMES, name);
    return;
  }
  bufcpy(c->names[c->name_count], sizeof c->names[0], name);
  c->name_count++;
}
