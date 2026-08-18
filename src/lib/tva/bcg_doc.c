/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ioutil.h"
#include "../log.h"

#include "bcg_doc.h"

void bcg_doc_init(bcg_doc_t *d) { memset(d, 0, sizeof *d); }

void bcg_doc_free(bcg_doc_t *d) {
  free(d->channels);
  free(d->programmes);
  memset(d, 0, sizeof *d);
}

bcg_channel_t *bcg_add_channel(bcg_doc_t *d) {
  void *p = array_grow(d->channels, &d->channel_cap, d->channel_count + 1, sizeof *d->channels);
  if (!p)
    return NULL;
  d->channels = p;
  memset(&d->channels[d->channel_count], 0, sizeof *d->channels);
  return &d->channels[d->channel_count++];
}

bcg_programme_t *bcg_add_programme(bcg_doc_t *d) {
  void *p = array_grow(d->programmes, &d->programme_cap, d->programme_count + 1, sizeof *d->programmes);
  if (!p)
    return NULL;
  d->programmes = p;
  memset(&d->programmes[d->programme_count], 0, sizeof *d->programmes);
  return &d->programmes[d->programme_count++];
}

const bcg_channel_t *bcg_find_channel(const bcg_doc_t *d, const char *id) {
  int i;
  for (i = 0; i < d->channel_count; i++)
    if (!strcmp(d->channels[i].id, id))
      return &d->channels[i];
  return NULL;
}

void bcg_channel_add_name(bcg_channel_t *c, const char *name) {
  if (c->name_count >= BCG_MAX_NAMES) {
    log_line("bcg: channel %s has more than %d names, dropping \"%s\"", c->id, BCG_MAX_NAMES, name);
    return;
  }
  snprintf(c->names[c->name_count], sizeof c->names[0], "%s", name);
  c->name_count++;
}
