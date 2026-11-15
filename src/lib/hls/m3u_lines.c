/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "m3u_lines.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "lib/helper/strutil.h"

char *playlist_skip_blank(char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

char *playlist_next_line(char **cursor) {
  char *start = *cursor;
  char *nl;
  size_t l;

  if (!start || !*start) return NULL;
  nl = strchr(start, '\n');
  if (nl) {
    *nl = '\0';
    *cursor = nl + 1;
  } else {
    *cursor = start + strlen(start);
  }
  l = strlen(start);
  while (l && (start[l - 1] == '\r' || start[l - 1] == ' ' || start[l - 1] == '\t')) start[--l] = '\0';
  while (*start == ' ' || *start == '\t') start++;
  return start;
}

int playlist_resolve_relative(const http_url_t *base, const char *ref, char *out, size_t n) {
  const char *dir_end;
  int v6;
  sbuf_t sb;
  if (!base) return 0;
  v6 = strchr(base->host, ':') != NULL;
  sbuf_init(&sb, out, n);
  sbuf_add(&sb, base->tls ? "https://" : "http://");
  if (v6) sbuf_add(&sb, "[");
  sbuf_add(&sb, base->host);
  if (v6) sbuf_add(&sb, "]");
  if (base->port != (unsigned)(base->tls ? 443 : 80)) {
    sbuf_add(&sb, ":");
    sbuf_add_uint(&sb, base->port);
  }
  if (ref[0] != '/') {
    dir_end = strrchr(base->path, '/');
    if (dir_end) sbuf_add_n(&sb, base->path, (size_t)(dir_end - base->path) + 1);
  }
  sbuf_add(&sb, ref);
  return !sb.truncated;
}

void playlist_resolve_uri(const http_url_t *base, const char *ref, char *out, size_t n) {
  if (!strncasecmp(ref, "http://", 7) || !strncasecmp(ref, "https://", 8)) bufcpy(out, n, ref);
  else if (ref[0] == '/' && ref[1] == '/') {
    sbuf_t sb;
    sbuf_init(&sb, out, n);
    sbuf_add(&sb, (base && base->tls) ? "https:" : "http:");
    sbuf_add(&sb, ref);
    if (!base || sb.truncated) out[0] = '\0';
  }
  else if (!playlist_resolve_relative(base, ref, out, n)) out[0] = '\0';
}
