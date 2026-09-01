/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/uriparse.h"

#include "route.h"

#define MAX_SEGS 8

static int fmt_parse(const char *s, route_fmt_t *out) {
  if (!strcmp(s, "ts")) {
    *out = ROUTE_FMT_TS;
    return 0;
  }
  if (!strcmp(s, "spts")) {
    *out = ROUTE_FMT_SPTS;
    return 0;
  }
  if (!strcmp(s, "hls")) {
    *out = ROUTE_FMT_HLS;
    return 0;
  }
  if (!strcmp(s, "hls-fmp4")) {
    *out = ROUTE_FMT_HLS_FMP4;
    return 0;
  }
  if (!strcmp(s, "llhls")) {
    *out = ROUTE_FMT_LLHLS;
    return 0;
  }
  if (!strcmp(s, "dash")) {
    *out = ROUTE_FMT_DASH;
    return 0;
  }
  if (!strcmp(s, "lldash")) {
    *out = ROUTE_FMT_LLDASH;
    return 0;
  }
  if (!strcmp(s, "rawaudio")) {
    *out = ROUTE_FMT_RAWAUDIO;
    return 0;
  }
  return -1;
}

static int hexval(int c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/* %XX -> byte, decoded in place. 0 ok, -1 malformed escape */
static int pct_decode(char *s) {
  char *w = s;
  while (*s) {
    if (*s == '%') {
      int hi = hexval((unsigned char)s[1]);
      int lo = hi >= 0 ? hexval((unsigned char)s[2]) : -1;
      if (lo < 0)
        return -1;
      *w++ = (char)((hi << 4) | lo);
      s += 3;
    } else {
      *w++ = *s++;
    }
  }
  *w = '\0';
  return 0;
}

static int hls_ts_seg_filename(const char *s) {
  size_t i;
  if (strncmp(s, "seg", 3) != 0)
    return 0;
  for (i = 3; s[i] >= '0' && s[i] <= '9'; i++)
    ;
  return i > 3 && !strcmp(s + i, ".ts");
}

static int hls_fmp4_seg_filename(const char *s) {
  size_t i;
  if (strncmp(s, "seg", 3) != 0)
    return 0;
  for (i = 3; s[i] >= '0' && s[i] <= '9'; i++)
    ;
  return i > 3 && !strcmp(s + i, ".m4s");
}

/* "dsegTTTT.m4s": DASH media segment, addressed by start time (ms). NOT like hls-fmp4 */
static int dash_seg_filename(const char *s) {
  size_t i;
  if (strncmp(s, "dseg", 4) != 0)
    return 0;
  for (i = 4; s[i] >= '0' && s[i] <= '9'; i++)
    ;
  return i > 4 && !strcmp(s + i, ".m4s");
}

/* "segNNNNN.PP.ts": LL-HLS part, distinct from plain "segNNNNN.ts" */
static int llhls_part_filename(const char *s) {
  size_t i;
  if (strncmp(s, "seg", 3) != 0)
    return 0;
  for (i = 3; s[i] >= '0' && s[i] <= '9'; i++)
    ;
  if (i == 3 || s[i] != '.')
    return 0;
  i++;
  if (s[i] < '0' || s[i] > '9')
    return 0;
  for (; s[i] >= '0' && s[i] <= '9'; i++)
    ;
  return !strcmp(s + i, ".ts");
}

/* playlist-referenced segment/init filenames carry no format token */
static int fmt_or_hls_file_parse(const char *s, route_t *out) {
  if (llhls_part_filename(s) || !strcmp(s, "index_ll.m3u8")) {
    out->fmt = ROUTE_FMT_LLHLS;
    bufcpy(out->hls_file, sizeof out->hls_file, s);
    return 0;
  }
  if (hls_ts_seg_filename(s)) {
    out->fmt = ROUTE_FMT_HLS;
    bufcpy(out->hls_file, sizeof out->hls_file, s);
    return 0;
  }
  if (hls_fmp4_seg_filename(s) || !strcmp(s, "init.mp4")) {
    out->fmt = ROUTE_FMT_HLS_FMP4;
    bufcpy(out->hls_file, sizeof out->hls_file, s);
    return 0;
  }
  if (dash_seg_filename(s)) {
    out->fmt = ROUTE_FMT_DASH;
    bufcpy(out->hls_file, sizeof out->hls_file, s);
    return 0;
  }
  if (fmt_parse(s, &out->fmt))
    return -1;
  if (out->fmt == ROUTE_FMT_HLS || out->fmt == ROUTE_FMT_HLS_FMP4)
    bufcpy(out->hls_file, sizeof out->hls_file, "index.m3u8");
  else if (out->fmt == ROUTE_FMT_LLHLS)
    bufcpy(out->hls_file, sizeof out->hls_file, "index_ll.m3u8");
  else if (out->fmt == ROUTE_FMT_DASH || out->fmt == ROUTE_FMT_LLDASH)
    bufcpy(out->hls_file, sizeof out->hls_file, "manifest.mpd");
  return 0;
}

static const char *const route_reserved_names[] = {"rtp", "udp", "srt", "rist", "stdin", "list", "metrics", "ui", "api", "dlna"};

int route_name_valid(const char *name) {
  if (!name || !*name || strlen(name) > ROUTE_NAME_MAX)
    return 0;
  if (strchr(name, '/') || name[0] == '.')
    return 0;
  for (size_t i = 0; i < sizeof route_reserved_names / sizeof route_reserved_names[0]; i++)
    if (!strcmp(name, route_reserved_names[i]))
      return 0;
  return 1;
}

static int uint_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v;
  if (*s == '\0' || !isdigit((unsigned char)*s))
    return -1;
  v = strtoul(s, &end, 10);
  if (*end != '\0')
    return -1;
  *out = (unsigned)v;
  return 0;
}

int route_resolve_channel_uri(const char *uri, int *family, char *addr, size_t addrsz, unsigned *port, int *rtp) {
  const char *rest;
  if (!strncmp(uri, "rtp://", 6)) {
    *rtp = 1;
    rest = uri + 6;
  } else if (!strncmp(uri, "udp://", 6)) {
    *rtp = 0;
    rest = uri + 6;
  } else {
    return -1;
  }
  if (*rest == '@')
    rest++;
  return uriparse_mcast_addrport(rest, family, addr, addrsz, port);
}

/* unlike route_resolve_channel_uri, host may be a DNS name not just numeric */
int route_parse_srt_uri(const char *uri, char *host, size_t hostsz, unsigned *port) {
  const char *rest, *colon;
  int family;
  size_t len;

  if (strncmp(uri, "srt://", 6))
    return -1;
  rest = uri + 6;
  if (*rest == '@')
    rest++;
  if (*rest == '\0')
    return -1;

  if (!argutil_addrport_parse(rest, &family, host, hostsz, port))
    return 0;
  if (*rest == '[')
    return -1;

  colon = strrchr(rest, ':');
  if (!colon || colon == rest)
    return -1;
  len = (size_t)(colon - rest);
  if (len >= hostsz)
    return -1;
  memcpy(host, rest, len);
  host[len] = '\0';
  return argutil_port_parse(colon + 1, port);
}

int route_parse(const char *path, route_t *out) {
  char buf[512];
  char *seg[MAX_SEGS];
  int nseg = 0;
  char *save;

  if (!path || path[0] != '/')
    return -1;
  if (strlen(path + 1) >= sizeof buf)
    return -1;
  bufcpy(buf, sizeof buf, path + 1);

  for (char *p = strtok_r(buf, "/", &save); p && nseg < MAX_SEGS; p = strtok_r(NULL, "/", &save))
    seg[nseg++] = p;

  if (nseg > 0 && pct_decode(seg[0]))
    return -1;

  memset(out, 0, sizeof *out);

  if ((nseg == 3 || nseg == 2) && (!strcmp(seg[0], "rtp") || !strcmp(seg[0], "udp") || !strcmp(seg[0], "srt"))) {
    int family;
    unsigned port;
    if (argutil_addrport_parse(seg[1], &family, out->addr, sizeof out->addr, &port))
      return -1;
    if (nseg == 3) {
      if (fmt_or_hls_file_parse(seg[2], out))
        return -1;
    } else {
      out->fmt = ROUTE_FMT_TS; /* no format segment: default to raw TS push */
    }
    if (!strcmp(seg[0], "rtp"))
      out->kind = ROUTE_RTP;
    else if (!strcmp(seg[0], "udp"))
      out->kind = ROUTE_UDP;
    else
      out->kind = ROUTE_SRT;
    out->family = family;
    out->port = port;
    return 0;
  }

  if ((nseg == 2 || nseg == 1) && (!strcmp(seg[0], "rist") || !strcmp(seg[0], "stdin"))) {
    if (nseg == 2) {
      if (fmt_or_hls_file_parse(seg[1], out))
        return -1;
    } else {
      out->fmt = ROUTE_FMT_TS;
    }
    out->kind = !strcmp(seg[0], "rist") ? ROUTE_RIST : ROUTE_STDIN;
    return 0;
  }

  if ((nseg == 5 || nseg == 4) && !strcmp(seg[0], "list") && !strcmp(seg[2], "item")) {
    if (uint_parse(seg[1], &out->list_num) || out->list_num == 0)
      return -1;
    if (uint_parse(seg[3], &out->item_num) || out->item_num == 0)
      return -1;
    if (nseg == 5) {
      if (fmt_or_hls_file_parse(seg[4], out))
        return -1;
    } else {
      out->fmt = ROUTE_FMT_TS;
    }
    out->kind = ROUTE_LIST_ITEM;
    return 0;
  }

  if ((nseg == 5 || nseg == 4) && !strcmp(seg[0], "list") && !strcmp(seg[2], "name")) {
    if (uint_parse(seg[1], &out->list_num) || out->list_num == 0)
      return -1;
    if (pct_decode(seg[3]))
      return -1;
    if (strlen(seg[3]) >= sizeof out->item_name)
      return -1;
    bufcpy(out->item_name, sizeof out->item_name, seg[3]);
    if (nseg == 5) {
      if (fmt_or_hls_file_parse(seg[4], out))
        return -1;
    } else {
      out->fmt = ROUTE_FMT_TS;
    }
    out->kind = ROUTE_LIST_NAME;
    return 0;
  }

  if (nseg >= 1 && nseg <= 4 && route_name_valid(seg[0])) {
    if (nseg == 1) {
      out->fmt = ROUTE_FMT_TS;
      out->kind = ROUTE_NAMED_BARE;
      bufcpy(out->src_name, sizeof out->src_name, seg[0]);
      return 0;
    }
    if (nseg == 2) {
      if (fmt_or_hls_file_parse(seg[1], out))
        return -1;
      out->kind = ROUTE_NAMED_BARE;
      bufcpy(out->src_name, sizeof out->src_name, seg[0]);
      return 0;
    }
    if (!strcmp(seg[1], "item")) {
      if (uint_parse(seg[2], &out->item_num) || out->item_num == 0)
        return -1;
      if (nseg == 4) {
        if (fmt_or_hls_file_parse(seg[3], out))
          return -1;
      } else {
        out->fmt = ROUTE_FMT_TS;
      }
      out->kind = ROUTE_LIST_ITEM;
      bufcpy(out->src_name, sizeof out->src_name, seg[0]);
      return 0;
    }
    if (!strcmp(seg[1], "name")) {
      if (pct_decode(seg[2]))
        return -1;
      if (strlen(seg[2]) >= sizeof out->item_name)
        return -1;
      bufcpy(out->item_name, sizeof out->item_name, seg[2]);
      if (nseg == 4) {
        if (fmt_or_hls_file_parse(seg[3], out))
          return -1;
      } else {
        out->fmt = ROUTE_FMT_TS;
      }
      out->kind = ROUTE_LIST_NAME;
      bufcpy(out->src_name, sizeof out->src_name, seg[0]);
      return 0;
    }
  }
  return -1;
}
