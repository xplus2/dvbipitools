/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBIM_STRREPO_H
#define DIPIBIM_STRREPO_H

#include <stddef.h>

typedef struct {
  unsigned char *buf;
  size_t cap;
  size_t len;
} strrepo_writer_t;

void strrepo_writer_init(strrepo_writer_t *sw);
void strrepo_writer_free(strrepo_writer_t *sw);
/* appends s (nul-terminated) + terminator. returns 0 ok, -1 oom */
int strrepo_writer_put(strrepo_writer_t *sw, const char *s);
/* full repository: encoding_type byte + all appended strings */
const unsigned char *strrepo_writer_data(const strrepo_writer_t *sw, size_t *out_len);

typedef struct {
  const unsigned char *buf;
  size_t len;
  size_t pos; /* next unread byte */
} strrepo_reader_t;

/* checks/skips the encoding_type byte. returns 0 ok, -1 empty or unsupported encoding */
int strrepo_reader_init(strrepo_reader_t *sr, const unsigned char *buf, size_t len);
/* reads the next terminator-delimited string, truncates silently like xml_elem_text.
 * returns 0 ok, -1 no more strings (no terminator found before end of buffer) */
int strrepo_reader_next(strrepo_reader_t *sr, char *out, size_t outcap);

#endif
