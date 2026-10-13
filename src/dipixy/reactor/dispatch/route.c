/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/secure_zero.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int find_header(const struct phr_header *headers, size_t num_headers, const char *name, char *out, size_t outsz) {
  size_t namelen = strlen(name);
  for (size_t i = 0; i < num_headers; i++) {
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

typedef struct { const char *s; size_t len; } cors_span_t;

static pthread_once_t g_cors_once = PTHREAD_ONCE_INIT;
static const config_t *g_cors_cfg;
static cors_span_t *g_cors_list;
static int g_cors_n;
static int g_cors_all;

static void cors_parse_once(void) {
  const char *p = g_cors_cfg->cors_origins;
  int cap;
  if (!p) {
    g_cors_all = 1;
    return;
  }
  cap = 1;
  for (const char *q = p; *q; q++) {
    if (*q == ',') cap++;
  }
  g_cors_list = malloc((size_t)cap * sizeof *g_cors_list);
  if (!g_cors_list) {
    g_cors_all = 1;
    return;
  }
  while (*p) {
    const char *comma = strchr(p, ',');
    const char *start = p;
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    while (len && *start == ' ') {
      start++;
      len--;
    }
    while (len && start[len - 1] == ' ') len--;
    if (len == 1 && start[0] == '*') {
      g_cors_all = 1;
      free(g_cors_list);
      g_cors_list = NULL;
      g_cors_n = 0;
      return;
    }
    if (len) {
      g_cors_list[g_cors_n].s = start;
      g_cors_list[g_cors_n].len = len;
      g_cors_n++;
    }
    p = comma ? comma + 1 : p + strlen(p);
  }
}

const char *cors_match(const config_t *cfg, const char *origin_hdr, int *vary) {
  size_t olen;
  *vary = 0;
  g_cors_cfg = cfg;
  pthread_once(&g_cors_once, cors_parse_once);
  if (g_cors_all) return "*";
  if (!origin_hdr) return NULL;
  olen = strlen(origin_hdr);
  for (int i = 0; i < g_cors_n; i++) {
    if (g_cors_list[i].len == olen && !memcmp(g_cors_list[i].s, origin_hdr, olen)) {
      *vary = 1;
      return origin_hdr;
    }
  }
  return NULL;
}

typedef struct {
  char name[130]; /* lowercase, IPv6 keeps brackets */
  unsigned port;  /* 0 = absent */
} hostport_t;

static int hostport_parse(const char *s, hostport_t *out) {
  const char *p, *colon;
  size_t n;
  char *end;
  unsigned long port;
  if (!s[0] || strpbrk(s, "/?#@ ")) return 0;
  if (s[0] == '[') {
    p = strchr(s, ']');
    if (!p) return 0;
    colon = p[1] ? p + 1 : NULL;
    if (colon && *colon != ':') return 0;
    n = (size_t)(p - s) + 1;
  } else {
    colon = strchr(s, ':');
    if (colon && strchr(colon + 1, ':')) return 0;
    n = colon ? (size_t)(colon - s) : strlen(s);
    for (size_t i = 0; i < n; i++) {
      if (!isalnum((unsigned char)s[i]) && !strchr("-._", s[i])) return 0;
    }
  }
  if (n < 1 || n >= sizeof out->name) return 0;
  for (size_t i = 0; i < n; i++) out->name[i] = (char)tolower((unsigned char)s[i]);
  out->name[n] = '\0';
  out->port = 0;
  if (colon) {
    if (!isdigit((unsigned char)colon[1])) return 0;
    port = strtoul(colon + 1, &end, 10);
    if (*end || port < 1 || port > 65535) return 0;
    out->port = (unsigned)port;
  }
  return 1;
}

static int host_is_ip_literal(const char *name) {
  unsigned char buf[16];
  char v6[64];
  size_t n = strlen(name);
  if (name[0] != '[') return inet_pton(AF_INET, name, buf) == 1;
  if (n < 3 || n - 2 >= sizeof v6) return 0;
  memcpy(v6, name + 1, n - 2);
  v6[n - 2] = '\0';
  return inet_pton(AF_INET6, v6, buf) == 1;
}

/* comma list of host or scheme://host[:port] tokens. "*" matches only if star_all */
static int host_listed(const char *list, const char *name, int star_all) {
  while (list && *list) {
    const char *comma = strchr(list, ',');
    size_t len = comma ? (size_t)(comma - list) : strlen(list);
    const char *start = list;
    char tok[160];
    const char *sep;
    hostport_t hp;
    list = comma ? comma + 1 : NULL;
    while (len && *start == ' ') {
      start++;
      len--;
    }
    while (len && start[len - 1] == ' ') len--;
    if (!len || len >= sizeof tok) continue;
    memcpy(tok, start, len);
    tok[len] = '\0';
    if (tok[0] == '*' && !tok[1]) {
      if (star_all) return 1;
      continue;
    }
    sep = strstr(tok, "://");
    if (hostport_parse(sep ? sep + 3 : tok, &hp) && !strcmp(hp.name, name)) return 1;
  }
  return 0;
}

static int host_name_allowed(const config_t *cfg, const char *name) {
  if (host_is_ip_literal(name) || !strcmp(name, "localhost")) return 1;
  if (cfg->dlna_host[0] && host_listed(cfg->dlna_host, name, 0)) return 1;
  return host_listed(cfg->cors_origins, name, 0) || host_listed(cfg->ws_allowed_hosts, name, 1);
}

int ws_origin_ok(const config_t *cfg, const char *origin, const char *host) {
  hostport_t oh, hh;
  unsigned def_port;
  if (!origin || !origin[0]) return 1;
  if (!strncasecmp(origin, "http://", 7)) {
    origin += 7;
    def_port = 80;
  } else if (!strncasecmp(origin, "https://", 8)) {
    origin += 8;
    def_port = 443;
  } else {
    return 0;
  }
  if (!host || !hostport_parse(origin, &oh) || !hostport_parse(host, &hh)) return 0;
  if (!oh.port) oh.port = def_port;
  if (!hh.port) hh.port = def_port;
  if (oh.port != hh.port || strcmp(oh.name, hh.name)) return 0;
  return host_name_allowed(cfg, hh.name);
}

/* shared with http2.c/http3_req.c: those transports only ever guard /ui/ws/'s CONNECT */
int http_auth_ok(const char *expected, const char *auth_hdr) {
  const char *ep, *ap;
  size_t slen, elen;
  if (!expected[0]) return 1;
  if (!auth_hdr) return 0;
  ep = strchr(expected, ' ');
  slen = ep ? (size_t)(ep - expected) : strlen(expected);
  if (strncasecmp(auth_hdr, expected, slen)) return 0;
  ep = expected + slen;
  ap = auth_hdr + slen;
  if (*ap && *ap != ' ') return 0;
  while (*ep == ' ') ep++;
  while (*ap == ' ') ap++;
  elen = strlen(ep);
  return strlen(ap) == elen && secure_eq(ap, ep, elen);
}

int path_is_ui_ws(const char *path) {
  return !strcmp(path, "/ui/ws/") || !strcmp(path, "/ui/ws");
}

const char *special_path_auth(const char *path) {
  const config_t *cfg = reactor_cfg();
  if (!strcmp(path, "/") || !strcmp(path, "/index.html") || !strcmp(path, "/ui/status.js") || path_is_ui_ws(path)) return cfg->http_auth[0] ? cfg->http_auth : NULL;
  if (!strcmp(path, "/metrics")) return cfg->http_metrics_auth[0] ? cfg->http_metrics_auth : NULL;
  return NULL;
}

int route_disabled(const route_t *rt) {
  const config_t *cfg = reactor_cfg();
  const struct { route_kind_t kind; const int *flag; } kind_checks[] = {
{ROUTE_RTP, &cfg->no_url_rtp}, {ROUTE_UDP, &cfg->no_url_udp}, {ROUTE_SRT, &cfg->no_url_srt},
  };
  unsigned fmts = route_file_fmts(rt);
  size_t i;
  for (i = 0; i < sizeof kind_checks / sizeof kind_checks[0]; i++)
    if (rt->kind == kind_checks[i].kind && *kind_checks[i].flag) return 1;
  for (int f = ROUTE_FMT_TS; f <= ROUTE_FMT_MP4; f++)
    if ((fmts & 1u << f) && !playlist_fmt_disabled(cfg, (route_fmt_t)f)) return 0;
  return 1;
}

/* -n-named source: ordinal of its -i, 0 if unmatched */
static unsigned source_ordinal_by_name(const config_t *cfg, const char *name) {
  for (int i = 0; i < cfg->n_sources; i++) {
    if (cfg->sources[i].name && !strcmp(cfg->sources[i].name, name)) return (unsigned)cfg->sources[i].ordinal;
  }
  return 0;
}

typedef enum { OPEN_SPEC_CTX, OPEN_SPEC_MCAST, OPEN_SPEC_SRT, OPEN_SPEC_NONE } open_spec_kind_t;

typedef struct {
  open_spec_kind_t kind;
  capture_ctx_t *ctx; /* OPEN_SPEC_CTX: ref taken */
  int family;
  int rtp;
  char addr[64];
  char src[64];
  unsigned port;
  channel_ret_fcc_t rf;
} open_spec_t;

static void open_spec_resolve(const route_t *rt, unsigned *out_list_num, open_spec_t *sp) {
  const config_t *cfg = reactor_cfg();
  memset(sp, 0, sizeof *sp);
  sp->kind = OPEN_SPEC_NONE;
  switch (rt->kind) {
    case ROUTE_RTP:
    case ROUTE_UDP:
      sp->kind = OPEN_SPEC_MCAST;
      sp->family = rt->family;
      sp->rtp = rt->kind == ROUTE_RTP;
      bufcpy(sp->addr, sizeof sp->addr, rt->addr);
      bufcpy(sp->src, sizeof sp->src, rt->src);
      sp->port = rt->port;
      return;
    case ROUTE_SRT:
      sp->kind = OPEN_SPEC_SRT;
      bufcpy(sp->addr, sizeof sp->addr, rt->addr);
      sp->port = rt->port;
      return;
    case ROUTE_RIST:
      sp->ctx = capture_rist_get();
      break;
    case ROUTE_STDIN:
      sp->ctx = capture_stdin_get();
      break;
    case ROUTE_NAMED_BARE:
      if (cfg->rist_name && !strcmp(cfg->rist_name, rt->src_name)) sp->ctx = capture_rist_get();
      else if (cfg->stdin_name && !strcmp(cfg->stdin_name, rt->src_name)) sp->ctx = capture_stdin_get();
      break;
    case ROUTE_LIST_ITEM:
    case ROUTE_LIST_NAME: {
      unsigned list_num = rt->list_num;
      if (rt->src_name[0]) {
        list_num = source_ordinal_by_name(cfg, rt->src_name);
        if (!list_num) return;
      }
      if (out_list_num) *out_list_num = list_num;
      sp->ctx = channels_resolve_static(reactor_channels(), list_num, rt->item_num, rt->kind == ROUTE_LIST_NAME ? rt->item_name : NULL);
      if (sp->ctx) break;
      if (channels_resolve(reactor_channels(), list_num, rt->item_num, rt->kind == ROUTE_LIST_NAME ? rt->item_name : NULL, &sp->family, sp->addr, sizeof sp->addr, sp->src, sizeof sp->src, &sp->port,
                           &sp->rtp, &sp->rf))
        return;
      sp->kind = OPEN_SPEC_MCAST;
      return;
    }
  }
  if (sp->ctx) sp->kind = OPEN_SPEC_CTX;
}

static const sds_ret_t *open_spec_ret(const open_spec_t *sp) { return sp->rf.has_ret && !reactor_cfg()->no_ret ? &sp->rf.ret : NULL; }
static const sds_fcc_t *open_spec_fcc(const open_spec_t *sp) { return sp->rf.has_fcc && !reactor_cfg()->no_fcc ? &sp->rf.fcc : NULL; }
static const sds_fec_t *open_spec_fec(const open_spec_t *sp) { return sp->rf.has_fec && !reactor_cfg()->no_al_fec ? &sp->rf.fec : NULL; }

capture_ctx_t *open_source(const route_t *rt, unsigned *out_list_num) {
  const config_t *cfg = reactor_cfg();
  open_spec_t sp;
  open_spec_resolve(rt, out_list_num, &sp);
  switch (sp.kind) {
    case OPEN_SPEC_CTX:
      return sp.ctx;
    case OPEN_SPEC_MCAST:
      return capture_open(sp.family, sp.addr, sp.src, sp.port, cfg->iface, sp.rtp, open_spec_ret(&sp), open_spec_fcc(&sp), open_spec_fec(&sp), cfg->al_fec_l, cfg->al_fec_d);
    case OPEN_SPEC_SRT:
      return capture_open_srt(sp.addr, sp.port);
    case OPEN_SPEC_NONE:
      break;
  }
  return NULL;
}

open_res_t open_source_try(const route_t *rt, unsigned *out_list_num, capture_ctx_t **out_ctx, capture_open_job_t **out_job) {
  const config_t *cfg = reactor_cfg();
  open_spec_t sp;
  capture_ctx_t *ctx = NULL;
  capture_open_job_t *job = NULL;
  *out_ctx = NULL;
  *out_job = NULL;
  if (t_open_failed) {
    t_open_failed = 0;
    return OPEN_RES_FAIL;
  }
  open_spec_resolve(rt, out_list_num, &sp);
  switch (sp.kind) {
    case OPEN_SPEC_CTX:
      *out_ctx = sp.ctx;
      return OPEN_RES_OK;
    case OPEN_SPEC_MCAST:
      ctx = capture_try_ref(sp.family, sp.addr, sp.src, sp.port, cfg->iface);
      if (!ctx)
        job = capture_open_async(t_reactor_tid, sp.family, sp.addr, sp.src, sp.port, cfg->iface, sp.rtp, open_spec_ret(&sp), open_spec_fcc(&sp), open_spec_fec(&sp), cfg->al_fec_l, cfg->al_fec_d);
      break;
    case OPEN_SPEC_SRT:
      ctx = capture_try_ref_srt(sp.addr, sp.port);
      if (!ctx) job = capture_open_srt_async(t_reactor_tid, sp.addr, sp.port);
      break;
    case OPEN_SPEC_NONE:
      return OPEN_RES_FAIL;
  }
  if (job) {
    *out_job = job;
    return OPEN_RES_PENDING;
  }
  if (!ctx && capture_lifecycle_active()) return OPEN_RES_FAIL;
  if (!ctx) ctx = sp.kind == OPEN_SPEC_SRT ? capture_open_srt(sp.addr, sp.port)
                                           : capture_open(sp.family, sp.addr, sp.src, sp.port, cfg->iface, sp.rtp, open_spec_ret(&sp), open_spec_fcc(&sp), open_spec_fec(&sp), cfg->al_fec_l, cfg->al_fec_d);
  *out_ctx = ctx;
  return ctx ? OPEN_RES_OK : OPEN_RES_FAIL;
}

void route_client_info(const route_t *rt, unsigned list_num, const pid_filter_t *filter, unsigned pmt_pid, const char *client_ip, int http_ver, route_item_bufs_t *bufs, client_info_t *out) {
  const config_t *cfg = reactor_cfg();
  char portbuf[12];
  size_t off;
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
      off = 0;
      if (rt->src[0]) {
        off = bufcpy(bufs->addr, sizeof bufs->addr, rt->src);
        off += bufcpy(bufs->addr + off, sizeof bufs->addr - off, "@");
      }
      off += bufcpy(bufs->addr + off, sizeof bufs->addr - off, rt->addr);
      off += bufcpy(bufs->addr + off, sizeof bufs->addr - off, ":");
      uint_to_str(portbuf, rt->port);
      bufcpy(bufs->addr + off, sizeof bufs->addr - off, portbuf);
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
      for (int i = 0; i < cfg->n_sources; i++) {
        if ((unsigned)cfg->sources[i].ordinal == list_num) {
          out->src_ordinal = list_num;
          out->src_name = cfg->sources[i].name;
          break;
        }
      }
      if (channels_item_lookup(reactor_channels(), list_num, rt->item_num, rt->kind == ROUTE_LIST_NAME ? rt->item_name : NULL, &out->item_num, bufs->name,
         sizeof bufs->name, bufs->proto, sizeof bufs->proto, bufs->addr, sizeof bufs->addr) == 0) {
        out->item_name = bufs->name[0] ? bufs->name : NULL;
        out->src_proto = bufs->proto[0] ? bufs->proto : NULL;
        out->src_addr = bufs->addr[0] ? bufs->addr : NULL;
      }
      return;
  }
}

