/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_PLAYLIST_IN_H
#define DVBIPITOOLS_LIB_HELPER_PLAYLIST_IN_H

#include <stdio.h>

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

#define PLAYLIST_CSV_LINE_MAX 1024

enum {
  PLAYLIST_CSV_NOMEM = -1,
  PLAYLIST_CSV_LONG_LINE = -2, /* line >= PLAYLIST_CSV_LINE_MAX bytes */
  PLAYLIST_CSV_BAD_NUM = -3    /* tsid/onid/sid not a decimal 0..65535 */
};

/* strict stream form of the CSV reader. 0 ok, else PLAYLIST_CSV_*; *err_line (may be NULL) = 1-based line of failure */
int playlist_in_read_csv(FILE *f, playlist_list_t **out, int *err_line);

void playlist_list_free(playlist_list_t *pl);

#endif
