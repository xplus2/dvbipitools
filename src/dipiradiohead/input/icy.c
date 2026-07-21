/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "icy.h"

#define ICY_META_CAP (255 * 16)

typedef enum { ST_AUDIO, ST_LEN, ST_META } icy_state_t;

struct icy {
  size_t metaint;
  icy_meta_cb cb;
  void *ctx;

  icy_state_t state;
  size_t audio_count;
  size_t meta_need, meta_have;
  unsigned char meta_buf[ICY_META_CAP + 1];
  char last_title[512];
};

icy_t *icy_new(size_t metaint, icy_meta_cb cb, void *ctx) {
  icy_t *c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  c->metaint = metaint;
  c->cb = cb;
  c->ctx = ctx;
  c->state = ST_AUDIO;
  return c;
}

void icy_free(icy_t *c) { free(c); }

/* "StreamTitle='...';" -> split on first " - " into artist/title */
static void handle_meta_block(icy_t *c) {
  const char *tag = "StreamTitle='";
  char *start, *end;
  size_t len;

  c->meta_buf[c->meta_have] = '\0';
  start = strstr((char *)c->meta_buf, tag);
  if (!start)
    return;
  start += strlen(tag);
  end = strstr(start, "';");
  if (!end)
    return;
  len = (size_t)(end - start);
  if (len >= sizeof c->last_title)
    len = sizeof c->last_title - 1;

  if (len == strlen(c->last_title) && !memcmp(start, c->last_title, len))
    return; /* unchanged */
  memcpy(c->last_title, start, len);
  c->last_title[len] = '\0';

  if (!c->cb)
    return;
  {
    char artist[sizeof c->last_title] = "", title[sizeof c->last_title];
    const char *sep = strstr(c->last_title, " - ");
    if (sep) {
      size_t alen = (size_t)(sep - c->last_title);
      if (alen >= sizeof artist)
        alen = sizeof artist - 1;
      memcpy(artist, c->last_title, alen);
      artist[alen] = '\0';
      snprintf(title, sizeof title, "%s", sep + 3);
    } else {
      snprintf(title, sizeof title, "%s", c->last_title);
    }
    c->cb(c->ctx, artist, title);
  }
}

size_t icy_feed(icy_t *c, const unsigned char *in, size_t inlen, unsigned char *out, size_t cap) {
  size_t i, w = 0;

  if (c->metaint == 0) {
    size_t n = inlen < cap ? inlen : cap;
    memcpy(out, in, n);
    return n;
  }
  for (i = 0; i < inlen; i++) {
    unsigned char b = in[i];
    switch (c->state) {
      case ST_AUDIO:
        if (w < cap)
          out[w++] = b;
        if (++c->audio_count == c->metaint)
          c->state = ST_LEN;
        break;
      case ST_LEN:
        c->meta_need = (size_t)b * 16;
        c->meta_have = 0;
        c->audio_count = 0;
        c->state = (c->meta_need == 0) ? ST_AUDIO : ST_META;
        break;
      case ST_META:
        if (c->meta_have < ICY_META_CAP)
          c->meta_buf[c->meta_have++] = b;
        if (c->meta_have >= c->meta_need) {
          handle_meta_block(c);
          c->state = ST_AUDIO;
        }
        break;
    }
  }
  return w;
}
