/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_PLAYLIST_OUT_H
#define DVBIPITOOLS_LIB_HELPER_PLAYLIST_OUT_H

#include <stddef.h>
#include <stdio.h>

#include "argutil.h"

/* member order must match each caller's own out_fmt_t: OUT_M3U, OUT_CSV, OUT_XSPF,
   OUT_XML, OUT_NULL. different type per tool, same order, cast at call sites */
typedef enum { PLAYLIST_OUT_M3U, PLAYLIST_OUT_CSV, PLAYLIST_OUT_XSPF, PLAYLIST_OUT_XML, PLAYLIST_OUT_NULL } playlist_out_fmt_t;

static inline int playlist_out_fmt_parse(const char *s, int *out) {
  static const enum_map_t map[] = {{"m3u", PLAYLIST_OUT_M3U}, {"csv", PLAYLIST_OUT_CSV}, {"xspf", PLAYLIST_OUT_XSPF}, {"xml", PLAYLIST_OUT_XML}, {"null", PLAYLIST_OUT_NULL}};
  return map_lookup(map, sizeof map / sizeof map[0], s, out);
}

/* "%Y-%m-%d %H:%M" UTC */
void playlist_out_stamp(char *buf, size_t n);

/* M3U/XSPF header only, CSV/XML/NULL no-op. title_prefix before "<stamp> UTC" in XSPF title */
void playlist_out_init(FILE *f, playlist_out_fmt_t fmt, const char *invocation, const char *title_prefix);

/* XSPF footer only, M3U has none */
void playlist_out_close(FILE *f, playlist_out_fmt_t fmt);

/* #EXTINF line + uri line. control chars dropped, '"' dropped from logo */
void playlist_out_m3u_item(FILE *f, const char *name, const char *uri, const char *icon_uri, unsigned tsid, unsigned onid, unsigned sid);

/* name with any ',' stripped (field separator), then ,uri,tsid,onid,sid */
void playlist_out_csv_item(FILE *f, const char *name, const char *uri, unsigned tsid, unsigned onid, unsigned sid);

/* <track> with dvb-triplet extension */
void playlist_out_xspf_item(FILE *f, const char *name, const char *uri, const char *icon_uri, unsigned tsid, unsigned onid, unsigned sid);

#endif
