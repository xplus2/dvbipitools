/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/strutil.h"
#include "lib/helper/uriparse.h"

#include "route.h"

#define MAX_SEGS 8

int fmt_parse(const char *s, route_fmt_t *out) {
  static const enum_map_t map[] = {
{"ts", ROUTE_FMT_TS}, {"spts", ROUTE_FMT_SPTS}, {"hls", ROUTE_FMT_HLS}, {"hls-fmp4", ROUTE_FMT_HLS_FMP4},
{"llhls", ROUTE_FMT_LLHLS}, {"llhls-fmp4", ROUTE_FMT_LLHLS_FMP4}, {"dash", ROUTE_FMT_DASH}, {"lldash", ROUTE_FMT_LLDASH},
{"rawaudio", ROUTE_FMT_RAWAUDIO}, {"mp4", ROUTE_FMT_MP4},
  };
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v)) return -1;
  *out = (route_fmt_t)v;
  return 0;
}

static int dec_u64(const char **p, uint64_t max, uint64_t *out) {
  const char *s = *p;
  uint64_t v = 0;
  if (*s < '0' || *s > '9') return -1;
  for (; *s >= '0' && *s <= '9'; s++) {
    if (v > (max - (uint64_t)(*s - '0')) / 10) return -1;
    v = v * 10 + (uint64_t)(*s - '0');
  }
  *p = s;
  *out = v;
  return 0;
}

static int seg_ext(const char *s, int *fmp4) {
  if (!strcmp(s, ".ts")) *fmp4 = 0;
  else if (!strcmp(s, ".m4s")) *fmp4 = 1;
  else return -1;
  return 0;
}

int route_file_parse(const char *s, route_file_t *out) {
  static const enum_map_t fixed[] = {
      {"index.m3u8", ROUTE_FILE_INDEX}, {"index_ll.m3u8", ROUTE_FILE_INDEX_LL}, {"manifest.mpd", ROUTE_FILE_MPD}, {"init.mp4", ROUTE_FILE_INIT},
      {"dinitv.mp4", ROUTE_FILE_DINIT}, {"dinita.mp4", ROUTE_FILE_DINIT},
  };
  uint64_t v;
  int k;
  memset(out, 0, sizeof *out);
  if (!map_lookup(fixed, sizeof fixed / sizeof fixed[0], s, &k)) {
    out->kind = (route_file_kind_t)k;
    if (k == ROUTE_FILE_DINIT) out->track = s[5] == 'v' ? ROUTE_TRACK_VIDEO : ROUTE_TRACK_AUDIO;
    return 0;
  }
  if (!strncmp(s, "dseg", 4)) {
    s += 4;
    if (*s == 'v' || *s == 'a') out->track = *s++ == 'v' ? ROUTE_TRACK_VIDEO : ROUTE_TRACK_AUDIO;
    if (dec_u64(&s, UINT64_MAX, &out->t_ms) || strcmp(s, ".m4s")) return -1;
    out->kind = ROUTE_FILE_DSEG;
    return 0;
  }
  if (strncmp(s, "seg", 3)) return -1;
  s += 3;
  if (dec_u64(&s, UINT32_MAX, &v)) return -1;
  out->seq = (uint32_t)v;
  out->kind = ROUTE_FILE_SEG;
  if (*s == '.' && s[1] >= '0' && s[1] <= '9') {
    s++;
    if (dec_u64(&s, INT_MAX, &v)) return -1;
    out->part = (int)v;
    out->kind = ROUTE_FILE_PART;
  }
  return seg_ext(s, &out->fmp4);
}

unsigned route_file_fmts(const route_t *rt) {
  const route_file_t *f = &rt->file;
  unsigned own = 1u << rt->fmt;
  if (rt->fmt == ROUTE_FMT_HLS && f->kind == ROUTE_FILE_SEG) return own | 1u << ROUTE_FMT_LLHLS;
  if ((rt->fmt == ROUTE_FMT_DASH || rt->fmt == ROUTE_FMT_LLDASH) && f->kind != ROUTE_FILE_MPD) return 1u << ROUTE_FMT_DASH | 1u << ROUTE_FMT_LLDASH;
  if (rt->fmt == ROUTE_FMT_HLS_FMP4 && f->kind == ROUTE_FILE_INIT) return own | 1u << ROUTE_FMT_DASH | 1u << ROUTE_FMT_LLHLS_FMP4;
  if (rt->fmt == ROUTE_FMT_HLS_FMP4 && f->kind == ROUTE_FILE_SEG) return own | 1u << ROUTE_FMT_LLHLS_FMP4;
  return own;
}

/* playlist-referenced segment/init filenames carry no format token */
static int fmt_or_hls_file_parse(const char *s, route_t *out) {
  if (!route_file_parse(s, &out->file)) {
    switch (out->file.kind) {
      case ROUTE_FILE_INDEX_LL: out->fmt = ROUTE_FMT_LLHLS; return 0;
      case ROUTE_FILE_PART: out->fmt = out->file.fmp4 ? ROUTE_FMT_LLHLS_FMP4 : ROUTE_FMT_LLHLS; return 0;
      case ROUTE_FILE_SEG: out->fmt = out->file.fmp4 ? ROUTE_FMT_HLS_FMP4 : ROUTE_FMT_HLS; return 0;
      case ROUTE_FILE_INIT: out->fmt = ROUTE_FMT_HLS_FMP4; return 0;
      case ROUTE_FILE_DSEG:
      case ROUTE_FILE_DINIT: out->fmt = ROUTE_FMT_DASH; return 0;
      default: out->file.kind = ROUTE_FILE_NONE; return -1;
    }
  }
  if (fmt_parse(s, &out->fmt)) return -1;
  if (out->fmt == ROUTE_FMT_HLS || out->fmt == ROUTE_FMT_HLS_FMP4) out->file.kind = ROUTE_FILE_INDEX;
  else if (out->fmt == ROUTE_FMT_LLHLS || out->fmt == ROUTE_FMT_LLHLS_FMP4) out->file.kind = ROUTE_FILE_INDEX_LL;
  else if (out->fmt == ROUTE_FMT_DASH || out->fmt == ROUTE_FMT_LLDASH) out->file.kind = ROUTE_FILE_MPD;
  return 0;
}

static const char *const route_reserved_names[] = {"rtp", "udp", "srt", "rist", "stdin", "list", "metrics", "ui", "api", "dlna", "export", "time"};

int route_name_valid(const char *name) {
  if (!name || !*name || strlen(name) > ROUTE_NAME_MAX) return 0;
  if (strchr(name, '/') || name[0] == '.') return 0;
  for (size_t i = 0; i < sizeof route_reserved_names / sizeof route_reserved_names[0]; i++)
    if (!strcmp(name, route_reserved_names[i])) return 0;
  return 1;
}

static int parse_trailing_fmt(const char *fmt_seg, route_t *out) {
  if (fmt_seg) return fmt_or_hls_file_parse(fmt_seg, out);
  out->fmt = ROUTE_FMT_TS;
  return 0;
}

static int uint_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v;
  if (*s == '\0' || !isdigit((unsigned char)*s)) return -1;
  errno = 0;
  v = strtoul(s, &end, 10);
  if (*end != '\0' || errno == ERANGE || v > UINT_MAX) return -1;
  *out = (unsigned)v;
  return 0;
}

static int parse_item_tail(const char *item_seg, const char *fmt_seg, route_t *out) {
  if (uint_parse(item_seg, &out->item_num) || out->item_num == 0) return -1;
  if (parse_trailing_fmt(fmt_seg, out)) return -1;
  out->kind = ROUTE_LIST_ITEM;
  return 0;
}

static int parse_name_tail(char *name_seg, const char *fmt_seg, route_t *out) {
  if (uriparse_pct_decode(name_seg)) return -1;
  if (strlen(name_seg) >= sizeof out->item_name) return -1;
  bufcpy(out->item_name, sizeof out->item_name, name_seg);
  if (parse_trailing_fmt(fmt_seg, out)) return -1;
  out->kind = ROUTE_LIST_NAME;
  return 0;
}

int route_resolve_channel_uri(const char *uri, int *family, char *addr, size_t addrsz, char *src, size_t srcsz, unsigned *port, int *rtp) {
  const char *rest;
  char srcbuf[64];
  if (!strncmp(uri, "rtp://", 6)) {
    *rtp = 1;
    rest = uri + 6;
  } else if (!strncmp(uri, "udp://", 6)) {
    *rtp = 0;
    rest = uri + 6;
  } else {
    return -1;
  }
  if (!src) {
    src = srcbuf;
    srcsz = sizeof srcbuf;
  }
  return uriparse_mcast_src_addrport(rest, family, addr, addrsz, port, src, srcsz);
}

/* unlike route_resolve_channel_uri, host may be a DNS name not just numeric */
int route_parse_srt_uri(const char *uri, char *host, size_t hostsz, unsigned *port) {
  const char *rest;
  const char *colon;
  int family;
  size_t len;

  if (strncmp(uri, "srt://", 6)) return -1;
  rest = uri + 6;
  if (*rest == '@') rest++;
  if (*rest == '\0') return -1;
  if (!argutil_addrport_parse(rest, &family, host, hostsz, port)) return 0;
  if (*rest == '[') return -1;
  colon = strrchr(rest, ':');
  if (!colon || colon == rest) return -1;
  len = (size_t)(colon - rest);
  if (len >= hostsz) return -1;
  memcpy(host, rest, len);
  host[len] = '\0';
  return argutil_port_parse(colon + 1, port);
}

int route_parse(const char *path, route_t *out) {
  char buf[512];
  char *seg[MAX_SEGS];
  int nseg = 0;
  char *save;
  static const enum_map_t kind_map[] = {{"rtp", ROUTE_RTP}, {"udp", ROUTE_UDP}, {"srt", ROUTE_SRT}};
  int kind;

  if (!path || path[0] != '/') return -1;
  if (strlen(path + 1) >= sizeof buf) return -1;
  bufcpy(buf, sizeof buf, path + 1);
  for (char *p = strtok_r(buf, "/", &save); p && nseg < MAX_SEGS; p = strtok_r(NULL, "/", &save))
    seg[nseg++] = p;

  if (nseg > 0 && uriparse_pct_decode(seg[0])) return -1;
  memset(out, 0, sizeof *out);
  if ((nseg == 3 || nseg == 2) && !map_lookup(kind_map, sizeof kind_map / sizeof kind_map[0], seg[0], &kind)) {
    int family;
    unsigned port;
    if (kind == ROUTE_SRT || !strchr(seg[1], '@')) {
      if (argutil_addrport_parse(seg[1], &family, out->addr, sizeof out->addr, &port)) return -1;
    } else if (uriparse_mcast_src_addrport(seg[1], &family, out->addr, sizeof out->addr, &port, out->src, sizeof out->src)) {
      return -1;
    }
    if (parse_trailing_fmt(nseg == 3 ? seg[2] : NULL, out)) return -1;
    out->kind = (route_kind_t)kind;
    out->family = family;
    out->port = port;
    return 0;
  }

  if ((nseg == 2 || nseg == 1) && (!strcmp(seg[0], "rist") || !strcmp(seg[0], "stdin"))) {
    if (parse_trailing_fmt(nseg == 2 ? seg[1] : NULL, out)) return -1;
    out->kind = !strcmp(seg[0], "rist") ? ROUTE_RIST : ROUTE_STDIN;
    return 0;
  }
  if ((nseg == 5 || nseg == 4) && !strcmp(seg[0], "list") && !strcmp(seg[2], "item")) {
    if (uint_parse(seg[1], &out->list_num) || out->list_num == 0) return -1;
    return parse_item_tail(seg[3], nseg == 5 ? seg[4] : NULL, out);
  }

  if ((nseg == 5 || nseg == 4) && !strcmp(seg[0], "list") && !strcmp(seg[2], "name")) {
    if (uint_parse(seg[1], &out->list_num) || out->list_num == 0) return -1;
    return parse_name_tail(seg[3], nseg == 5 ? seg[4] : NULL, out);
  }

  if (nseg >= 1 && nseg <= 4 && route_name_valid(seg[0])) {
    if (nseg == 1) {
      out->fmt = ROUTE_FMT_TS;
      out->kind = ROUTE_NAMED_BARE;
      bufcpy(out->src_name, sizeof out->src_name, seg[0]);
      return 0;
    }
    if (nseg == 2) {
      if (fmt_or_hls_file_parse(seg[1], out)) return -1;
      out->kind = ROUTE_NAMED_BARE;
      bufcpy(out->src_name, sizeof out->src_name, seg[0]);
      return 0;
    }
    if (!strcmp(seg[1], "item")) {
      if (parse_item_tail(seg[2], nseg == 4 ? seg[3] : NULL, out)) return -1;
      bufcpy(out->src_name, sizeof out->src_name, seg[0]);
      return 0;
    }
    if (!strcmp(seg[1], "name")) {
      if (parse_name_tail(seg[2], nseg == 4 ? seg[3] : NULL, out)) return -1;
      bufcpy(out->src_name, sizeof out->src_name, seg[0]);
      return 0;
    }
  }
  return -1;
}
