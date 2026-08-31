/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_PIDFILTER_H
#define DIPIXY_PIDFILTER_H

#include <stddef.h>
#include <stdint.h>

#define PID_FILTER_MAX 32

/* sorted ascending, deduped, PIDs 0..8191 only */
typedef struct {
  uint16_t pids[PID_FILTER_MAX];
  int count;
} pid_filter_t;

/* parses one dec/0x-hex token at s ("101" or "0x20") into *out. *end set
   past number (== s if unparsable). 1 parsed, 0 unparsable */
int pid_token_parse(const char *s, char **end, unsigned long *out);

/* comma-separated decimal/0x-hex PIDs ("101,0x20"). NULL value: out empty.
   bad/out-of-range/excess tokens silently skipped */
void pid_filter_parse(const char *value, pid_filter_t *out);

/* extracts "filter=..." from a raw HTTP query string, then pid_filter_parse()s
   it. query NULL or no filter param: out left empty */
void pid_filter_parse_query(const char *query, pid_filter_t *out);

/* finds "key=" in query, copies value up to '&' or bufsz-1 into buf, NUL-terminates.
   1 found, 0 not (buf untouched) */
int query_param_extract(const char *query, const char *key, char *buf, size_t bufsz);

/* 1 if pid is in f (drop it), 0 otherwise */
int pid_filter_excludes(const pid_filter_t *f, unsigned pid);

/* 1 if a and b are same set, order never matters (parse already sorts) */
int pid_filter_equal(const pid_filter_t *a, const pid_filter_t *b);

/* "101,32" decimal csv, empty string if f->count == 0. truncates on bufsz overflow */
void pid_filter_format(const pid_filter_t *f, char *buf, size_t bufsz);

#endif
