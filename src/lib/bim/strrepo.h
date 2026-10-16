/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_STRREPO_H
#define DVBIPITOOLS_LIB_BIM_STRREPO_H

#include <stddef.h>

typedef struct {
  unsigned char *buf;
  size_t cap;
  size_t len;
} strrepo_writer_t;

/* returns 0 ok, -1 oom (writer stays empty, puts fail) */
int strrepo_writer_init(strrepo_writer_t *sw);
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

/* checks/skips encoding_type byte. returns 0 ok, -1 empty or unsupported encoding */
int strrepo_reader_init(strrepo_reader_t *sr, const unsigned char *buf, size_t len);
/* moves cursor to byte offset off from repository start. -1 if off is inside encoding_type byte or past end */
int strrepo_reader_seek(strrepo_reader_t *sr, size_t off);
/* reads next terminator-delimited string.
   0 ok, -1 no more strings (no terminator found before end of buffer) or str does not fit outcap with NUL */
int strrepo_reader_next(strrepo_reader_t *sr, char *out, size_t outcap);
/* as next into a malloc'd string, never truncated. -1 also on OOM. *out untouched on failure */
int strrepo_reader_next_dup(strrepo_reader_t *sr, char **out);

#endif
