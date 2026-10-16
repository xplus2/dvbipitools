/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"

#include "../../version.h"

#define SRT_CONNECT_TIMEOUT_MS 3000

/* calloc+tssrc_open+backend/refcount=1. key: caller-owned strdup, NULL for rist/stdin. */
static capture_ctx_t *tssrc_ctx_new(char *key, const tssrc_cfg_t *cfg, net_err_reason_t *reason) {
  capture_ctx_t *c = calloc(1, sizeof *c);
  if (!c) {
    free(key);
    if (reason)
      *reason = NET_ERR_OTHER;
    return NULL;
  }
  atomic_init(&c->ts_push_head, -1);
  c->pump_shard = next_pump_shard();
  c->key = key;
  c->ts = tssrc_open_blocking(cfg, 30.0, reason);
  if (!c->ts) {
    free(c->key);
    free(c);
    return NULL;
  }
  c->backend = CAP_BACKEND_TSSRC;
  c->refcount = 1;
  return c;
}

static void srt_key(char *key, size_t keysz, const char *host, unsigned port) {
  char portbuf[12];
  size_t off;
  off = bufcpy(key, keysz, "srt:");
  off += bufcpy(key + off, keysz - off, host);
  off += bufcpy(key + off, keysz - off, ":");
  uint_to_str(portbuf, port);
  bufcpy(key + off, keysz - off, portbuf);
}

capture_ctx_t *capture_try_ref_srt(const char *host, unsigned port) {
  char key[300];
  capture_ctx_t *c;
  srt_key(key, sizeof key, host, port);
  pthread_mutex_lock(&g_lock);
  c = find_existing_tssrc(key);
  if (c) atomic_fetch_add_explicit(&c->refcount, 1, memory_order_relaxed);
  pthread_mutex_unlock(&g_lock);
  return c;
}

capture_ctx_t *capture_open_srt(const char *host, unsigned port) {
  char key[300];
  capture_ctx_t *c;
  capture_ctx_t *dup;
  tssrc_cfg_t cfg;
  char *keydup;

  srt_key(key, sizeof key, host, port);

  pthread_mutex_lock(&g_lock);
  c = find_existing_tssrc(key);
  if (c) {
    atomic_fetch_add_explicit(&c->refcount, 1, memory_order_relaxed);
    pthread_mutex_unlock(&g_lock);
    return c;
  }
  pthread_mutex_unlock(&g_lock);

  keydup = strdup(key);
  if (!keydup) return NULL;
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_SRT;
  cfg.srt_listen = 0;
  cfg.srt_host = host;
  cfg.srt_port = port;
  cfg.srt_connect_timeout_ms = SRT_CONNECT_TIMEOUT_MS;
  c = tssrc_ctx_new(keydup, &cfg, NULL);
  if (!c) {
    log_code("YE600156", TOOL_NAME ": srt source %s:%u: connect failed, source left unavailable", host, port);
    return NULL;
  }

  dup = link_ctx(c);
  if (dup != c) free_ctx_resources(c);
  return dup;
}

capture_ctx_t *capture_open_http_static(const char *url, int insecure_tls) {
  tssrc_cfg_t cfg;
  capture_ctx_t *c;
  capture_ctx_t *dup;
  http_url_t hu;
  net_err_reason_t reason;
  char *keydup;
  if (http_url_parse(url, &hu)) {
    char *safe = uriparse_redact_dup(url);
    log_code("YE000674", TOOL_NAME ": http source %s: invalid URL, source left unavailable", safe ? safe : "?");
    free(safe);
    return NULL;
  }

  pthread_mutex_lock(&g_lock);
  c = find_existing_tssrc(url);
  if (c) {
    atomic_fetch_add_explicit(&c->refcount, 1, memory_order_relaxed);
    pthread_mutex_unlock(&g_lock);
    return c;
  }
  pthread_mutex_unlock(&g_lock);

  keydup = strdup(url);
  if (!keydup) return NULL;
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_HTTP;
  cfg.http = hu;
  cfg.insecure_tls = insecure_tls;
  c = tssrc_ctx_new(keydup, &cfg, &reason);
  if (!c) {
    char *safe = uriparse_redact_dup(url);
    log_code("YE600157", TOOL_NAME ": http source %s: %s, source left unavailable", safe ? safe : "?", net_err_reason_name(reason));
    free(safe);
    return NULL;
  }

  dup = link_ctx(c);
  if (dup != c) free_ctx_resources(c);
  return dup;
}

static capture_ctx_t *g_rist_ctx;
static capture_ctx_t *g_stdin_ctx;

int capture_rist_init(const char *rist_uri) {
  tssrc_cfg_t cfg;
  capture_ctx_t *c;
  net_err_reason_t reason;

  if (!rist_uri) return 0;
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_RIST;
  cfg.rist_uri = rist_uri;
  c = tssrc_ctx_new(NULL, &cfg, &reason);
  if (!c) {
    char *safe = uriparse_redact_dup(rist_uri);
    log_code("YE000675", TOOL_NAME ": --rist %s: %s", safe ? safe : "?", net_err_reason_name(reason));
    free(safe);
    return -1;
  }
  if (!link_ctx(c)) {
    free_ctx_resources(c);
    return -1;
  }
  g_rist_ctx = c;
  return 0;
}

capture_ctx_t *capture_rist_get(void) { return g_rist_ctx ? capture_ref(g_rist_ctx) : NULL; }

int capture_stdin_init(void) {
  tssrc_cfg_t cfg;
  capture_ctx_t *c;
  net_err_reason_t reason;

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_STDIN;
  c = tssrc_ctx_new(NULL, &cfg, &reason);
  if (!c) {
    log_code("YE000676", TOOL_NAME ": -i -: %s", net_err_reason_name(reason));
    return -1;
  }

  if (!link_ctx(c)) {
    free_ctx_resources(c);
    return -1;
  }
  g_stdin_ctx = c;
  return 0;
}

capture_ctx_t *capture_stdin_get(void) { return g_stdin_ctx ? capture_ref(g_stdin_ctx) : NULL; }
