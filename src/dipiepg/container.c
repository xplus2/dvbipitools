/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "container.h"

#define HEADER_LEN (1 + 2 * 8) /* num_structures + 2 entries of 8 bytes each */

static void put24(unsigned char *p, size_t v) {
  p[0] = (unsigned char)(v >> 16);
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)v;
}

static size_t get24(const unsigned char *p) {
  return ((size_t)p[0] << 16) | ((size_t)p[1] << 8) | (size_t)p[2];
}

int container_build(const unsigned char *access_unit, size_t au_len, const unsigned char *string_repo, size_t sr_len, unsigned char **out, size_t *out_len) {
  size_t total = HEADER_LEN + au_len + sr_len;
  unsigned char *buf = malloc(total);
  unsigned char *p;
  if (!buf)
    return -1;

  buf[0] = 2; /* num_structures */
  p = buf + 1;
  p[0] = CONTAINER_STRUCT_DATA_REPOSITORY;
  p[1] = CONTAINER_DATAREPO_BINARY;
  put24(p + 2, HEADER_LEN);
  put24(p + 5, au_len);
  p += 8;
  p[0] = CONTAINER_STRUCT_DATA_REPOSITORY;
  p[1] = CONTAINER_DATAREPO_STRINGS;
  put24(p + 2, HEADER_LEN + au_len);
  put24(p + 5, sr_len);

  memcpy(buf + HEADER_LEN, access_unit, au_len);
  memcpy(buf + HEADER_LEN + au_len, string_repo, sr_len);

  *out = buf;
  *out_len = total;
  return 0;
}

int container_parse(const unsigned char *buf, size_t len, const unsigned char **au, size_t *au_len,  const unsigned char **sr, size_t *sr_len) {
  unsigned num_structures, i;
  const unsigned char *p;

  *au = NULL;
  *sr = NULL;
  if (len < 1)
    return -1;
  num_structures = buf[0];
  if (len < 1 + (size_t)num_structures * 8)
    return -1;

  p = buf + 1;
  for (i = 0; i < num_structures; i++, p += 8) {
    unsigned type = p[0], id = p[1];
    size_t ptr = get24(p + 2), length = get24(p + 5);
    if (ptr > len || length > len - ptr)
      return -1;
    if (type != CONTAINER_STRUCT_DATA_REPOSITORY)
      continue;
    if (id == CONTAINER_DATAREPO_BINARY) {
      *au = buf + ptr;
      *au_len = length;
    } else if (id == CONTAINER_DATAREPO_STRINGS) {
      *sr = buf + ptr;
      *sr_len = length;
    }
  }
  return (*au && *sr) ? 0 : -1;
}
