/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "strrepo.h"

#define STRREPO_ENCODING_UTF8 0x01

static int grow(strrepo_writer_t *sw, size_t more) {
  if (more > SIZE_MAX - sw->len) return -1;
  return growbuf_reserve((void **)&sw->buf, &sw->cap, 1, sw->len + more, 256);
}

int strrepo_writer_init(strrepo_writer_t *sw) {
  sw->buf = NULL;
  sw->cap = 0;
  sw->len = 0;
  sw->resync = NULL;
  if (grow(sw, 1)) return -1;
  sw->buf[sw->len++] = STRREPO_ENCODING_UTF8;
  sw->base = sw->len;
  return 0;
}

void strrepo_writer_free(strrepo_writer_t *sw) {
  free(sw->buf);
  sw->buf = NULL;
  sw->cap = 0;
  sw->len = 0;
  sw->resync = NULL;
}

int strrepo_writer_put(strrepo_writer_t *sw, const char *s) {
  size_t n = strlen(s);
  if (sw->len == 0) return -1; /* init failed */
  if (sw->resync) {
    bitwriter_t *bw = sw->resync;
    sw->resync = NULL;
    if (bitwriter_put(bw, sw->len - sw->base, 16)) return -1;
  }
  if (grow(sw, n + 1)) return -1;
  memcpy(sw->buf + sw->len, s, n);
  sw->len += n;
  sw->buf[sw->len++] = '\0';
  return 0;
}

const unsigned char *strrepo_writer_data(const strrepo_writer_t *sw, size_t *out_len) {
  *out_len = sw->len;
  return sw->buf;
}

int strrepo_reader_init(strrepo_reader_t *sr, const unsigned char *buf, size_t len) {
  sr->buf = buf;
  sr->len = len;
  sr->pos = 0;
  sr->base = 1;
  sr->resync = NULL;
  if (len < 1 || buf[0] != STRREPO_ENCODING_UTF8) return -1;
  sr->pos = 1;
  return 0;
}

int strrepo_reader_seek(strrepo_reader_t *sr, size_t off) {
  if (off < 1 || off > sr->len) return -1;
  sr->pos = off;
  return 0;
}

static int take_resync(strrepo_reader_t *sr) {
  uint64_t off;
  bitreader_t *br = sr->resync;
  if (!br) return 0;
  sr->resync = NULL;
  if (bitreader_get(br, 16, &off) || off > sr->len - sr->base) return -1;
  sr->pos = sr->base + (size_t)off;
  return 0;
}

static int peek(strrepo_reader_t *sr, size_t *start, size_t *n) {
  const unsigned char *z;
  if (take_resync(sr)) return -1;
  *start = sr->pos;
  if (*start >= sr->len) return -1;
  z = memchr(sr->buf + *start, 0, sr->len - *start);
  if (!z) return -1;
  *n = (size_t)(z - (sr->buf + *start));
  return 0;
}

int strrepo_reader_next(strrepo_reader_t *sr, char *out, size_t outcap) {
  size_t start;
  size_t n;
  if (peek(sr, &start, &n) || n >= outcap) return -1;
  memcpy(out, sr->buf + start, n);
  out[n] = '\0';
  sr->pos = start + n + 1;
  return 0;
}

int strrepo_reader_next_dup(strrepo_reader_t *sr, char **out) {
  size_t start;
  size_t n;
  char *s;
  if (peek(sr, &start, &n)) return -1;
  s = malloc(n + 1);
  if (!s) return -1;
  memcpy(s, sr->buf + start, n);
  s[n] = '\0';
  sr->pos = start + n + 1;
  *out = s;
  return 0;
}

const char *strrepo_at(const unsigned char *buf, size_t len, size_t off) {
  if (!buf || off < 1 || off >= len || !memchr(buf + off, 0, len - off)) return NULL;
  return (const char *)buf + off;
}
