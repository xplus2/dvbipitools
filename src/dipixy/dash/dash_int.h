/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DASH_INT_H
#define DIPIXY_DASH_INT_H

#include "../segstore_int.h"

/* dash.c */
int parse_dash_seg_filename(const char *fn, uint64_t *t, seg_container_t *container);

#define DASH_MPD_BUF_CAP 8192

/* worst-case fixed text: head = prolog through Representation open, tail = closers after timeline,
   audio = split audio AdaptationSet without its timeline and utc url */
#define DASH_MPD_HEAD_MAX 2560
#define DASH_MPD_TAIL_MAX 512
#define DASH_MPD_AUDIO_MAX 1280
#define DASH_SEG_ENTRY_MAX 80

_Static_assert(DASH_MPD_BUF_CAP > DASH_MPD_HEAD_MAX + DASH_MPD_AUDIO_MAX + DASH_MPD_TAIL_MAX, "mpd cap");

/* vsnap, asnap: track stores. both set or both NULL: one muxed set from snap */
typedef struct {
  const hls_store_t *s;
  const hls_snapshot_t *snap;
  const hls_snapshot_t *vsnap;
  const hls_snapshot_t *asnap;
  int want_ll;
  const char *utc_url;
} mpd_fmt_ctx_t;
#define DASH_MPD_ALL_BUF_CAP (DASH_MPD_BUF_CAP * (PSI_LCEVC_MAX_LINKS + 1))

typedef struct {
  int status;
  uint8_t *body;
  size_t body_len;
  const cached_text_t *owned; /* private build, release after use */
} dash_mpd_resolve_t;

typedef struct {
  int status;
  char etag[48];
  const char *content_type;
  uint8_t *body;
  size_t body_len;
} dash_seg_resolve_t;
void iso8601_utc(time_t t, char *out, size_t outsz);
void mpd_avail_time(const hls_store_t *s, const hls_snapshot_t *snap, char *out, size_t outsz);
size_t xml_escaped_len(const char *s);
char *write_xml_escaped(char *dst, const char *s);
char *write_mpd_prolog(char *mp, const char *avail, const char *publish, double min_update, double tsb_depth, double pres_delay, double min_buffer, int want_ll, double part_target);
char *write_ll_resync(char *mp, const char *avail, const char *utc_url, double part_target, unsigned prt_id);
char *write_period_open(char *mp, const hls_snapshot_t *snap);
char *write_segment_template(char *mp, const char *mpd_start, size_t cap, const hls_store_t *s, const hls_snapshot_t *snap, const char *init, const char *media, const char *query, int ll);
uint64_t mpd_bandwidth(const hls_snapshot_t *snap);
char *write_audio_set(char *mp, const char *mpd_start, size_t cap, const mpd_fmt_ctx_t *ctx, const char *avail, int ll, unsigned as_id);
void mpd_codecs(const hls_snapshot_t *snap, char *codecs, size_t codecs_sz);

#endif
