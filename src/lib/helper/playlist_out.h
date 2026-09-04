/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_PLAYLIST_OUT_H
#define LIB_PLAYLIST_OUT_H

#include <stddef.h>
#include <stdio.h>

/* member order must match each caller's own out_fmt_t: OUT_M3U, OUT_CSV, OUT_XSPF,
   OUT_XML, OUT_NULL. different type per tool, same order, cast at call sites */
typedef enum { PLAYLIST_OUT_M3U, PLAYLIST_OUT_CSV, PLAYLIST_OUT_XSPF, PLAYLIST_OUT_XML, PLAYLIST_OUT_NULL } playlist_out_fmt_t;

/* "%Y-%m-%d %H:%M" UTC */
void playlist_out_stamp(char *buf, size_t n);

/* M3U/XSPF header only, CSV/XML/NULL no-op. title_prefix before "<stamp> UTC" in XSPF title */
void playlist_out_init(FILE *f, playlist_out_fmt_t fmt, const char *invocation, const char *title_prefix);

/* M3U/XSPF footer only */
void playlist_out_close(FILE *f, playlist_out_fmt_t fmt);

/* #EXTINF line + uri line */
void playlist_out_m3u_item(FILE *f, const char *name, const char *uri, const char *icon_uri, unsigned tsid, unsigned onid, unsigned sid);

/* name with any ',' stripped (field separator), then ,uri,tsid,onid,sid */
void playlist_out_csv_item(FILE *f, const char *name, const char *uri, unsigned tsid, unsigned onid, unsigned sid);

/* <track> with dvb-triplet extension */
void playlist_out_xspf_item(FILE *f, const char *name, const char *uri, const char *icon_uri, unsigned tsid, unsigned onid, unsigned sid);

#endif
