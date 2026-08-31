/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include "lib/helper/ioutil.h"

#include <string.h>
#include <strings.h>

int find_header(const struct phr_header *headers, size_t num_headers, const char *name, char *out, size_t outsz) {
  size_t namelen = strlen(name);
  size_t i;
  for (i = 0; i < num_headers; i++) {
    const struct phr_header *h = &headers[i];
    size_t vlen;
    if (h->name_len != namelen || strncasecmp(h->name, name, namelen)) continue;
    vlen = h->value_len;
    if (vlen >= outsz) vlen = outsz - 1;
    memcpy(out, h->value, vlen);
    out[vlen] = '\0';
    return 1;
  }
  return 0;
}

const char *cors_match(const config_t *cfg, const char *origin_hdr, int *vary) {
  const char *p = cfg->cors_origins;
  *vary = 0;
  if (!p) return "*";
  while (*p) {
    const char *comma = strchr(p, ',');
    const char *start = p;
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    while (len && *start == ' ') {
      start++;
      len--;
    }
    while (len && start[len - 1] == ' ') len--;
    if (len == 1 && start[0] == '*') return "*";
    if (len && origin_hdr && strlen(origin_hdr) == len && !memcmp(origin_hdr, start, len)) {
      *vary = 1;
      return origin_hdr;
    }
    p = comma ? comma + 1 : p + strlen(p);
  }
  return NULL;
}

/* quoted If-None-Match, optionally prefixed (W/"x"). bare stored etags, strip both in place */
void strip_etag_quotes(char *v) {
  size_t len;
  if (!strncmp(v, "W/", 2)) memmove(v, v + 2, strlen(v + 2) + 1);
  len = strlen(v);
  if (len >= 2 && v[0] == '"' && v[len - 1] == '"') {
    memmove(v, v + 1, len - 2);
    v[len - 2] = '\0';
  }
}

/* shared with http2.c/http3_req.c: those transports only ever guard /ui/ws/'s CONNECT */
int http_auth_ok(const config_t *cfg, const char *auth_hdr) {
  if (!cfg->http_auth[0]) return 1;
  return auth_hdr && !strcmp(auth_hdr, cfg->http_auth);
}

int route_disabled(const route_t *rt) {
  const config_t *cfg = reactor_cfg();
  return (rt->kind == ROUTE_RTP && cfg->no_url_rtp) || (rt->kind == ROUTE_UDP && cfg->no_url_udp) ||
         (rt->kind == ROUTE_SRT && cfg->no_url_srt) || (rt->fmt == ROUTE_FMT_TS && cfg->no_ts) ||
         (rt->fmt == ROUTE_FMT_SPTS && cfg->no_spts) ||
         ((rt->fmt == ROUTE_FMT_HLS || rt->fmt == ROUTE_FMT_HLS_FMP4) && cfg->no_hls) ||
         (rt->fmt == ROUTE_FMT_LLHLS && cfg->no_llhls) || (rt->fmt == ROUTE_FMT_DASH && cfg->no_dash) ||
         (rt->fmt == ROUTE_FMT_RAWAUDIO && cfg->no_rawaudio);
}

/* -n-named source: ordinal of its -i, 0 if unmatched */
static unsigned source_ordinal_by_name(const config_t *cfg, const char *name) {
  int i;
  for (i = 0; i < cfg->n_sources; i++) if (cfg->sources[i].name && !strcmp(cfg->sources[i].name, name)) return (unsigned)cfg->sources[i].ordinal;
  return 0;
}

capture_ctx_t *open_source(const route_t *rt, unsigned *out_list_num) {
  int family, rtp;
  char addr[64];
  unsigned port;
  const config_t *cfg = reactor_cfg();
  switch (rt->kind) {
    case ROUTE_RTP:
    case ROUTE_UDP:
      return capture_open(rt->family, rt->addr, rt->port, cfg->iface, rt->kind == ROUTE_RTP, NULL, NULL);
    case ROUTE_SRT:
      return capture_open_srt(rt->addr, rt->port);
    case ROUTE_RIST:
      return capture_rist_get();
    case ROUTE_STDIN:
      return capture_stdin_get();
    case ROUTE_NAMED_BARE:
      if (cfg->rist_name && !strcmp(cfg->rist_name, rt->src_name))
        return capture_rist_get();
      if (cfg->stdin_name && !strcmp(cfg->stdin_name, rt->src_name))
        return capture_stdin_get();
      return NULL;
    case ROUTE_LIST_ITEM:
    case ROUTE_LIST_NAME: {
      capture_ctx_t *ctx;
      unsigned list_num = rt->list_num;
      if (rt->src_name[0]) {
        list_num = source_ordinal_by_name(cfg, rt->src_name);
        if (!list_num)
          return NULL;
      }
      if (out_list_num) *out_list_num = list_num;
      ctx = channels_resolve_static(reactor_channels(), list_num, rt->item_num, rt->kind == ROUTE_LIST_NAME ? rt->item_name : NULL);
      if (ctx) return ctx;
      {
        channel_ret_fcc_t rf;
        memset(&rf, 0, sizeof rf);
        if (channels_resolve(reactor_channels(), list_num, rt->item_num, rt->kind == ROUTE_LIST_NAME ? rt->item_name : NULL, &family, addr, sizeof addr, &port, &rtp, &rf))
          return NULL;
        return capture_open(family, addr, port, cfg->iface, rtp, rf.has_ret && !cfg->no_ret ? &rf.ret : NULL, rf.has_fcc && !cfg->no_fcc ? &rf.fcc : NULL);
      }
    }
  }
  return NULL;
}

void route_client_info(const route_t *rt, unsigned list_num, const pid_filter_t *filter, unsigned pmt_pid, const char *client_ip, int http_ver, route_item_bufs_t *bufs, client_info_t *out) {
  const config_t *cfg = reactor_cfg();
  int i;
  memset(out, 0, sizeof *out);
  out->ip = client_ip;
  out->http_ver = http_ver;
  out->fmt = rt->fmt;
  out->pmt_pid = pmt_pid;
  out->filter = filter;
  switch (rt->kind) {
    case ROUTE_RTP:
    case ROUTE_UDP:
    case ROUTE_SRT:
      out->src_proto = rt->kind == ROUTE_RTP ? "rtp" : rt->kind == ROUTE_UDP ? "udp" : "srt";
      {
        char portbuf[12];
        size_t off = bufcpy(bufs->addr, sizeof bufs->addr, rt->addr);
        off += bufcpy(bufs->addr + off, sizeof bufs->addr - off, ":");
        uint_to_str(portbuf, rt->port);
        bufcpy(bufs->addr + off, sizeof bufs->addr - off, portbuf);
      }
      out->src_addr = bufs->addr;
      return;
    case ROUTE_RIST:
      out->src_proto = "rist";
      out->src_ordinal = cfg->rist_ordinal;
      out->src_name = cfg->rist_name;
      return;
    case ROUTE_STDIN:
      out->src_proto = "stdin";
      out->src_ordinal = cfg->stdin_ordinal;
      out->src_name = cfg->stdin_name;
      return;
    case ROUTE_NAMED_BARE:
      if (cfg->rist_name && !strcmp(cfg->rist_name, rt->src_name)) {
        out->src_proto = "rist";
        out->src_ordinal = cfg->rist_ordinal;
        out->src_name = cfg->rist_name;
      } else if (cfg->stdin_name && !strcmp(cfg->stdin_name, rt->src_name)) {
        out->src_proto = "stdin";
        out->src_ordinal = cfg->stdin_ordinal;
        out->src_name = cfg->stdin_name;
      }
      return;
    case ROUTE_LIST_ITEM:
    case ROUTE_LIST_NAME:
      for (i = 0; i < cfg->n_sources; i++)
        if ((unsigned)cfg->sources[i].ordinal == list_num) {
          out->src_ordinal = list_num;
          out->src_name = cfg->sources[i].name;
          break;
        }
      if (channels_item_lookup(reactor_channels(), list_num, rt->item_num, rt->kind == ROUTE_LIST_NAME ? rt->item_name : NULL,
                               &out->item_num, bufs->name, sizeof bufs->name, bufs->proto, sizeof bufs->proto,
                               bufs->addr, sizeof bufs->addr) == 0) {
        out->item_name = bufs->name[0] ? bufs->name : NULL;
        out->src_proto = bufs->proto[0] ? bufs->proto : NULL;
        out->src_addr = bufs->addr[0] ? bufs->addr : NULL;
      }
      return;
  }
}

