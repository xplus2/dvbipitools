/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"

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

static int utf8_valid(const char *s) {
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    size_t n, i;
    if (*p < 0x80) n = 0;
    else if (*p >= 0xC2 && *p <= 0xDF) n = 1;
    else if ((*p & 0xF0) == 0xE0) n = 2;
    else if (*p >= 0xF0 && *p <= 0xF4) n = 3;
    else return 0;
    for (i = 1; i <= n; i++)
      if ((p[i] & 0xC0) != 0x80) return 0;
    p += n + 1;
  }
  return 1;
}

static void to_utf8(char *dst, size_t cap, const char *src) {
  size_t o = 0;
  if (utf8_valid(src)) {
    bufcpy(dst, cap, src);
    return;
  }
  for (; *src && o + 2 < cap; src++) {
    unsigned char b = (unsigned char)*src;
    if (b < 0x80) {
      dst[o++] = (char)b;
    } else {
      dst[o++] = (char)(0xC0 | (b >> 6));
      dst[o++] = (char)(0x80 | (b & 0x3F));
    }
  }
  dst[o] = '\0';
}

/* "StreamTitle='...';" -> split on first " - " into artist/title */
static void handle_meta_block(icy_t *c) {
  const char *tag = "StreamTitle='";
  char *start, *end;
  size_t len;
  char artist[2 * sizeof c->last_title];
  char title[2 * sizeof c->last_title];
  char raw[sizeof c->last_title];
  const char *sep;

  c->meta_buf[c->meta_have] = '\0';
  start = strstr((char *)c->meta_buf, tag);
  if (!start) return;
  start += strlen(tag);
  end = strstr(start, "';");
  if (!end) return;
  len = (size_t)(end - start);
  if (len >= sizeof c->last_title) len = sizeof c->last_title - 1;

  if (len == strlen(c->last_title) && !memcmp(start, c->last_title, len))
    return; /* unchanged */
  memcpy(c->last_title, start, len);
  c->last_title[len] = '\0';

  if (!c->cb) return;

  sep = strstr(c->last_title, " - ");
  if (sep) {
    size_t alen = (size_t)(sep - c->last_title);
    memcpy(raw, c->last_title, alen);
    raw[alen] = '\0';
    to_utf8(artist, sizeof artist, raw);
    to_utf8(title, sizeof title, sep + 3);
  } else {
    artist[0] = '\0';
    to_utf8(title, sizeof title, c->last_title);
  }
  c->cb(c->ctx, artist, title);
}

size_t icy_feed(icy_t *c, const unsigned char *in, size_t inlen, unsigned char *out, size_t cap) {
  size_t r = 0;
  size_t w = 0;
  if (c->metaint == 0) {
    size_t n = inlen < cap ? inlen : cap;
    memcpy(out, in, n);
    return n;
  }
  while (r < inlen) {
    unsigned char b;
    if (c->state == ST_AUDIO) {
      size_t remain_audio = c->metaint - c->audio_count;
      size_t avail_in = inlen - r;
      size_t n = remain_audio < avail_in ? remain_audio : avail_in;
      size_t to_write = cap > w ? cap - w : 0;
      if (to_write > n) to_write = n;
      if (to_write > 0) {
        memcpy(out + w, in + r, to_write);
        w += to_write;
      }
      r += n;
      c->audio_count += n;
      if (c->audio_count == c->metaint) c->state = ST_LEN;
      continue;
    }
    b = in[r++];
    switch (c->state) {
      case ST_LEN:
        c->meta_need = (size_t)b * 16;
        c->meta_have = 0;
        c->audio_count = 0;
        c->state = (c->meta_need == 0) ? ST_AUDIO : ST_META;
        break;
      case ST_META:
        if (c->meta_have < ICY_META_CAP) c->meta_buf[c->meta_have++] = b;
        if (c->meta_have >= c->meta_need) {
          handle_meta_block(c);
          c->state = ST_AUDIO;
        }
        break;
      default:
        break;
    }
  }
  return w;
}
