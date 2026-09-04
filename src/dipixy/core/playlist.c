/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "playlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/playlist_out.h"
#include "lib/helper/uriparse.h"
#include "../version.h"

int playlist_path_parse(const char *path, route_fmt_t *fmt, playlist_type_t *ptype) {
  char buf[64];
  char *slash;
  if (!path || strncmp(path, "/export/", 8))
    return -1;
  if (strlen(path + 8) >= sizeof buf)
    return -1;
  bufcpy(buf, sizeof buf, path + 8);
  slash = strchr(buf, '/');
  if (!slash)
    return -1;
  *slash = '\0';
  if (strchr(slash + 1, '/'))
    return -1;
  if (fmt_parse(buf, fmt))
    return -1;
  if (!strcmp(slash + 1, "m3u"))
    *ptype = PLAYLIST_M3U;
  else if (!strcmp(slash + 1, "xspf"))
    *ptype = PLAYLIST_XSPF;
  else
    return -1;
  return 0;
}

int playlist_fmt_disabled(const config_t *cfg, route_fmt_t fmt) {
  switch (fmt) {
    case ROUTE_FMT_TS:       return cfg->no_ts;
    case ROUTE_FMT_SPTS:     return cfg->no_spts;
    case ROUTE_FMT_RAWAUDIO: return cfg->no_rawaudio;
    case ROUTE_FMT_HLS:
    case ROUTE_FMT_HLS_FMP4: return cfg->no_hls;
    case ROUTE_FMT_LLHLS:    return cfg->no_llhls;
    case ROUTE_FMT_DASH:     return cfg->no_dash;
    case ROUTE_FMT_LLDASH:   return cfg->no_lldash;
  }
  return 1;
}

int playlist_query_has_flag(const char *query, const char *name) {
  size_t namelen;
  const char *p;
  if (!query) return 0;
  namelen = strlen(name);
  for (p = query; (p = strstr(p, name)) != NULL; p += namelen) {
    char after = p[namelen];
    if ((p == query || p[-1] == '&') && (after == '\0' || after == '&' || after == '='))
      return 1;
  }
  return 0;
}

static const char *fmt_str(route_fmt_t fmt) {
  switch (fmt) {
    case ROUTE_FMT_TS:       return "ts";
    case ROUTE_FMT_SPTS:     return "spts";
    case ROUTE_FMT_RAWAUDIO: return "rawaudio";
    case ROUTE_FMT_HLS:      return "hls";
    case ROUTE_FMT_HLS_FMP4: return "hls-fmp4";
    case ROUTE_FMT_LLHLS:    return "llhls";
    case ROUTE_FMT_DASH:     return "dash";
    case ROUTE_FMT_LLDASH:   return "lldash";
  }
  return "ts";
}

static void pct_encode_seg(const char *s, char *out, size_t outcap) {
  static const char *unreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
  size_t oi = 0;
  for (; *s && oi + 1 < outcap; s++) {
    if (strchr(unreserved, *s)) {
      out[oi++] = *s;
    } else {
      if (oi + 4 > outcap) break;
      snprintf(out + oi, 4, "%%%02X", (unsigned char)*s);
      oi += 3;
    }
  }
  out[oi] = '\0';
}

static int has_port_suffix(const char *h) {
  const char *close = strchr(h, ']');
  return close ? strchr(close, ':') != NULL : strchr(h, ':') != NULL;
}

static void host_strip_port(char *h) {
  char *close = strchr(h, ']');
  char *colon = close ? strchr(close, ':') : strrchr(h, ':');
  if (colon) *colon = '\0';
}

static void resolve_hostport(const config_t *cfg, int is_tls, const char *host_hdr, const char *query,
                              char *out, size_t outsz) {
  char qhost[128];
  char hostname[128];
  unsigned port = is_tls ? cfg->listen_tls.port : cfg->listen.port;

  if (query_param_extract(query, "host=", qhost, sizeof qhost)) {
    if (has_port_suffix(qhost)) {
      bufcpy(out, outsz, qhost);
      return;
    }
    bufcpy(hostname, sizeof hostname, qhost);
  } else if (host_hdr) {
    bufcpy(hostname, sizeof hostname, host_hdr);
    host_strip_port(hostname);
  } else {
    bufcpy(hostname, sizeof hostname, "127.0.0.1");
  }
  snprintf(out, outsz, "%s:%u", hostname, port);
}

static int ordinal_included(const char *csv, unsigned ord) {
  const char *p = csv;
  if (!csv || !*csv) return 1;
  while (p) {
    char *end;
    unsigned long v = strtoul(p, &end, 10);
    if (end != p && v == ord)
      return 1;
    p = strchr(p, ',');
    if (p) p++;
  }
  return 0;
}

static void append_filter(char *target, size_t targetsz, size_t used, const pid_filter_t *filter) {
  char filterbuf[128];
  if (!filter || !filter->count || used >= targetsz)
    return;
  pid_filter_format(filter, filterbuf, sizeof filterbuf);
  snprintf(target + used, targetsz - used, "?filter=%s", filterbuf);
}

static void emit_out(FILE *f, playlist_type_t ptype, const char *name, const char *target, const char *icon_uri, unsigned tsid, unsigned onid, unsigned sid) {
  if (ptype == PLAYLIST_M3U)
    playlist_out_m3u_item(f, name, target, icon_uri, tsid, onid, sid);
  else
    playlist_out_xspf_item(f, name, target, icon_uri, tsid, onid, sid);
}

static void emit_singleton(FILE *f, playlist_type_t ptype, route_fmt_t fmt, const char *scheme, const char *hostport, const pid_filter_t *filter, const char *kind, const char *name) {
  char target[300];
  char name_enc[192];
  int n;
  if (name) {
    pct_encode_seg(name, name_enc, sizeof name_enc);
    n = snprintf(target, sizeof target, "%s://%s/%s/%s", scheme, hostport, name_enc, fmt_str(fmt));
  } else {
    n = snprintf(target, sizeof target, "%s://%s/%s/%s", scheme, hostport, kind, fmt_str(fmt));
  }
  append_filter(target, sizeof target, n < 0 ? sizeof target : (size_t)n, filter);
  emit_out(f, ptype, name ? name : kind, target, NULL, 0, 0, 0);
}

typedef struct {
  FILE *f;
  playlist_type_t ptype;
  route_fmt_t fmt;
  const char *scheme;
  const char *hostport;
  const pid_filter_t *filter;
  int keep_multicast;
  unsigned ordinal;
  const char *src_name;
  int item_num;
} emit_ctx_t;

static void emit_item(void *vctx, const channel_item_t *item) {
  emit_ctx_t *rc = vctx;
  char target[600];
  int family, rtp_flag;
  char maddr[64];
  unsigned mport;
  int n;
  rc->item_num++;
  if (rc->keep_multicast && item->uri && !route_resolve_channel_uri(item->uri, &family, maddr, sizeof maddr, &mport, &rtp_flag)) {
    char hostport[80];
    uriparse_mcast_describe(family, maddr, mport, hostport, sizeof hostport);
    snprintf(target, sizeof target, "%s://%s", rtp_flag ? "rtp" : "udp", hostport);
  } else {
    char name_enc[192];
    if (rc->src_name) {
      pct_encode_seg(rc->src_name, name_enc, sizeof name_enc);
      n = snprintf(target, sizeof target, "%s://%s/%s/item/%d/%s", rc->scheme, rc->hostport, name_enc, rc->item_num, fmt_str(rc->fmt));
    } else {
      n = snprintf(target, sizeof target, "%s://%s/list/%u/item/%d/%s", rc->scheme, rc->hostport, rc->ordinal, rc->item_num, fmt_str(rc->fmt));
    }
    append_filter(target, sizeof target, n < 0 ? sizeof target : (size_t)n, rc->filter);
  }
  emit_out(rc->f, rc->ptype, item->name, target, item->icon_uri, item->tsid, item->onid, item->sid);
}

int playlist_render(const config_t *cfg, const channels_t *ch, int is_tls, const char *host_hdr, const char *query, const pid_filter_t *filter,
                    route_fmt_t fmt, playlist_type_t ptype, char **out, size_t *out_len) {
  FILE *f;
  char hostport[160];
  char input_csv[256];
  const char *scheme = is_tls ? "https" : "http";
  const char *input_filter;
  int keep_multicast = playlist_query_has_flag(query, "keep_multicast");
  int max_ord, si;

  resolve_hostport(cfg, is_tls, host_hdr, query, hostport, sizeof hostport);
  input_filter = query_param_extract(query, "input=", input_csv, sizeof input_csv) ? input_csv : NULL;
  f = open_memstream(out, out_len);
  if (!f) return -1;
  playlist_out_init(f, ptype == PLAYLIST_M3U ? PLAYLIST_OUT_M3U : PLAYLIST_OUT_XSPF, TOOL_NAME " " TOOL_VERSION, "dipixy ");
  max_ord = cfg->stdin_ordinal;
  if (cfg->rist_ordinal > max_ord) max_ord = cfg->rist_ordinal;
  if (cfg->n_sources > 0 && cfg->sources[cfg->n_sources - 1].ordinal > max_ord) max_ord = cfg->sources[cfg->n_sources - 1].ordinal;
  si = 0;
  for (int ord = 1; ord <= max_ord; ord++) {
    if (ord == cfg->stdin_ordinal) {
      if (ordinal_included(input_filter, (unsigned)ord))
        emit_singleton(f, ptype, fmt, scheme, hostport, filter, "stdin", cfg->stdin_name);
    } else if (ord == cfg->rist_ordinal) {
      if (ordinal_included(input_filter, (unsigned)ord))
        emit_singleton(f, ptype, fmt, scheme, hostport, filter, "rist", cfg->rist_name);
    } else if (si < cfg->n_sources && cfg->sources[si].ordinal == ord) {
      if (ordinal_included(input_filter, (unsigned)ord)) {
        emit_ctx_t rc = {0};
        rc.f = f;
        rc.ptype = ptype;
        rc.fmt = fmt;
        rc.scheme = scheme;
        rc.hostport = hostport;
        rc.filter = filter;
        rc.keep_multicast = keep_multicast;
        rc.ordinal = (unsigned)ord;
        rc.src_name = cfg->sources[si].name;
        channels_list_for_each(ch, (unsigned)ord, emit_item, &rc);
      }
      si++;
    }
  }
  playlist_out_close(f, ptype == PLAYLIST_M3U ? PLAYLIST_OUT_M3U : PLAYLIST_OUT_XSPF);
  fclose(f);
  return 0;
}
