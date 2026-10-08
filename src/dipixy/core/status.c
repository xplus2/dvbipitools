/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "status.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/jsonbuf.h"
#include "lib/sys/signal.h"

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
  if (!f) return 0;
  if (fscanf(f, "%ld %ld", &total_pages, &rss_pages) != 2) rss_pages = 0;
  fclose(f);
  return (uint64_t)rss_pages * (uint64_t)sysconf(_SC_PAGESIZE);
}

#ifdef HAVE_TLS
#define STATUS_HAVE_TLS 1
#else
#define STATUS_HAVE_TLS 0
#endif
#ifdef HAVE_HTTP2
#define STATUS_HAVE_HTTP2 1
#else
#define STATUS_HAVE_HTTP2 0
#endif
#ifdef HAVE_HTTP3
#define STATUS_HAVE_HTTP3 1
#else
#define STATUS_HAVE_HTTP3 0
#endif

#define JFIELD(setter, name, val, sep) do { jbuf_key(&j, name); setter(&j, val); jbuf_str(&j, sep); } while (0)
#define JOBJ(name) JFIELD(jbuf_str, name, "{", "")
#define JBOOL(name, cond, sep) JFIELD(jbuf_str, name, (cond) ? "true" : "false", sep)
#define JSTR_OR_NULL(name, cond, val, sep) do { jbuf_key(&j, name); if (cond) jbuf_json_string(&j, val); else jbuf_str(&j, "null"); jbuf_str(&j, sep); } while (0)

static pthread_key_t g_json_key;
static pthread_once_t g_json_once = PTHREAD_ONCE_INIT;

static void json_buf_release(void *p) {
  jbuf_t *j = p;
  free(j->buf);
  j->buf = NULL;
  j->len = 0;
  j->cap = 0;
}

static void json_key_init(void) {
  pthread_key_create(&g_json_key, json_buf_release);
}

int dipixy_status_render_json(const config_t *cfg, char **out, size_t *out_len) {
  static _Thread_local jbuf_t j;
  struct tm tmv;
  char start_str[32];
  double in_mbps, out_mbps;
  int i;

  pthread_once(&g_json_once, json_key_init);
  pthread_setspecific(g_json_key, &j);
  jbuf_reset(&j);
  jbuf_str(&j, "{");

  JFIELD(jbuf_json_string, "tool", TOOL_NAME, ",");
  JFIELD(jbuf_json_string, "version", TOOL_VERSION, ",");
  JOBJ("build");
  JFIELD(jbuf_json_string, "type", BUILD_TYPE, ",");
  JFIELD(jbuf_json_string, "arch", BUILD_ARCH, ",");
  JFIELD(jbuf_json_string, "link", BUILD_LINK, ",");
  JOBJ("features");
  JBOOL("tls", STATUS_HAVE_TLS, ",");
  JBOOL("http2", STATUS_HAVE_HTTP2, ",");
  JBOOL("http3", STATUS_HAVE_HTTP3, "");
  jbuf_str(&j, "}"); /* features */
  jbuf_str(&j, "}"); /* build */
  jbuf_str(&j, ",");

  gmtime_r(&g_start_unix, &tmv);
  strftime(start_str, sizeof start_str, "%Y-%m-%dT%H:%M:%SZ", &tmv);
  JFIELD(jbuf_json_string, "start_time", start_str, ",");
  JFIELD(jbuf_i64, "start_time_unix", (long long)g_start_unix, ",");
  JFIELD(jbuf_i64, "uptime_seconds", (long long)(time(NULL) - g_start_unix), ",");

  jbuf_key(&j, "exec_args");
  jbuf_str(&j, "[");
  for (i = 0; i < g_argc; i++) {
    if (i) jbuf_str(&j, ",");
    jbuf_json_string(&j, g_argv[i]);
  }
  jbuf_str(&j, "],");

  JOBJ("threads");
  JFIELD(jbuf_i64, "workers", reactor_worker_count(), ",");
  JFIELD(jbuf_str, "pump", "1", ",");
  JFIELD(jbuf_i64, "channels_refresh", channels_refresh_active(), ",");
  JFIELD(jbuf_i64, "total", reactor_worker_count() + 1 + channels_refresh_active(), "},");

  JOBJ("memory");
  JFIELD(jbuf_u64, "rss_bytes", (unsigned long long)status_rss_bytes(), "},");

  status_bitrate(&in_mbps, &out_mbps);
  JOBJ("bitrate");
  JFIELD(jbuf_fixed3, "in_mbps", in_mbps, ",");
  JFIELD(jbuf_fixed3, "out_mbps", out_mbps, "},");

  JOBJ("listen");
  JFIELD(jbuf_json_string, "addr", cfg->listen.scope == LISTEN_ANY ? "all" : cfg->listen.addr, ",");
  JFIELD(jbuf_u64, "port", cfg->listen.port, ",");
  JFIELD(jbuf_json_string, "tls_addr", cfg->listen_tls.scope == LISTEN_ANY ? "all" : cfg->listen_tls.addr, ",");
  JFIELD(jbuf_u64, "tls_port", cfg->listen_tls.port, "},");

  JOBJ("server");
  JFIELD(jbuf_i64, "workers_spec", cfg->workers_spec, ",");
  JFIELD(jbuf_i64, "max_clients", cfg->max_clients, ",");
  JFIELD(jbuf_i64, "max_channels", cfg->max_channels, ",");
  JFIELD(jbuf_u64, "capture_ring_kib", cfg->capture_ring_kib, "},");

  JOBJ("segment");
  JFIELD(jbuf_fixed3, "size_s", cfg->segment_size, ",");
  JFIELD(jbuf_i64, "count", cfg->segment_count, ",");
  JFIELD(jbuf_fixed3, "hls_part_size_s", cfg->hls_part_size, ",");
  JFIELD(jbuf_fixed3, "dash_part_size_s", cfg->dash_part_size, ",");
  JFIELD(jbuf_i64, "hls_seg_pool", cfg->hls_seg_pool, "},");

  JOBJ("sds");
  JFIELD(jbuf_fixed3, "timeout_s", cfg->sds_timeout_s, ",");
  JFIELD(jbuf_fixed3, "refresh_interval_s", cfg->sds_refresh_interval_s, "},");

  JOBJ("metrics");
  JBOOL("enabled", cfg->metrics_id, ",");
  JSTR_OR_NULL("id", cfg->metrics_id, cfg->metrics_id, ",");
  JFIELD(jbuf_u64, "interval_s", cfg->metrics_interval_s, ",");
  JBOOL("http", cfg->metrics_http, "},");

  JFIELD(jbuf_json_string, "cors_origins", cfg->cors_origins ? cfg->cors_origins : "*", ",");

  JBOOL("auth_enabled", cfg->http_auth[0], ",");

  JOBJ("tls");
  if (tls_is_running()) {
    tls_cert_detail_t d;
    tls_cert_detail(NULL, 0, &d);
    JFIELD(jbuf_str, "enabled", "true", ",");
    JFIELD(jbuf_json_string, "cn", d.cn, ",");
    jbuf_key(&j, "aliases");
    jbuf_str(&j, "[");
    for (i = 0; i < d.alias_count; i++) {
      if (i) jbuf_str(&j, ",");
      jbuf_json_string(&j, d.aliases[i]);
    }
    jbuf_str(&j, "],");
    JFIELD(jbuf_json_string, "expiry", d.valid_to, "");
  } else {
    JFIELD(jbuf_str, "enabled", "false", "");
  }
  jbuf_str(&j, "}"); /* tls */
  jbuf_str(&j, ",");

  JOBJ("dlna");
  JBOOL("enabled", cfg->enable_dlna, ",");
  JFIELD(jbuf_i64, "ssdp_ttl", cfg->ssdp_ttl, ",");
  JSTR_OR_NULL("ssdp_iface", cfg->ssdp_iface, cfg->ssdp_iface, ",");
  JSTR_OR_NULL("dlna_host", cfg->enable_dlna, cfg->dlna_host, ",");
  JSTR_OR_NULL("dlna_name", cfg->enable_dlna && cfg->dlna_name, cfg->dlna_name, ",");
  JBOOL("keep_multicast", cfg->dlna_keep_multicast, ",");
  JFIELD(jbuf_fixed3, "ssdp_interval_s", cfg->ssdp_interval_s, ",");
  JFIELD(jbuf_u64, "ssdp_max_age_s", cfg->ssdp_max_age_s, "},");

  JOBJ("flags");
  JBOOL("no_hls", cfg->no_hls, ",");
  JBOOL("no_llhls", cfg->no_llhls, ",");
  JBOOL("no_dash", cfg->no_dash, ",");
  JBOOL("no_lldash", cfg->no_lldash, ",");
  JBOOL("no_ts", cfg->no_ts, ",");
  JBOOL("no_spts", cfg->no_spts, ",");
  JBOOL("no_rawaudio", cfg->no_rawaudio, ",");
  JBOOL("no_mp4", cfg->no_mp4, ",");
  JBOOL("no_url_rtp", cfg->no_url_rtp, ",");
  JBOOL("no_url_udp", cfg->no_url_udp, ",");
  JBOOL("no_url_srt", cfg->no_url_srt, ",");
  JBOOL("no_pid_filters", cfg->no_pid_filters, ",");
  JBOOL("no_lcevc", cfg->no_lcevc, ",");
  JBOOL("no_http2", cfg->no_http2, ",");
  JBOOL("no_http3", cfg->no_http3, ",");
  JBOOL("no_fcc", cfg->no_fcc, ",");
  JBOOL("no_ret", cfg->no_ret, ",");
  JBOOL("join_all", cfg->join_all, "}");
  jbuf_str(&j, "}"); /* root */

  if (j.failed) return -1;
  *out = j.buf;
  *out_len = j.len;
  return 0;
}
