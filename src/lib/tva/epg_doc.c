/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epg_doc.h"

void epg_doc_init(epg_doc_t *d) { memset(d, 0, sizeof *d); }

void epg_doc_free(epg_doc_t *d) {
  free(d->channels);
  free(d->programmes);
  memset(d, 0, sizeof *d);
}

static void *grow(void *arr, int *cap, int need, size_t elemsz) {
  int newcap;
  void *p;
  if (need <= *cap)
    return arr;
  newcap = *cap ? *cap * 2 : 16;
  if (newcap < need)
    newcap = need;
  p = realloc(arr, (size_t)newcap * elemsz);
  if (!p)
    return NULL;
  *cap = newcap;
  return p;
}

epg_channel_t *epg_add_channel(epg_doc_t *d) {
  void *p = grow(d->channels, &d->channel_cap, d->channel_count + 1, sizeof *d->channels);
  if (!p)
    return NULL;
  d->channels = p;
  memset(&d->channels[d->channel_count], 0, sizeof *d->channels);
  return &d->channels[d->channel_count++];
}

epg_programme_t *epg_add_programme(epg_doc_t *d) {
  void *p = grow(d->programmes, &d->programme_cap, d->programme_count + 1, sizeof *d->programmes);
  if (!p)
    return NULL;
  d->programmes = p;
  memset(&d->programmes[d->programme_count], 0, sizeof *d->programmes);
  return &d->programmes[d->programme_count++];
}

const epg_channel_t *epg_find_channel(const epg_doc_t *d, const char *id) {
  int i;
  for (i = 0; i < d->channel_count; i++)
    if (!strcmp(d->channels[i].id, id))
      return &d->channels[i];
  return NULL;
}

void epg_channel_add_name(epg_channel_t *c, const char *name) {
  if (c->name_count >= EPG_MAX_NAMES)
    return;
  snprintf(c->names[c->name_count], sizeof c->names[0], "%s", name);
  c->name_count++;
}
