/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/helper/argutil.h"

#include "priv.h"


/* host[:port], IPv6 host in brackets. port optional, default 80 */
int scan_http_proxy_parse(const char *s, config_t *cfg) {
  const char *p = s;
  size_t len;
  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close) return -1;
    len = (size_t)(close - (p + 1));
    if (len == 0 || len >= sizeof cfg->http_proxy_host) return -1;
    memcpy(cfg->http_proxy_host, p + 1, len);
    cfg->http_proxy_host[len] = '\0';
    p = close + 1;
  } else {
    const char *hp = p;
    while (*hp && *hp != ':') hp++;
    len = (size_t)(hp - p);
    if (len == 0 || len >= sizeof cfg->http_proxy_host) return -1;
    memcpy(cfg->http_proxy_host, p, len);
    cfg->http_proxy_host[len] = '\0';
    p = hp;
  }

  if (*p == ':') return argutil_port_parse(p + 1, &cfg->http_proxy_port);
  if (*p != '\0') return -1;
  cfg->http_proxy_port = 80;
  return 0;
}

/* %g (group), %p (port), %% (literal %). anything else after % is invalid */
int scan_http_path_tmpl_valid(const char *t) {
  while (*t) {
    size_t step = 1;
    if (*t == '%') {
      if (t[1] != 'g' && t[1] != 'p' && t[1] != '%') return -1;
      step = 2;
    }
    t += step;
  }
  return 0;
}

int scan_fmt_from_name(const char *s, out_fmt_t *f) {
  static const enum_map_t map[] = {{"m3u", OUT_M3U}, {"csv", OUT_CSV}, {"xspf", OUT_XSPF}, {"xml", OUT_XML}, {"null", OUT_NULL}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v)) return -1;
  *f = (out_fmt_t)v;
  return 0;
}
int scan_cfg_format(config_t *cfg, const char *s) {
  return scan_fmt_from_name(s, &cfg->format);
}

int scan_cfg_http_proxy(config_t *cfg, const char *s) {
  if (scan_http_proxy_parse(s, cfg)) return -1;
  cfg->http_proxy = 1;
  return 0;
}

int scan_cfg_http_path(const char *s) {
  if (scan_http_path_tmpl_valid(s)) return -1;
  return 0;
}
