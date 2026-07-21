/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "id3.h"

struct id3 {
  id3_meta_cb cb;
  void *ctx;
  char last_artist[256], last_title[256];
};

id3_t *id3_new(id3_meta_cb cb, void *ctx) {
  id3_t *c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  c->cb = cb;
  c->ctx = ctx;
  return c;
}

void id3_free(id3_t *c) { free(c); }

int id3_is_tag(const unsigned char *p, size_t avail) { return avail >= 3 && memcmp(p, "ID3", 3) == 0; }

static unsigned syncsafe32(const unsigned char *b) {
  return ((unsigned)(b[0] & 0x7F) << 21) | ((unsigned)(b[1] & 0x7F) << 14) | ((unsigned)(b[2] & 0x7F) << 7) | (unsigned)(b[3] & 0x7F);
}

static unsigned be32(const unsigned char *b) {
  return ((unsigned)b[0] << 24) | ((unsigned)b[1] << 16) | ((unsigned)b[2] << 8) | (unsigned)b[3];
}

size_t id3_tag_size(const unsigned char *p, size_t avail) {
  unsigned body, footer;
  if (avail < 10 || !id3_is_tag(p, avail))
    return 0;
  body = syncsafe32(p + 6);
  footer = (p[3] == 4 && (p[5] & 0x10)) ? 10 : 0;
  return 10 + body + footer;
}

/* text frame body -> ISO-8859-1/UTF-8 as-is, UTF-16 downsampled to the low byte of each unit (ASCII-range only) */
static void text_frame(const unsigned char *body, size_t len, char *out, size_t cap) {
  size_t i, o = 0;
  unsigned char enc;
  out[0] = '\0';
  if (len == 0)
    return;
  enc = body[0];
  body++;
  len--;
  if (enc == 0x01 || enc == 0x02) {
    size_t start = 0;
    if (enc == 0x01 && len >= 2 && ((body[0] == 0xFF && body[1] == 0xFE) || (body[0] == 0xFE && body[1] == 0xFF)))
      start = 2;
    for (i = start; i + 1 < len && o + 1 < cap; i += 2) {
      unsigned char lo = body[i], hi = body[i + 1];
      unsigned char ch = (hi == 0) ? lo : (lo == 0 ? hi : '?');
      if (ch == 0)
        break;
      out[o++] = (char)ch;
    }
  } else {
    for (i = 0; i < len && o + 1 < cap; i++) {
      if (body[i] == 0)
        break;
      out[o++] = (char)body[i];
    }
  }
  out[o] = '\0';
}

void id3_consume(id3_t *c, const unsigned char *p, size_t taglen) {
  unsigned char version = (taglen > 3) ? p[3] : 0;
  unsigned char flags = (taglen > 5) ? p[5] : 0;
  size_t cursor = 10;
  char artist[256] = "", title[256] = "";
  int found = 0;

  if (taglen < 10)
    return;
  if (flags & 0x40) { /* extended header present, skip it */
    if (cursor + 4 > taglen)
      return;
    if (version >= 4)
      cursor += syncsafe32(p + cursor);
    else
      cursor += 4 + be32(p + cursor);
  }

  while (cursor + 10 <= taglen) {
    char id[5];
    unsigned fsize;
    const unsigned char *fbody;

    memcpy(id, p + cursor, 4);
    id[4] = '\0';
    if (id[0] == '\0')
      break; /* padding */
    fsize = (version >= 4) ? syncsafe32(p + cursor + 4) : be32(p + cursor + 4);
    fbody = p + cursor + 10;
    if (cursor + 10 + fsize > taglen)
      break;

    if (!strcmp(id, "TIT2")) {
      text_frame(fbody, fsize, title, sizeof title);
      found = 1;
    } else if (!strcmp(id, "TPE1")) {
      text_frame(fbody, fsize, artist, sizeof artist);
      found = 1;
    }
    cursor += 10 + fsize;
  }
  if (!found)
    return;
  if (!strcmp(artist, c->last_artist) && !strcmp(title, c->last_title))
    return;

  snprintf(c->last_artist, sizeof c->last_artist, "%s", artist);
  snprintf(c->last_title, sizeof c->last_title, "%s", title);
  if (c->cb)
    c->cb(c->ctx, artist, title);
}
