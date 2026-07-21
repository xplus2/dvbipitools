/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "playlist.h"

#define SNIFF_CAP 4096

/* ID3 tag or MPEG/ADTS sync at offset 0: audio, not a playlist */
static int looks_binary(const unsigned char *b, size_t len) {
  if (len >= 3 && memcmp(b, "ID3", 3) == 0)
    return 1;
  if (len >= 2 && b[0] == 0xFF && (b[1] & 0xE0) == 0xE0)
    return 1;
  return 0;
}

static size_t sniff_copy(const unsigned char *body, size_t len, char *out, size_t cap) {
  size_t n = len < cap - 1 ? len : cap - 1;
  memcpy(out, body, n);
  out[n] = '\0';
  return n;
}

/* splits *cursor on '\n', trims \r and surrounding blanks, NULL at end */
static char *next_line(char **cursor) {
  char *start = *cursor;
  char *nl;
  size_t l;

  if (!start || !*start)
    return NULL;
  nl = strchr(start, '\n');
  if (nl) {
    *nl = '\0';
    *cursor = nl + 1;
  } else {
    *cursor = start + strlen(start);
  }
  l = strlen(start);
  while (l && (start[l - 1] == '\r' || start[l - 1] == ' ' || start[l - 1] == '\t'))
    start[--l] = '\0';
  while (*start == ' ' || *start == '\t')
    start++;
  return start;
}

static int is_url_line(const char *l) { return !strncasecmp(l, "http://", 7) || !strncasecmp(l, "https://", 8); }

static int parse_m3u(char *text, char *url, size_t n) {
  char *cur = text, *line;
  while ((line = next_line(&cur)) != NULL) {
    if (!*line || line[0] == '#')
      continue;
    if (is_url_line(line)) {
      snprintf(url, n, "%s", line);
      return 1;
    }
  }
  return 0;
}

/* "FileN=<url>", case-insensitive key, first match wins */
static int is_file_key(const char *l, const char **value) {
  const char *p = l;
  if (strncasecmp(p, "file", 4))
    return 0;
  p += 4;
  if (!isdigit((unsigned char)*p))
    return 0;
  while (isdigit((unsigned char)*p))
    p++;
  if (*p != '=')
    return 0;
  *value = p + 1;
  return 1;
}

static int parse_pls(char *text, char *url, size_t n) {
  char *cur = text, *line;
  while ((line = next_line(&cur)) != NULL) {
    const char *v;
    if (is_file_key(line, &v) && is_url_line(v)) {
      snprintf(url, n, "%s", v);
      return 1;
    }
  }
  return 0;
}

int playlist_extract(const unsigned char *body, size_t len, char *url, size_t n) {
  char buf[SNIFF_CAP];
  char *p;

  if (looks_binary(body, len))
    return 0;
  sniff_copy(body, len, buf, sizeof buf);

  p = buf;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
    p++;
  if (!strncasecmp(p, "[playlist]", 10))
    return parse_pls(buf, url, n);
  return parse_m3u(buf, url, n);
}
