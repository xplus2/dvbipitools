/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_PLAYLIST_IN_H
#define LIB_PLAYLIST_IN_H

typedef struct {
  char *name; /* malloc'd, "" if absent */
  char *uri;  /* malloc'd */
  char *icon_uri; /* malloc'd, NULL if absent */
  unsigned tsid, onid, sid;
  int has_triplet; /* nonzero: triplet came from source, not defaulted */
} playlist_item_t;

typedef struct {
  playlist_item_t *items;
  int count;
  int cap;
} playlist_list_t;

/* dipiscan-shaped M3U (#EXTINF tsid/onid/sid attributes), or generic M3U */
playlist_list_t *playlist_in_parse_m3u(const char *path);

/* dipiscan-shaped XSPF (urn:dvbipitools:dvb-triplet <extension>), or generic
   third-party XSPF (<track><location> only). per-track <image> read as icon_uri */
playlist_list_t *playlist_in_parse_xspf(const char *path);

/* dipiscan-shaped CSV: name,uri[,tsid,onid,sid[,icon]]. trailing triplet fields optional, default=0 if absent.
   icon requires full triplet to precede it */
playlist_list_t *playlist_in_parse_csv(const char *path);

void playlist_list_free(playlist_list_t *pl);

#endif
