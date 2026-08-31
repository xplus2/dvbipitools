/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "pidfilter.h"

#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"

static int cmp_u16(const void *a, const void *b) {
  uint16_t x = *(const uint16_t *)a, y = *(const uint16_t *)b;
  return x < y ? -1 : (x > y ? 1 : 0);
}

int pid_token_parse(const char *s, char **end, unsigned long *out) {
  const char *tok = (*s == '0' && (s[1] == 'x' || s[1] == 'X')) ? s + 2 : s;
  int is_hex = tok != s;
  *out = strtoul(tok, end, is_hex ? 16 : 10);
  return *end != tok;
}

void pid_filter_parse(const char *value, pid_filter_t *out) {
  const char *p = value;
  out->count = 0;
  if (!p)
    return;
  while (*p && out->count < PID_FILTER_MAX) {
    char *end;
    unsigned long v;

    while (*p == ' ' || *p == ',')
      p++;
    if (!*p)
      break;
    if (!pid_token_parse(p, &end, &v)) { /* unparsable token: skip to next comma */
      while (*p && *p != ',')
        p++;
      continue;
    }
    if (v <= 8191)
      out->pids[out->count++] = (uint16_t)v;
    p = end;
  }
  if (out->count > 1)
    qsort(out->pids, (size_t)out->count, sizeof out->pids[0], cmp_u16);
  {
    int i, w = 0;
    for (i = 0; i < out->count; i++)
      if (i == 0 || out->pids[i] != out->pids[w - 1])
        out->pids[w++] = out->pids[i];
    out->count = w;
  }
}

int pid_filter_excludes(const pid_filter_t *f, unsigned pid) {
  int lo = 0, hi = f->count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (f->pids[mid] == pid)
      return 1;
    if (f->pids[mid] < pid)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return 0;
}

int pid_filter_equal(const pid_filter_t *a, const pid_filter_t *b) {
  return a->count == b->count && (a->count == 0 || !memcmp(a->pids, b->pids, (size_t)a->count * sizeof a->pids[0]));
}

void pid_filter_format(const pid_filter_t *f, char *buf, size_t bufsz) {
  size_t off = 0;
  int i;
  if (!bufsz)
    return;
  buf[0] = '\0';
  for (i = 0; i < f->count && off < bufsz; i++) {
    char frag[8];
    size_t flen = 0;
    if (i)
      frag[flen++] = ',';
    flen += uint_to_str(frag + flen, f->pids[i]);
    off += bufcpy(buf + off, bufsz - off, frag);
  }
}

int query_param_extract(const char *query, const char *key, char *buf, size_t bufsz) {
  size_t keylen = strlen(key);
  const char *f = query ? strstr(query, key) : NULL;
  size_t i = 0;
  const char *p;
  if (!f || !bufsz)
    return 0;
  p = f + keylen;
  while (*p && *p != '&' && i + 1 < bufsz)
    buf[i++] = *p++;
  buf[i] = '\0';
  return 1;
}

void pid_filter_parse_query(const char *query, pid_filter_t *out) {
  char buf[256];
  if (!query_param_extract(query, "filter=", buf, sizeof buf)) {
    out->count = 0;
    return;
  }
  pid_filter_parse(buf, out);
}
