/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_IOUTIL_H
#define LIB_IOUTIL_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>

/* null-terminated, malloc'd. 0 ok, -1 error */
int read_all(FILE *f, char **out, size_t *out_len);

/* bounded strcpy: copies at most dstsz-1 bytes of src, always NUL-terminates dst.
   returns strlen(src). truncated if >= dstsz, matches BSD strlcpy */
size_t bufcpy(char *dst, size_t dstsz, const char *src);

/* decimal digits of val into dst, NUL-terminated, zero-padded to at least min_width (like %0*u, widens past min_width rather than truncating).
   no format-string parsing. dst needs >= min_width+1, and >= 11 regardless (UINT_MAX is 10 digits). returns strlen(dst) */
size_t uint_to_str_pad(char *dst, unsigned val, unsigned min_width);

/* uint_to_str_pad(dst, val, 0): plain decimal, no padding */
size_t uint_to_str(char *dst, unsigned val);

/* grow arr for need elems of elemsz B, doubling *cap (16 initial, or need if bigger).
   new arr on success, NULL on OOM (arr, *cap unchanged) */
void *array_grow(void *arr, int *cap, int need, size_t elemsz);

/* doubles *cap (elem_size units, initial_elems if 0) until >= need_elems, realloc *buf.
   0 ok, -1 OOM (*buf, *cap unchanged) */
int growbuf_reserve(void **buf, size_t *cap, size_t elem_size, size_t need_elems, size_t initial_elems);

/* EINTR/EAGAIN-retrying pipe write, 100ms poll between EAGAIN spins.
   0 written fully, -1 unrecoverable (*stop set or EPIPE) */
int pipe_write_all(int fd, const unsigned char *buf, size_t n, const atomic_int *stop);

/* s[0..n) all ASCII digits */
int all_digits(const char *s, int n);

/* strips trailing \n/\r in place */
void chomp(char *line);

/* splits line in place on ',', writes up to max_fields pointers into fields[]. returns fill count */
size_t csv_split(char *line, char **fields, size_t max_fields);

typedef enum { ISO8601_OFF_NONE, ISO8601_OFF_Z, ISO8601_OFF_NUMERIC } iso8601_offset_kind_t;

typedef struct {
  int y, mo, d, h, mi, s;
  int off_min; /* signed minutes east of UTC, meaningful only if offset_kind == ISO8601_OFF_NUMERIC */
  iso8601_offset_kind_t offset_kind;
} iso8601_t;

/* "YYYY-MM-DDTHH:MM:SS[Z|+HH:MM|-HH:MM]". 0 ok, -1 malformed */
int iso8601_split(const char *in, iso8601_t *out);

/* EN 300 468 annex C, proleptic Gregorian calendar date to modified Julian Day */
long date_to_mjd(int y, int mo, int d);

/* power of two >= n, 1 if n == 0 */
static inline size_t next_pow2(size_t n) {
  size_t p = 1;
  while (p < n)
    p <<= 1;
  return p;
}

#endif
