/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/ioutil.h"

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

/* encodes one Unicode code point as UTF-8. returns bytes written, 0 if it
   doesn't fit cap. never emits partial sequences */
static size_t put_utf8_cp(char *out, size_t cap, unsigned cp) {
  if (cp < 0x80) {
    if (cap < 1) return 0;
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    if (cap < 2) return 0;
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    if (cap < 3) return 0;
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  if (cap < 4) return 0;
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

static unsigned rd16(const unsigned char *p, int le) {
  return le ? (((unsigned)p[1] << 8) | p[0]) : (((unsigned)p[0] << 8) | p[1]);
}

/* ID3v2 UTF-16 (BOM per enc 0x01, fixed BE per enc 0x02) -> UTF-8, surrogate pairs included */
static void utf16_to_utf8(const unsigned char *body, size_t len, char *out, size_t cap, size_t *o) {
  size_t i, start = 0;
  int le = 0;

  if (len >= 2 && body[0] == 0xFF && body[1] == 0xFE) { le = 1; start = 2; }
  else if (len >= 2 && body[0] == 0xFE && body[1] == 0xFF) { start = 2; }

  for (i = start; i + 1 < len; i += 2) {
    unsigned unit = rd16(body + i, le);
    unsigned cp;
    size_t n;

    if (unit == 0)
      break;
    if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < len) {
      unsigned lo = rd16(body + i + 2, le);
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        cp = 0x10000 + ((unit - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      } else {
        cp = 0xFFFD; /* unpaired high surrogate */
      }
    } else if (unit >= 0xD800 && unit <= 0xDFFF) {
      cp = 0xFFFD; /* unpaired surrogate */
    } else {
      cp = unit;
    }
    n = put_utf8_cp(out + *o, cap - 1 - *o, cp);
    if (!n)
      break;
    *o += n;
  }
}

/* Latin 1/15: every byte is its own code point U+0000-U+00FF, 1:1 with Unicode */
static void latin1_to_utf8(const unsigned char *body, size_t len, char *out, size_t cap, size_t *o) {
  size_t i;
  for (i = 0; i < len; i++) {
    size_t n;
    if (body[i] == 0)
      break;
    n = put_utf8_cp(out + *o, cap - 1 - *o, body[i]);
    if (!n)
      break;
    *o += n;
  }
}

/* already UTF-8 (or unrecognized enc, best-effort): copy whole sequences only, never split one at the cap boundary */
static void utf8_copy(const unsigned char *body, size_t len, char *out, size_t cap, size_t *o) {
  size_t i = 0;
  while (i < len && body[i]) {
    unsigned char b0 = body[i];
    size_t seqlen = (b0 < 0x80) ? 1 : ((b0 & 0xE0) == 0xC0) ? 2 : ((b0 & 0xF0) == 0xE0) ? 3 : ((b0 & 0xF8) == 0xF0) ? 4 : 1;
    if (i + seqlen > len)
      break;
    if (*o + seqlen > cap - 1)
      break;
    memcpy(out + *o, body + i, seqlen);
    *o += seqlen;
    i += seqlen;
  }
}

/* text frame body, any ID3v2 encoding -> UTF-8 */
static void text_frame(const unsigned char *body, size_t len, char *out, size_t cap) {
  size_t o = 0;
  unsigned char enc;
  out[0] = '\0';
  if (len == 0 || cap == 0)
    return;
  enc = body[0];
  body++;
  len--;
  if (enc == 0x01 || enc == 0x02)
    utf16_to_utf8(body, len, out, cap, &o);
  else if (enc == 0x00)
    latin1_to_utf8(body, len, out, cap, &o);
  else
    utf8_copy(body, len, out, cap, &o);
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

  bufcpy(c->last_artist, sizeof c->last_artist, artist);
  bufcpy(c->last_title, sizeof c->last_title, title);
  if (c->cb)
    c->cb(c->ctx, artist, title);
}
