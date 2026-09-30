/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "m3u_lines.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "lib/helper/ioutil.h"

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
  char path[sizeof base->path];
  int len;
  if (!base) return 0;
  if (ref[0] == '/') {
    if (bufcpy(path, sizeof path, ref) >= sizeof path) return 0;
  } else {
    char *slash;
    if (bufcpy(path, sizeof path, base->path) >= sizeof path) return 0;
    slash = strrchr(path, '/');
    if (slash) slash[1] = '\0';
    else       path[0] = '\0';
    if (strlen(path) + strlen(ref) >= sizeof path) return 0;
    strcat(path, ref);
  }
  if (base->port == (unsigned)(base->tls ? 443 : 80)) len = snprintf(out, n, "%s://%s%s", base->tls ? "https" : "http", base->host, path);
  else len = snprintf(out, n, "%s://%s:%u%s", base->tls ? "https" : "http", base->host, base->port, path);

  return len > 0 && (size_t)len < n;
}

void playlist_resolve_uri(const http_url_t *base, const char *ref, char *out, size_t n) {
  if (!strncasecmp(ref, "http://", 7) || !strncasecmp(ref, "https://", 8)) bufcpy(out, n, ref);
  else if (!playlist_resolve_relative(base, ref, out, n)) out[0] = '\0';
}
