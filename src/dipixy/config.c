/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "lib/config/yamlcfg.h"
#include "lib/mux/fec2022.h"
#include "config.h"
#include "version.h"

static struct {
  int active;
  int have;
} item;

void dixy_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  memset(&item, 0, sizeof item);
  dixy_cfg_listen(&cfg->listen, "all:9080");
  dixy_cfg_listen(&cfg->listen_tls, "all:9443");
  cfg->workers_spec = -1;
  cfg->max_clients = 256;
  cfg->max_channels = 32;
  cfg->capture_ring_kib = 4096;
  cfg->sds_timeout_s = 3.0;
  cfg->sds_refresh_interval_s = 30.0;
  cfg->segment_size = 3.0;
  cfg->segment_count = 4;
  cfg->hls_part_size = 0.35;
  cfg->dash_part_size = 0.333;
  cfg->dash_utc_url = "http://time.akamai.com/?iso&ms";
  cfg->hls_seg_pool = 8;
  cfg->ssdp_ttl = 3;
  cfg->ssdp_interval_s = 60.0;
  cfg->ssdp_max_age_s = 1800;
}

static int set_int(int *dst, const char *v, unsigned min, unsigned max, char *e, size_t n) {
  unsigned u;
  if (yamlcfg_set_uint(&u, v, min, max, e, n)) return -1;
  *dst = (int)u;
  return 0;
}

static int item_hook(void *c, const char *list, int begin, char *e, size_t n) {
  (void)c;
  (void)list;
  if (begin) {
    memset(&item, 0, sizeof item);
    item.active = 1;
    return 0;
  }
  item.active = 0;
  if (!item.have) {
    snprintf(e, n, "missing input");
    return -1;
  }
  return 0;
}

static int apply_input(void *c, const char *v, char *e, size_t n) {
  const char *val;
  if (yamlcfg_set_str(&val, v, e, n)) return -1;
  if (dixy_cfg_add_input(c, val, e, n)) return -1;
  if (item.active) item.have = 1;
  return 0;
}

static int apply_input_name(void *c, const char *v, char *e, size_t n) {
  const char *val;
  if (!item.active) {
    snprintf(e, n, "only valid inside an input list item");
    return -1;
  }
  if (yamlcfg_set_str(&val, v, e, n)) return -1;
  return dixy_cfg_set_name(c, val, e, n);
}

static int apply_input_media_type(void *c, const char *v, char *e, size_t n) {
  if (!item.active) {
    snprintf(e, n, "only valid inside an input list item");
    return -1;
  }
  return dixy_cfg_set_media_type(c, v, e, n);
}

static int apply_iface(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->iface, v, e, n);
}

static int apply_listen(void *c, const char *v, char *e, size_t n) {
  if (dixy_cfg_listen(&((config_t *)c)->listen, v)) {
    snprintf(e, n, "invalid '%s' (all:<port> or <addr>:<port>)", v);
    return -1;
  }
  return 0;
}

static int apply_listen_tls(void *c, const char *v, char *e, size_t n) {
  if (dixy_cfg_listen(&((config_t *)c)->listen_tls, v)) {
    snprintf(e, n, "invalid '%s' (all:<port> or <addr>:<port>)", v);
    return -1;
  }
  return 0;
}

static int apply_tls_cert(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->tls_cert, v, e, n);
}

static int apply_tls_key(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->tls_key, v, e, n);
}

static int apply_workers(void *c, const char *v, char *e, size_t n) {
  if (dixy_cfg_workers(&((config_t *)c)->workers_spec, v)) {
    snprintf(e, n, "invalid '%s' (-1/-2/-3, or a positive count)", v);
    return -1;
  }
  return 0;
}

static int apply_max_clients(void *c, const char *v, char *e, size_t n) {
  return set_int(&((config_t *)c)->max_clients, v, 1, 65536, e, n);
}

static int apply_max_channels(void *c, const char *v, char *e, size_t n) {
  return set_int(&((config_t *)c)->max_channels, v, 1, 1024, e, n);
}

static int apply_idle_timeout(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->idle_timeout_s, v, 0, 86400, e, n);
}

static int apply_capture_ring_size(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->capture_ring_kib, v, 1, UINT_MAX, e, n);
}

static int apply_join_all(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->join_all, v, e, n);
}

static int apply_insecure(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->insecure_tls, v, e, n);
}

static int apply_sds_timeout(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_double(&((config_t *)c)->sds_timeout_s, v, 0.0, 1e9, 1, e, n);
}

static int apply_sds_refresh_interval(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_double(&((config_t *)c)->sds_refresh_interval_s, v, 0.0, 1e9, 1, e, n);
}

static int apply_segment_size(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_double(&((config_t *)c)->segment_size, v, 2.0, 1e9, 0, e, n);
}

static int apply_segment_count(void *c, const char *v, char *e, size_t n) {
  return set_int(&((config_t *)c)->segment_count, v, 3, 1000, e, n);
}

static int apply_hls_part_size(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_double(&((config_t *)c)->hls_part_size, v, 0.05, 5.0, 0, e, n);
}

static int apply_dash_part_size(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_double(&((config_t *)c)->dash_part_size, v, 0.05, 5.0, 0, e, n);
}

static int apply_dash_utc_url(void *c, const char *v, char *e, size_t n) {
  if (strlen(v) > 256) {
    snprintf(e, n, "too long (max 256)");
    return -1;
  }
  return yamlcfg_set_str(&((config_t *)c)->dash_utc_url, v, e, n);
}

static int apply_hls_seg_pool(void *c, const char *v, char *e, size_t n) {
  return set_int(&((config_t *)c)->hls_seg_pool, v, 1, INT_MAX, e, n);
}

static int apply_metrics_sock(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->metrics_sock, v, e, n);
}

static int apply_metrics_id(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->metrics_id, v, e, n);
}

static int apply_metrics_interval(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->metrics_interval_s, v, 1, 86400, e, n);
}

static int apply_metrics_http(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->metrics_http, v, e, n);
}

static int apply_metrics_auth(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return dixy_cfg_auth(v, cfg->http_metrics_auth, sizeof cfg->http_metrics_auth, e, n);
}

static int apply_format(void *c, const char *v, char *e, size_t n) {
  if (dixy_cfg_format(c, v)) {
    snprintf(e, n, "invalid '%s' (comma-separated list of ts,spts,rawaudio,hls,llhls,dash,lldash)", v);
    return -1;
  }
  return 0;
}

static int apply_no_url_rtp(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_url_rtp, v, e, n);
}

static int apply_no_url_udp(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_url_udp, v, e, n);
}

static int apply_no_url_srt(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_url_srt, v, e, n);
}

static int apply_no_pid_filters(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_pid_filters, v, e, n);
}

static int apply_no_lcevc(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_lcevc, v, e, n);
}

static int apply_no_http2(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_http2, v, e, n);
}

static int apply_no_http3(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_http3, v, e, n);
}

static int apply_no_fcc(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_fcc, v, e, n);
}

static int apply_no_ret(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_ret, v, e, n);
}

static int apply_al_fec(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (fec2022_parse_ld(v, &cfg->al_fec_l, &cfg->al_fec_d)) {
    snprintf(e, n, "invalid '%s' (want L:D, L*D<=400, L<=40)", v);
    return -1;
  }
  return 0;
}

static int apply_no_al_fec(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_al_fec, v, e, n);
}

static int apply_no_status(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_status, v, e, n);
}

static int apply_status_tpl(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->status_template, v, e, n);
}

static int apply_auth(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return dixy_cfg_auth(v, cfg->http_auth, sizeof cfg->http_auth, e, n);
}

static int apply_cors_origin(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->cors_origins, v, e, n);
}

static int apply_ssdp_ttl(void *c, const char *v, char *e, size_t n) {
  return set_int(&((config_t *)c)->ssdp_ttl, v, 1, 255, e, n);
}

static int apply_ssdp_iface(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->ssdp_iface, v, e, n);
}

static int apply_ssdp_interval(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_double(&((config_t *)c)->ssdp_interval_s, v, 0.0, 1e9, 1, e, n);
}

static int apply_ssdp_max_age(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->ssdp_max_age_s, v, 1, UINT_MAX, e, n);
}

static int apply_enable_dlna(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->enable_dlna, v, e, n);
}

static int apply_dlna_host(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->dlna_host_opt, v, e, n);
}

static int apply_dlna_name(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->dlna_name, v, e, n);
}

static int apply_dlna_keep_multicast(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->dlna_keep_multicast, v, e, n);
}

static int apply_daemonize(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->daemonize, v, e, n);
}

static int apply_verbose(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->verbose, v, e, n);
}

static int apply_color(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_color(&((config_t *)c)->color_mode, v, e, n);
}

static const yamlcfg_key_t keys[] = {
  {"iface", apply_iface, 0, 0},
  {"listen", apply_listen, 0, 0},
  {"listen-tls", apply_listen_tls, 0, 0},
  {"tls.cert", apply_tls_cert, 1, 0},
  {"tls.key", apply_tls_key, 1, 0},
  {"workers", apply_workers, 0, 0},
  {"max-clients", apply_max_clients, 0, 0},
  {"max-channels", apply_max_channels, 0, 0},
  {"idle-timeout", apply_idle_timeout, 0, 0},
  {"capture-ring-size", apply_capture_ring_size, 0, 0},
  {"input", apply_input, 0, YAMLCFG_LIST_KEYED},
  {"input.name", apply_input_name, 0, 0},
  {"input.media-type", apply_input_media_type, 0, 0},
  {"join-all", apply_join_all, 0, 0},
  {"insecure", apply_insecure, 0, 0},
  {"sds.timeout", apply_sds_timeout, 0, 0},
  {"sds.refresh-interval", apply_sds_refresh_interval, 0, 0},
  {"segment-size", apply_segment_size, 0, 0},
  {"segment-count", apply_segment_count, 0, 0},
  {"hls.part-size", apply_hls_part_size, 0, 0},
  {"hls.seg-pool", apply_hls_seg_pool, 0, 0},
  {"dash.part-size", apply_dash_part_size, 0, 0},
  {"dash.utc-url", apply_dash_utc_url, 0, 0},
  {"metrics.sock", apply_metrics_sock, 0, 0},
  {"metrics.id", apply_metrics_id, 0, 0},
  {"metrics.interval", apply_metrics_interval, 0, 0},
  {"metrics.http", apply_metrics_http, 0, 0},
  {"metrics.auth", apply_metrics_auth, 0, 0},
  {"format", apply_format, 0, 0},
  {"no.url-rtp", apply_no_url_rtp, 0, 0},
  {"no.url-udp", apply_no_url_udp, 0, 0},
  {"no.url-srt", apply_no_url_srt, 0, 0},
  {"no.pid-filters", apply_no_pid_filters, 0, 0},
  {"no.lcevc", apply_no_lcevc, 0, 0},
  {"no.http2", apply_no_http2, 0, 0},
  {"no.http3", apply_no_http3, 0, 0},
  {"no.fcc", apply_no_fcc, 0, 0},
  {"no.ret", apply_no_ret, 0, 0},
  {"al-fec", apply_al_fec, 0, 0},
  {"no.al-fec", apply_no_al_fec, 0, 0},
  {"no.status", apply_no_status, 0, 0},
  {"status-tpl", apply_status_tpl, 1, 0},
  {"auth", apply_auth, 0, 0},
  {"cors-origin", apply_cors_origin, 0, 0},
  {"ssdp.ttl", apply_ssdp_ttl, 0, 0},
  {"ssdp.iface", apply_ssdp_iface, 0, 0},
  {"ssdp.interval", apply_ssdp_interval, 0, 0},
  {"ssdp.max-age", apply_ssdp_max_age, 0, 0},
  {"enable-dlna", apply_enable_dlna, 0, 0},
  {"dlna.host", apply_dlna_host, 0, 0},
  {"dlna.name", apply_dlna_name, 0, 0},
  {"dlna.keep-multicast", apply_dlna_keep_multicast, 0, 0},
  {"daemonize", apply_daemonize, 0, 0},
  {"verbose", apply_verbose, 0, 0},
  {"color", apply_color, 0, 0},
};

static int load(yamlcfg_t *y, int check, config_t *cfg, const char *path) {
  int rc = yamlcfg_load_items(y, TOOL_NAME, check, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg, item_hook);
  cfg->last_input = LAST_NONE;
  cfg->media_type_seen = 0;
  return rc;
}

int dixy_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  return load(&y, strict ? YAMLCFG_STRICT : 0, cfg, path) == YAMLCFG_ERROR ? -1 : 0;
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

int dixy_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  config_t cfg;
  int rc;

  dixy_cfg_defaults(&cfg);
  rc = load(&y, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, &cfg, path);
  if (rc != YAMLCFG_LOADED) {
    args_free(&cfg);
    return -1;
  }
  warn_if(&y, cfg.tls_cert && !cfg.tls_key, "tls.cert given without tls.key");
  warn_if(&y, cfg.tls_key && !cfg.tls_cert, "tls.key given without tls.cert");
  warn_if(&y, cfg.hls_part_size >= cfg.segment_size, "hls.part-size must be smaller than segment-size");
  warn_if(&y, cfg.dash_part_size >= cfg.segment_size, "dash.part-size must be smaller than segment-size");
  warn_if(&y, (double)cfg.ssdp_max_age_s < 2.0 * cfg.ssdp_interval_s, "ssdp.max-age must be at least 2x ssdp.interval");
  warn_if(&y, (cfg.metrics_sock || cfg.metrics_interval_s) && !cfg.metrics_id, "metrics.sock and metrics.interval require metrics.id");
  warn_if(&y, cfg.enable_dlna && cfg.no_spts, "enable-dlna requires spts in format");
  warn_if(&y, cfg.enable_dlna && cfg.no_rawaudio, "enable-dlna requires rawaudio in format");
  warn_if(&y, cfg.enable_dlna && !cfg.dlna_host_opt && cfg.listen.scope == LISTEN_ANY, "enable-dlna needs dlna.host or a concrete listen address, not 'all'");
  rc = yamlcfg_report(&y);
  args_free(&cfg);
  return rc;
}
