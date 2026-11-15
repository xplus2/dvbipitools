/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_STRUTIL_H
#define DVBIPITOOLS_LIB_HELPER_STRUTIL_H

#include <stddef.h>
#include <stdint.h>

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

/* bounded strcpy: copies at most dstsz-1 bytes of src, always NUL-terminates dst.
   returns strlen(src). truncated if >= dstsz, matches BSD strlcpy */
size_t bufcpy(char *dst, size_t dstsz, const char *src);

/* decimal digits of val into dst, NUL-terminated, zero-padded to at least min_width (like %0*u, widens past min_width rather than truncating).
   no format-string parsing. dst needs >= min_width+1, and >= 11 regardless (UINT_MAX is 10 digits). returns strlen(dst) */
size_t uint_to_str_pad(char *dst, unsigned val, unsigned min_width);

/* uint_to_str_pad(dst, val, 0): plain decimal, no padding */
size_t uint_to_str(char *dst, unsigned val);

/* decimal digits of val into dst, NUL-terminated, no padding. dst needs >= 21. returns strlen(dst) */
size_t u64_to_dec(char *dst, uint64_t val);

/* s[0..n) all ASCII digits */
int all_digits(const char *s, int n);

/* strict decimal: nonempty, digits only, no overflow, <= max. 0 ok, -1 bad */
int parse_ulong(const char *s, unsigned long max, unsigned long *out);

/* strips trailing \n/\r in place */
void chomp(char *line);

/* splits line in place on ',', writes up to max_fields pointers into fields[], extra fields dropped. returns fill count.
   RFC 4180 quoted fields unquoted, blanks around fields trimmed */
size_t csv_split(char *line, char **fields, size_t max_fields);

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} dstrbuf_t;

/* OOM: buf stays NULL, every dstrbuf_appendf becomes a no-op */
void dstrbuf_init(dstrbuf_t *sb);
void dstrbuf_appendf(dstrbuf_t *sb, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

void dstrbuf_add_n(dstrbuf_t *sb, const char *s, size_t n);
void dstrbuf_add(dstrbuf_t *sb, const char *s);
void dstrbuf_add_u64(dstrbuf_t *sb, uint64_t v);
void dstrbuf_add_i64(dstrbuf_t *sb, int64_t v);

typedef struct {
  char *buf;
  size_t cap;
  size_t len;
  int truncated;
} sbuf_t;

/* fixed cap buf */
void sbuf_init(sbuf_t *b, char *buf, size_t cap);
void sbuf_add_n(sbuf_t *b, const char *s, size_t maxn);
void sbuf_add(sbuf_t *b, const char *s);
void sbuf_add_u64(sbuf_t *b, uint64_t v);
void sbuf_add_uint(sbuf_t *b, unsigned v);
void sbuf_add_hex2(sbuf_t *b, unsigned v);
void sbuf_add_hex(sbuf_t *b, unsigned v);
void sbuf_add_upad(sbuf_t *b, const char *pre, unsigned v, unsigned width);

#endif
