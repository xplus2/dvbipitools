/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "lib/helper/ioutil.h"
#include "playlist.h"

#define SNIFF_CAP 4096

/* ID3 tag or MPEG/ADTS sync at offset 0: audio, not a playlist */
static int looks_binary(const unsigned char *b, size_t len) {
  if (len >= 3 && memcmp(b, "ID3", 3) == 0) return 1;
  if (len >= 2 && b[0] == 0xFF && (b[1] & 0xE0) == 0xE0) return 1;
  return 0;
}

static size_t sniff_copy(const unsigned char *body, size_t len, char *out, size_t cap) {
  size_t n = len < cap - 1 ? len : cap - 1;
  memcpy(out, body, n);
  out[n] = '\0';
  return n;
}

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

static int is_url_line(const char *l) { return !strncasecmp(l, "http://", 7) || !strncasecmp(l, "https://", 8); }

static int is_relative_ref(const char *l) { return *l && l[0] != '#' && !strstr(l, "://"); }

int playlist_resolve_relative(const http_url_t *base, const char *ref, char *out, size_t n) {
  char path[sizeof base->path];
  char *slash;
  int len;
  if (!base) return 0;
  if (ref[0] == '/') {
    if (bufcpy(path, sizeof path, ref) >= sizeof path) return 0;
  } else {
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

static int parse_m3u(char *text, const http_url_t *base, char *url, size_t n) {
  char *cur = text, *line;
  int extm3u = !strncmp(text, "#EXTM3U", 7);
  while ((line = playlist_next_line(&cur)) != NULL) {
    if (!*line || line[0] == '#') continue;
    if (is_url_line(line)) {
      bufcpy(url, n, line);
      return 1;
    }
    if (extm3u && is_relative_ref(line) && playlist_resolve_relative(base, line, url, n))
      return 1;
  }
  return 0;
}

/* "FileN=<url>", case-insensitive key, first match wins */
static int is_file_key(const char *l, const char **value) {
  const char *p = l;
  if (strncasecmp(p, "file", 4)) return 0;
  p += 4;
  if (!isdigit((unsigned char)*p)) return 0;
  while (isdigit((unsigned char)*p)) p++;
  if (*p != '=') return 0;
  *value = p + 1;
  return 1;
}

static int parse_pls(char *text, char *url, size_t n) {
  char *cur = text, *line;
  while ((line = playlist_next_line(&cur)) != NULL) {
    const char *v;
    if (is_file_key(line, &v) && is_url_line(v)) {
      bufcpy(url, n, v);
      return 1;
    }
  }
  return 0;
}

int playlist_extract(const unsigned char *body, size_t len, const http_url_t *base, char *url, size_t n) {
  char buf[SNIFF_CAP];
  char *p;
  if (looks_binary(body, len)) return 0;
  sniff_copy(body, len, buf, sizeof buf);
  p = playlist_skip_blank(buf);
  if (!strncasecmp(p, "[playlist]", 10)) return parse_pls(p, url, n);
  return parse_m3u(p, base, url, n);
}

int playlist_is_hls_media(const unsigned char *body, size_t len) {
  char buf[SNIFF_CAP];
  char *p;

  sniff_copy(body, len, buf, sizeof buf);
  p = playlist_skip_blank(buf);
  return !strncmp(p, "#EXTM3U", 7) && strstr(p, "#EXTINF:") != NULL;
}
