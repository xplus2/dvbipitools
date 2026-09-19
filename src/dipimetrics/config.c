/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/argutil.h"
#include "lib/helper/base64.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/protocol.h"
#include "config.h"
#include "version.h"

#define ARGS_AUTH_CREDS_MAX 128

static char sock_buf[sizeof(((struct sockaddr_un *)0)->sun_path)];
static char cert_buf[PATH_MAX];
static char key_buf[PATH_MAX];

void metrics_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->sock_path = METRICS_DEFAULT_SOCK_PATH;
  cfg->family = AF_INET;
  bufcpy(cfg->listen_addr, sizeof cfg->listen_addr, DEFAULT_LISTEN_ADDR);
  cfg->listen_port = DEFAULT_LISTEN_PORT;
  cfg->expiry_s = DEFAULT_EXPIRY_S;
}

int metrics_cfg_auth(const char *val, char *out, size_t outsz, char *err, size_t errsz) {
  const char *colon = strchr(val, ':');
  char b64[192];
  size_t n;
  if (!colon || colon == val) {
    snprintf(err, errsz, "need user:password");
    return -1;
  }
  if (strlen(val) >= ARGS_AUTH_CREDS_MAX) {
    snprintf(err, errsz, "credentials too long");
    return -1;
  }
  base64_encode(val, strlen(val), b64);
  n = bufcpy(out, outsz, "Basic ");
  bufcpy(out + n, outsz - n, b64);
  return 0;
}

const char *metrics_cfg_conflict(const config_t *cfg) {
  if (cfg->tls_cert && !cfg->tls_key) return "tls cert given without tls key";
  if (cfg->tls_key && !cfg->tls_cert) return "tls key given without tls cert";
  return NULL;
}

static int set_path(char *buf, size_t bufsz, const char **dst, const char *val, char *err, size_t errsz) {
  if (!*val) {
    snprintf(err, errsz, "empty path");
    return -1;
  }
  if (strlen(val) >= bufsz) {
    snprintf(err, errsz, "path too long");
    return -1;
  }
  bufcpy(buf, bufsz, val);
  *dst = buf;
  return 0;
}

static int apply_sock(void *c, const char *val, char *err, size_t errsz) {
  return set_path(sock_buf, sizeof sock_buf, &((config_t *)c)->sock_path, val, err, errsz);
}

static int apply_listen(void *c, const char *val, char *err, size_t errsz) {
  config_t *cfg = c;
  int family;
  char addr[sizeof cfg->listen_addr];
  unsigned port;
  if (argutil_addrport_parse(val, &family, addr, sizeof addr, &port)) {
    snprintf(err, errsz, "invalid addr:port '%s'", val);
    return -1;
  }
  cfg->family = family;
  bufcpy(cfg->listen_addr, sizeof cfg->listen_addr, addr);
  cfg->listen_port = port;
  return 0;
}

static int apply_tls_cert(void *c, const char *val, char *err, size_t errsz) {
  return set_path(cert_buf, sizeof cert_buf, &((config_t *)c)->tls_cert, val, err, errsz);
}

static int apply_tls_key(void *c, const char *val, char *err, size_t errsz) {
  return set_path(key_buf, sizeof key_buf, &((config_t *)c)->tls_key, val, err, errsz);
}

static int apply_auth(void *c, const char *val, char *err, size_t errsz) {
  config_t *cfg = c;
  return metrics_cfg_auth(val, cfg->http_auth, sizeof cfg->http_auth, err, errsz);
}

static int apply_expiry(void *c, const char *val, char *err, size_t errsz) {
  unsigned v;
  if (argutil_uint_range(val, 1, UINT_MAX, &v)) {
    snprintf(err, errsz, "invalid seconds '%s' (need 1..%u)", val, (unsigned)UINT_MAX);
    return -1;
  }
  ((config_t *)c)->expiry_s = v;
  return 0;
}

static int apply_daemonize(void *c, const char *val, char *err, size_t errsz) {
  if (yamlcfg_parse_bool(val, &((config_t *)c)->daemonize)) {
    snprintf(err, errsz, "invalid boolean '%s'", val);
    return -1;
  }
  return 0;
}

static int apply_verbose(void *c, const char *val, char *err, size_t errsz) {
  if (yamlcfg_parse_bool(val, &((config_t *)c)->verbose)) {
    snprintf(err, errsz, "invalid boolean '%s'", val);
    return -1;
  }
  return 0;
}

static int apply_color(void *c, const char *val, char *err, size_t errsz) {
  log_color_t v;
  if (log_color_from_string(val, &v)) {
    snprintf(err, errsz, "invalid '%s' (auto|always|never)", val);
    return -1;
  }
  ((config_t *)c)->color_mode = v;
  return 0;
}

static const yamlcfg_key_t keys[] = {
    {"sock", apply_sock, 0, 0},
    {"listen", apply_listen, 0, 0},
    {"tls.cert", apply_tls_cert, 1, 0},
    {"tls.key", apply_tls_key, 1, 0},
    {"auth", apply_auth, 0, 0},
    {"expiry", apply_expiry, 0, 0},
    {"daemonize", apply_daemonize, 0, 0},
    {"verbose", apply_verbose, 0, 0},
    {"color", apply_color, 0, 0},
};

int metrics_cfg_load(config_t *cfg, const char *path) {
  yamlcfg_t y;
  return yamlcfg_load(&y, TOOL_NAME, 0, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof *keys, cfg) == YAMLCFG_ERROR ? -1 : 0;
}

int metrics_cfg_test(const char *path) {
  yamlcfg_t y;
  config_t cfg;
  const char *conflict;

  metrics_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, 1, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof *keys, &cfg) != YAMLCFG_LOADED) return -1;
  conflict = metrics_cfg_conflict(&cfg);
  if (conflict) yamlcfg_warn(&y, "%s", conflict);
  yamlcfg_report(&y);
  return 0;
}
