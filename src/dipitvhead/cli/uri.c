/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/describe.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/uriparse.h"

#include "priv.h"

/* [@]<addr>:<port> or [@][<addr6>]:<port>, multicast literal required */
static int mcast_group_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out) {
  if (*s == '@') s++;
  return uriparse_mcast_addrport(s, family, addr_out, addr_out_sz, port_out);
}

/* [[src]@]<addr>:<port>, mcast literal required */
static int mcast_src_parse(const char *s, source_t *src) {
  return uriparse_mcast_src_addrport(s, &src->family, src->group, sizeof src->group, &src->port, src->source, sizeof src->source);
}

int tvh_cfg_mcast(config_t *cfg, const char *s) {
  return mcast_group_parse(s, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port);
}

static int source_parse(const char *uri, source_t *s) {
  memset(s, 0, sizeof *s);
  if (strcmp(uri, "-") == 0) {
    s->kind = SRC_STDIN;
    return 0;
  }
  if (strncmp(uri, "rtp://", 6) == 0) {
    s->kind = SRC_RTP;
    return mcast_src_parse(uri + 6, s);
  }
  if (strncmp(uri, "udp://", 6) == 0) {
    s->kind = SRC_UDP;
    return mcast_src_parse(uri + 6, s);
  }
  if (strncmp(uri, "http://", 7) == 0 || strncmp(uri, "https://", 8) == 0) {
    s->kind = SRC_HTTP;
    return http_url_parse(uri, &s->http);
  }
  if (strncmp(uri, "rist://", 7) == 0) {
    if (uri[7] != '@') return -1; /* rist:// as input always listens */
    if (strlen(uri) >= sizeof s->rist_uri) return -1;
    s->kind = SRC_RIST;
    bufcpy(s->rist_uri, sizeof s->rist_uri, uri);
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    const char *rest = uri + 6;
    int listen = *rest == '@';
    if (listen) rest++;
    if (argutil_addrport_parse(rest, &s->srt_family, s->srt_host, sizeof s->srt_host, &s->srt_port)) return -1;
    s->kind = SRC_SRT;
    s->srt_listen = listen;
    return 0;
  }
  return -1;
}

void source_describe(const source_t *s, char *buf, size_t n) {
  switch (s->kind) {
    case SRC_RTP:
      uriparse_mcast_src_uri(buf, n, "rtp", s->family, s->source, s->group, s->port);
      break;
    case SRC_UDP:
      uriparse_mcast_src_uri(buf, n, "udp", s->family, s->source, s->group, s->port);
      break;
    case SRC_HTTP:
      describe_http_uri(buf, n, s->http.tls, s->http.host, s->http.port, s->http.path);
      break;
    case SRC_STDIN:
      bufcpy(buf, n, "-");
      break;
    case SRC_RIST:
      bufcpy(buf, n, s->rist_uri);
      break;
    case SRC_SRT:
      describe_srt_uri(buf, n, s->srt_family, s->srt_listen, s->srt_host, s->srt_port);
      break;
  }
}

void mcast_describe(const config_t *cfg, char *buf, size_t n) {
  uriparse_mcast_describe(cfg->family, cfg->mcast_group, cfg->mcast_port, buf, n);
}

int tvh_cfg_source(source_t *dst, const char *uri, char *err, size_t errsz) {
  if (source_parse(uri, dst)) {
    snprintf(err, errsz, "invalid uri '%s'", uri);
    return -1;
  }
  return 0;
}

/* uri NULL: blank input, source given later */
int tvh_cfg_add_input(config_t *cfg, const char *uri, char *err, size_t errsz) {
  dipitvhead_input_t *in;
  if (cfg->n_inputs >= ARGS_MAX_INPUTS) {
    snprintf(err, errsz, "too many inputs (max %d)", ARGS_MAX_INPUTS);
    return -1;
  }
  in = &cfg->inputs[cfg->n_inputs];
  memset(in, 0, sizeof *in);
  if (uri && tvh_cfg_source(&in->input, uri, err, errsz)) return -1;
  cfg->n_inputs++;
  return 0;
}

int tvh_cfg_add_peer(config_t *cfg, const char *uri, char *err, size_t errsz) {
  if (strncmp(uri, "rist://", 7) == 0) {
    if (cfg->n_srt > 0) {
      bufcpy(err, errsz, "rist:// and srt:// peers cannot mix in one run");
      return -1;
    }
    if (cfg->n_rist >= ARGS_MAX_RIST_PEERS) {
      snprintf(err, errsz, "too many peers (max %d)", ARGS_MAX_RIST_PEERS);
      return -1;
    }
    if (strlen(uri) >= sizeof cfg->rist_uri[0]) {
      bufcpy(err, errsz, "uri too long");
      return -1;
    }
    memcpy(cfg->rist_uri[cfg->n_rist], uri, strlen(uri) + 1);
    cfg->n_rist++;
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    if (cfg->n_rist > 0) {
      bufcpy(err, errsz, "rist:// and srt:// peers cannot mix in one run");
      return -1;
    }
    if (cfg->n_srt >= ARGS_MAX_SRT_PEERS) {
      snprintf(err, errsz, "too many srt:// peers (max %d)", ARGS_MAX_SRT_PEERS);
      return -1;
    }
    if (uri[6] == '@') {
      bufcpy(err, errsz, "srt:// output always calls out, no listener mode");
      return -1;
    }
    if (argutil_addrport_parse(uri + 6, &cfg->srt_family[cfg->n_srt], cfg->srt_host[cfg->n_srt], sizeof cfg->srt_host[0], &cfg->srt_port[cfg->n_srt])) {
      snprintf(err, errsz, "invalid srt uri '%s'", uri);
      return -1;
    }
    cfg->n_srt++;
    return 0;
  }
  snprintf(err, errsz, "invalid uri '%s' (must start with rist:// or srt://)", uri);
  return -1;
}
