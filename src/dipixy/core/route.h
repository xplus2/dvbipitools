/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_ROUTE_H
#define DIPIXY_ROUTE_H

#include <stddef.h>

typedef enum {
  ROUTE_FMT_TS,
  ROUTE_FMT_SPTS,
  ROUTE_FMT_HLS,
  ROUTE_FMT_HLS_FMP4,
  ROUTE_FMT_LLHLS,
  ROUTE_FMT_DASH,
  ROUTE_FMT_LLDASH,
  ROUTE_FMT_RAWAUDIO
} route_fmt_t;
typedef enum {
  ROUTE_RTP,
  ROUTE_UDP,
  ROUTE_SRT,
  ROUTE_RIST,
  ROUTE_STDIN,
  ROUTE_LIST_ITEM,
  ROUTE_LIST_NAME,
  ROUTE_NAMED_BARE /* -n-named -i - or -i rist://, addressed as /<name>/<fmt> */
} route_kind_t;

#define ROUTE_NAME_MAX 63 /* -n/--name value, also route_t.src_name capacity - 1 */

typedef struct {
  route_kind_t kind;
  route_fmt_t fmt;
  int family;              /* AF_INET or AF_INET6. ROUTE_RTP/ROUTE_UDP only */
  char addr[64];           /* ROUTE_RTP/ROUTE_UDP/ROUTE_SRT only */
  unsigned port;           /* ROUTE_RTP/ROUTE_UDP/ROUTE_SRT only */
  unsigned list_num;       /* ROUTE_LIST_*, 1-based, from URL. 0 if addressed via src_name instead */
  char src_name[ROUTE_NAME_MAX + 1]; /* ROUTE_NAMED_BARE, or ROUTE_LIST_* addressed by -n name instead of list_num */
  unsigned item_num;       /* ROUTE_LIST_ITEM, 1-based, from URL */
  char item_name[128];     /* ROUTE_LIST_NAME, percent-decoded */
  char hls_file[32];       /* ROUTE_FMT_HLS: "index.m3u8" or "segNNNNN.ts".
                              ROUTE_FMT_HLS_FMP4: "index.m3u8", "init.mp4", or "segNNNNN.m4s".
                              ROUTE_FMT_LLHLS: "index_ll.m3u8" or "segNNNNN.PP.ts".
                              ROUTE_FMT_DASH/ROUTE_FMT_LLDASH: "manifest.mpd" or "dsegTTTT.m4s" (TTTT: start time, ms).
                              dseg/init: no format (playlist): always resolve to ROUTE_FMT_DASH */
} route_t;

/* path starts with '/'. 0 ok, -1 no match or malformed */
int route_parse(const char *path, route_t *out);

int fmt_parse(const char *s, route_fmt_t *out);

/* uri like "rtp://@239.0.0.1:8000" or "udp://@[ff0e::1]:8000" from channel_item_t.uri. 0 ok, -1 malformed */
int route_resolve_channel_uri(const char *uri, int *family, char *addr, size_t addrsz, unsigned *port, int *rtp);

/* uri like "srt://host:port" from channel_item_t.uri, host numeric or DNS name. 0 ok, -1 malformed */
int route_parse_srt_uri(const char *uri, char *host, size_t hostsz, unsigned *port);

/* -n/--name validation. 1 ok, 0 invalid */
int route_name_valid(const char *name);

#endif
