/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "status.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/jsonbuf.h"
#include "lib/helper/signal.h"

#include "../ts/capture/capture.h"
#include "../ts/channels/channels.h"
#include "../reactor/reactor.h"
#include "../reactor/reactor_tls.h"
#include "../version.h"

static int g_argc;
static char **g_argv;
static time_t g_start_unix;

void dipixy_status_init(int argc, char **argv) {
  g_argc = argc;
  g_argv = argv;
  g_start_unix = time(NULL);
}

static _Atomic uint64_t g_rate_bits;
static uint64_t g_prev_in, g_prev_out;
static double g_prev_t = -1.0;

void dipixy_status_tick(void) {
  double now = mono_seconds();
  uint64_t in_bytes = capture_bytes_total();
  uint64_t out_bytes = reactor_bytes_served_total();
  double dt = g_prev_t > 0.0 ? now - g_prev_t : 0.0;

  if (dt > 0.0) {
    uint64_t din = in_bytes > g_prev_in ? in_bytes - g_prev_in : 0;
    uint64_t dout = out_bytes > g_prev_out ? out_bytes - g_prev_out : 0;
    double in_mbps = (double)din * 8.0 / dt / 1e6;
    double out_mbps = (double)dout * 8.0 / dt / 1e6;
    uint32_t in_scaled = (uint32_t)(in_mbps * 1000.0 + 0.5);
    uint32_t out_scaled = (uint32_t)(out_mbps * 1000.0 + 0.5);
    atomic_store_explicit(&g_rate_bits, ((uint64_t)in_scaled << 32) | out_scaled, memory_order_relaxed);
  }
  g_prev_in = in_bytes;
  g_prev_out = out_bytes;
  g_prev_t = now;
}

static void status_bitrate(double *in_mbps, double *out_mbps) {
  uint64_t bits = atomic_load_explicit(&g_rate_bits, memory_order_relaxed);
  *in_mbps = (double)(bits >> 32) / 1000.0;
  *out_mbps = (double)(bits & 0xffffffffu) / 1000.0;
}

static uint64_t status_rss_bytes(void) {
  FILE *f = fopen("/proc/self/statm", "r");
  long total_pages = 0, rss_pages = 0;
  if (!f)
    return 0;
  if (fscanf(f, "%ld %ld", &total_pages, &rss_pages) != 2)
    rss_pages = 0;
  fclose(f);
  return (uint64_t)rss_pages * (uint64_t)sysconf(_SC_PAGESIZE);
}

int dipixy_status_render_json(const config_t *cfg, char **out, size_t *out_len) {
  static _Thread_local jbuf_t j;
  struct tm tmv;
  jbuf_reset(&j);
  char start_str[32];
  double in_mbps, out_mbps;
  int i;

  jbuf_str(&j, "{");

  jbuf_key(&j, "tool");
  jbuf_json_string(&j, TOOL_NAME);
  jbuf_str(&j, ",");

  jbuf_key(&j, "version");
  jbuf_json_string(&j, TOOL_VERSION);
  jbuf_str(&j, ",");

  jbuf_key(&j, "build");
  jbuf_str(&j, "{");
  jbuf_key(&j, "type");
  jbuf_json_string(&j, BUILD_TYPE);
  jbuf_str(&j, ",");
  jbuf_key(&j, "arch");
  jbuf_json_string(&j, BUILD_ARCH);
  jbuf_str(&j, ",");
  jbuf_key(&j, "link");
  jbuf_json_string(&j, BUILD_LINK);
  jbuf_str(&j, ",");
  jbuf_key(&j, "features");
  jbuf_str(&j, "{");
  jbuf_key(&j, "tls");
#ifdef HAVE_TLS
  jbuf_str(&j, "true");
#else
  jbuf_str(&j, "false");
#endif
  jbuf_str(&j, ",");
  jbuf_key(&j, "http2");
#ifdef HAVE_HTTP2
  jbuf_str(&j, "true");
#else
  jbuf_str(&j, "false");
#endif
  jbuf_str(&j, ",");
  jbuf_key(&j, "http3");
#ifdef HAVE_HTTP3
  jbuf_str(&j, "true");
#else
  jbuf_str(&j, "false");
#endif
  jbuf_str(&j, "}"); /* features */
  jbuf_str(&j, "}"); /* build */
  jbuf_str(&j, ",");

  gmtime_r(&g_start_unix, &tmv);
  strftime(start_str, sizeof start_str, "%Y-%m-%dT%H:%M:%SZ", &tmv);
  jbuf_key(&j, "start_time");
  jbuf_json_string(&j, start_str);
  jbuf_str(&j, ",");
  jbuf_key(&j, "start_time_unix");
  jbuf_fmt(&j, "%lld", (long long)g_start_unix);
  jbuf_str(&j, ",");
  jbuf_key(&j, "uptime_seconds");
  jbuf_fmt(&j, "%lld", (long long)(time(NULL) - g_start_unix));
  jbuf_str(&j, ",");

  jbuf_key(&j, "exec_args");
  jbuf_str(&j, "[");
  for (i = 0; i < g_argc; i++) {
    if (i) jbuf_str(&j, ",");
    jbuf_json_string(&j, g_argv[i]);
  }
  jbuf_str(&j, "],");

  jbuf_key(&j, "threads");
  jbuf_str(&j, "{");
  jbuf_key(&j, "workers");
  jbuf_fmt(&j, "%d", reactor_worker_count());
  jbuf_str(&j, ",");
  jbuf_key(&j, "pump");
  jbuf_str(&j, "1,");
  jbuf_key(&j, "channels_refresh");
  jbuf_fmt(&j, "%d", channels_refresh_active());
  jbuf_str(&j, ",");
  jbuf_key(&j, "total");
  jbuf_fmt(&j, "%d", reactor_worker_count() + 1 + channels_refresh_active());
  jbuf_str(&j, "},");

  jbuf_key(&j, "memory");
  jbuf_str(&j, "{");
  jbuf_key(&j, "rss_bytes");
  jbuf_fmt(&j, "%llu", (unsigned long long)status_rss_bytes());
  jbuf_str(&j, "},");

  status_bitrate(&in_mbps, &out_mbps);
  jbuf_key(&j, "bitrate");
  jbuf_str(&j, "{");
  jbuf_key(&j, "in_mbps");
  jbuf_fmt(&j, "%.3f", in_mbps);
  jbuf_str(&j, ",");
  jbuf_key(&j, "out_mbps");
  jbuf_fmt(&j, "%.3f", out_mbps);
  jbuf_str(&j, "},");

  jbuf_key(&j, "listen");
  jbuf_str(&j, "{");
  jbuf_key(&j, "addr");
  jbuf_json_string(&j, cfg->listen.scope == LISTEN_ANY ? "all" : cfg->listen.addr);
  jbuf_str(&j, ",");
  jbuf_key(&j, "port");
  jbuf_fmt(&j, "%u", cfg->listen.port);
  jbuf_str(&j, ",");
  jbuf_key(&j, "tls_addr");
  jbuf_json_string(&j, cfg->listen_tls.scope == LISTEN_ANY ? "all" : cfg->listen_tls.addr);
  jbuf_str(&j, ",");
  jbuf_key(&j, "tls_port");
  jbuf_fmt(&j, "%u", cfg->listen_tls.port);
  jbuf_str(&j, "},");

  jbuf_key(&j, "server");
  jbuf_str(&j, "{");
  jbuf_key(&j, "workers_spec");
  jbuf_fmt(&j, "%d", cfg->workers_spec);
  jbuf_str(&j, ",");
  jbuf_key(&j, "max_clients");
  jbuf_fmt(&j, "%d", cfg->max_clients);
  jbuf_str(&j, ",");
  jbuf_key(&j, "capture_ring_kib");
  jbuf_fmt(&j, "%u", cfg->capture_ring_kib);
  jbuf_str(&j, "},");

  jbuf_key(&j, "segment");
  jbuf_str(&j, "{");
  jbuf_key(&j, "size_s");
  jbuf_fmt(&j, "%.3f", cfg->segment_size);
  jbuf_str(&j, ",");
  jbuf_key(&j, "count");
  jbuf_fmt(&j, "%d", cfg->segment_count);
  jbuf_str(&j, ",");
  jbuf_key(&j, "hls_part_size_s");
  jbuf_fmt(&j, "%.3f", cfg->hls_part_size);
  jbuf_str(&j, ",");
  jbuf_key(&j, "dash_part_size_s");
  jbuf_fmt(&j, "%.3f", cfg->dash_part_size);
  jbuf_str(&j, ",");
  jbuf_key(&j, "hls_seg_pool");
  jbuf_fmt(&j, "%d", cfg->hls_seg_pool);
  jbuf_str(&j, "},");

  jbuf_key(&j, "sds");
  jbuf_str(&j, "{");
  jbuf_key(&j, "timeout_s");
  jbuf_fmt(&j, "%.3f", cfg->sds_timeout_s);
  jbuf_str(&j, ",");
  jbuf_key(&j, "refresh_interval_s");
  jbuf_fmt(&j, "%.3f", cfg->sds_refresh_interval_s);
  jbuf_str(&j, "},");

  jbuf_key(&j, "metrics");
  jbuf_str(&j, "{");
  jbuf_key(&j, "enabled");
  jbuf_str(&j, cfg->metrics_id ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "id");
  if (cfg->metrics_id)
    jbuf_json_string(&j, cfg->metrics_id);
  else
    jbuf_str(&j, "null");
  jbuf_str(&j, ",");
  jbuf_key(&j, "interval_s");
  jbuf_fmt(&j, "%u", cfg->metrics_interval_s);
  jbuf_str(&j, ",");
  jbuf_key(&j, "http");
  jbuf_str(&j, cfg->metrics_http ? "true" : "false");
  jbuf_str(&j, "},");

  jbuf_key(&j, "cors_origins");
  jbuf_json_string(&j, cfg->cors_origins ? cfg->cors_origins : "*");
  jbuf_str(&j, ",");

  jbuf_key(&j, "auth_enabled");
  jbuf_str(&j, cfg->http_auth[0] ? "true" : "false");
  jbuf_str(&j, ",");

  jbuf_key(&j, "tls");
  jbuf_str(&j, "{");
  if (tls_is_running()) {
    tls_cert_detail_t d;
    tls_cert_detail(NULL, 0, &d);
    jbuf_key(&j, "enabled");
    jbuf_str(&j, "true,");
    jbuf_key(&j, "cn");
    jbuf_json_string(&j, d.cn);
    jbuf_str(&j, ",");
    jbuf_key(&j, "aliases");
    jbuf_str(&j, "[");
    for (i = 0; i < d.alias_count; i++) {
      if (i) jbuf_str(&j, ",");
      jbuf_json_string(&j, d.aliases[i]);
    }
    jbuf_str(&j, "],");
    jbuf_key(&j, "expiry");
    jbuf_json_string(&j, d.valid_to);
  } else {
    jbuf_key(&j, "enabled");
    jbuf_str(&j, "false");
  }
  jbuf_str(&j, "}"); /* tls */
  jbuf_str(&j, ",");

  jbuf_key(&j, "dlna");
  jbuf_str(&j, "{");
  jbuf_key(&j, "enabled");
  jbuf_str(&j, cfg->enable_dlna ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "ssdp_ttl");
  jbuf_fmt(&j, "%d", cfg->ssdp_ttl);
  jbuf_str(&j, ",");
  jbuf_key(&j, "ssdp_iface");
  if (cfg->ssdp_iface)
    jbuf_json_string(&j, cfg->ssdp_iface);
  else
    jbuf_str(&j, "null");
  jbuf_str(&j, ",");
  jbuf_key(&j, "dlna_host");
  if (cfg->enable_dlna)
    jbuf_json_string(&j, cfg->dlna_host);
  else
    jbuf_str(&j, "null");
  jbuf_str(&j, ",");
  jbuf_key(&j, "dlna_name");
  if (cfg->enable_dlna && cfg->dlna_name)
    jbuf_json_string(&j, cfg->dlna_name);
  else
    jbuf_str(&j, "null");
  jbuf_str(&j, ",");
  jbuf_key(&j, "keep_multicast");
  jbuf_str(&j, cfg->dlna_keep_multicast ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "ssdp_interval_s");
  jbuf_fmt(&j, "%.3f", cfg->ssdp_interval_s);
  jbuf_str(&j, ",");
  jbuf_key(&j, "ssdp_max_age_s");
  jbuf_fmt(&j, "%u", cfg->ssdp_max_age_s);
  jbuf_str(&j, "},");

  jbuf_key(&j, "flags");
  jbuf_str(&j, "{");
  jbuf_key(&j, "no_hls");
  jbuf_str(&j, cfg->no_hls ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_llhls");
  jbuf_str(&j, cfg->no_llhls ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_dash");
  jbuf_str(&j, cfg->no_dash ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_lldash");
  jbuf_str(&j, cfg->no_lldash ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_ts");
  jbuf_str(&j, cfg->no_ts ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_spts");
  jbuf_str(&j, cfg->no_spts ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_rawaudio");
  jbuf_str(&j, cfg->no_rawaudio ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_url_rtp");
  jbuf_str(&j, cfg->no_url_rtp ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_url_udp");
  jbuf_str(&j, cfg->no_url_udp ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_url_srt");
  jbuf_str(&j, cfg->no_url_srt ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_pid_filters");
  jbuf_str(&j, cfg->no_pid_filters ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_http2");
  jbuf_str(&j, cfg->no_http2 ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_http3");
  jbuf_str(&j, cfg->no_http3 ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_fcc");
  jbuf_str(&j, cfg->no_fcc ? "true" : "false");
  jbuf_str(&j, ",");
  jbuf_key(&j, "no_ret");
  jbuf_str(&j, cfg->no_ret ? "true" : "false");
  jbuf_str(&j, "}");
  jbuf_str(&j, "}"); /* root */

  if (j.failed) return -1;
  *out = j.buf;
  *out_len = j.len;
  return 0;
}
