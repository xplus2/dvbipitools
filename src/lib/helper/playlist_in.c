/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ioutil.h"
#include "playlist_in.h"
#include "xml_util.h"

void playlist_list_free(playlist_list_t *pl) {
  int i;
  if (!pl)
    return;
  for (i = 0; i < pl->count; i++) {
    free(pl->items[i].name);
    free(pl->items[i].uri);
    free(pl->items[i].icon_uri);
  }
  free(pl->items);
  free(pl);
}

static playlist_item_t *list_append(playlist_list_t *pl) {
  void *p = array_grow(pl->items, &pl->cap, pl->count + 1, sizeof *pl->items);
  if (!p)
    return NULL;
  pl->items = p;
  return &pl->items[pl->count++];
}

static int slurp(const char *path, char **out, size_t *out_len) {
  FILE *f = fopen(path, "r");
  int rc;
  if (!f)
    return -1;
  rc = read_all(f, out, out_len);
  fclose(f);
  return rc;
}

/* rejects a hit inside a longer name, e.g. "sid" inside "tsid=" */
static int uint_attr(const char *s, const char *name, unsigned *out) {
  char pat[8];
  size_t patlen;
  const char *p = s;

  snprintf(pat, sizeof pat, "%s=\"", name);
  patlen = strlen(pat);
  for (;;) {
    const char *hit = strstr(p, pat);
    if (!hit)
      return 0;
    if (hit == s || !(isalnum((unsigned char)hit[-1]) || hit[-1] == '_')) {
      *out = (unsigned)strtoul(hit + patlen, NULL, 10);
      return 1;
    }
    p = hit + 1;
  }
}

/* "-1 tsid=\"1\" onid=\"2\" sid=\"3\",name" or generic "-1,name" after "#EXTINF:".
   tvg-logo="..." (de facto M3U icon attribute) recognized alongside the triplet */
static void extinf_parse(const char *body, playlist_item_t *it) {
  const char *comma = strchr(body, ',');
  char attrs[1024];
  char icon[512];
  size_t alen;
  unsigned t = 0, o = 0, s = 0;
  int has_t = 0, has_o = 0, has_s = 0;

  it->icon_uri = NULL;
  if (comma) {
    alen = (size_t)(comma - body);
    if (alen >= sizeof attrs)
      alen = sizeof attrs - 1;
    memcpy(attrs, body, alen);
    attrs[alen] = '\0';
    has_t = uint_attr(attrs, "tsid", &t);
    has_o = uint_attr(attrs, "onid", &o);
    has_s = uint_attr(attrs, "sid", &s);
    if (xml_attr(attrs, attrs + alen, "tvg-logo", icon, sizeof icon) == 0)
      it->icon_uri = strdup(icon);
    it->name = strdup(comma + 1);
  } else {
    it->name = strdup("");
  }
  it->tsid = t;
  it->onid = o;
  it->sid = s;
  it->has_triplet = has_t && has_o && has_s;
}

playlist_list_t *playlist_in_parse_m3u(const char *path) {
  char *buf;
  size_t len;
  playlist_list_t *pl;
  char *saveptr, *line;
  playlist_item_t pending;
  int have_pending = 0;

  if (slurp(path, &buf, &len))
    return NULL;

  pl = calloc(1, sizeof *pl);
  if (!pl) {
    free(buf);
    return NULL;
  }

  for (line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
    chomp(line);
    if (*line == '\0')
      continue;
    if (strncmp(line, "#EXTINF:", 8) == 0) {
      if (have_pending) {
        free(pending.name); /* prior EXTINF had no uri, drop it */
        free(pending.icon_uri);
      }
      extinf_parse(line + 8, &pending);
      have_pending = 1;
      continue;
    }
    if (*line == '#')
      continue; /* header/footer/comment */
    {
      playlist_item_t *slot = list_append(pl);
      if (!slot) {
        if (have_pending) {
          free(pending.name);
          free(pending.icon_uri);
        }
        break;
      }
      if (have_pending) {
        *slot = pending;
      } else {
        slot->name = strdup("");
        slot->icon_uri = NULL;
        slot->tsid = slot->onid = slot->sid = 0;
        slot->has_triplet = 0;
      }
      slot->uri = strdup(line);
      have_pending = 0;
    }
  }
  if (have_pending) {
    free(pending.name);
    free(pending.icon_uri);
  }
  free(buf);
  return pl;
}

playlist_list_t *playlist_in_parse_csv(const char *path) {
  char *buf;
  size_t len;
  playlist_list_t *pl;
  char *saveptr, *line;

  if (slurp(path, &buf, &len))
    return NULL;

  pl = calloc(1, sizeof *pl);
  if (!pl) {
    free(buf);
    return NULL;
  }

  for (line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
    char *fields[6];
    size_t nf;
    playlist_item_t *slot;
    chomp(line);
    if (*line == '\0')
      continue;
    nf = csv_split(line, fields, 6);
    if (nf < 2)
      continue; /* need at least name,uri */
    slot = list_append(pl);
    if (!slot)
      break;
    slot->name = strdup(fields[0]);
    slot->uri = strdup(fields[1]);
    slot->tsid = nf > 2 ? (unsigned)strtoul(fields[2], NULL, 10) : 0;
    slot->onid = nf > 3 ? (unsigned)strtoul(fields[3], NULL, 10) : 0;
    slot->sid = nf > 4 ? (unsigned)strtoul(fields[4], NULL, 10) : 0;
    slot->has_triplet = nf >= 5;
    slot->icon_uri = nf >= 6 && fields[5][0] ? strdup(fields[5]) : NULL;
  }
  free(buf);
  return pl;
}

static int track_cb(const char *tag, const char *blk_end, void *ctx) {
  playlist_list_t *pl = ctx;
  char uri[512], name[256], icon[512], tmp[32];
  int has_t, has_o, has_s;
  playlist_item_t *slot;

  if (xml_elem_text(tag, blk_end, "location", uri, sizeof uri))
    return 0; /* no location: skip track, not a parse failure */
  if (xml_elem_text(tag, blk_end, "title", name, sizeof name))
    name[0] = '\0';

  slot = list_append(pl);
  if (!slot)
    return -1;
  slot->uri = strdup(uri);
  slot->name = strdup(name);
  slot->icon_uri = xml_elem_text(tag, blk_end, "image", icon, sizeof icon) == 0 ? strdup(icon) : NULL;
  slot->tsid = slot->onid = slot->sid = 0;

  has_t = xml_attr(tag, blk_end, "tsid", tmp, sizeof tmp) == 0;
  if (has_t)
    slot->tsid = (unsigned)strtoul(tmp, NULL, 10);
  has_o = xml_attr(tag, blk_end, "onid", tmp, sizeof tmp) == 0;
  if (has_o)
    slot->onid = (unsigned)strtoul(tmp, NULL, 10);
  has_s = xml_attr(tag, blk_end, "sid", tmp, sizeof tmp) == 0;
  if (has_s)
    slot->sid = (unsigned)strtoul(tmp, NULL, 10);
  slot->has_triplet = has_t && has_o && has_s;
  return 0;
}

playlist_list_t *playlist_in_parse_xspf(const char *path) {
  char *buf;
  size_t len;
  playlist_list_t *pl;

  if (slurp(path, &buf, &len))
    return NULL;

  pl = calloc(1, sizeof *pl);
  if (!pl) {
    free(buf);
    return NULL;
  }
  if (for_each_xml_block(buf, buf + len, "<track", "</track>", track_cb, pl)) {
    playlist_list_free(pl);
    free(buf);
    return NULL;
  }
  free(buf);
  return pl;
}
