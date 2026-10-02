/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/describe.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/uriparse.h"

#include "priv.h"

/* rest: [@]addr:port, multicast literal required */
static int parse_mcast_addrport(const char *rest, int *family, char *group, size_t groupsz, unsigned *port) {
  if (*rest == '@') rest++;
  return uriparse_mcast_addrport(rest, family, group, groupsz, port);
}

static int parse_direct(const char *rest, source_t *s) {
  return parse_mcast_addrport(rest, &s->family, s->group, sizeof s->group, &s->port);
}

static int parse_uri(const char *uri, source_t *s) {
  memset(s, 0, sizeof *s);
  if (strcmp(uri, "-") == 0) {
    s->kind = URI_FILE; /* file_path[0] == '\0': stdin */
    return 0;
  }
  if (strncmp(uri, "rtp://", 6) == 0) {
    s->kind = URI_RTP;
    s->rtp_wrapped = 1;
    return parse_direct(uri + 6, s);
  }
  if (strncmp(uri, "udp://", 6) == 0) {
    s->kind = URI_UDP;
    s->rtp_wrapped = 0;
    return parse_direct(uri + 6, s);
  }
  if (strncmp(uri, "http://", 7) == 0 || strncmp(uri, "https://", 8) == 0) {
    s->kind = URI_HTTP;
    return http_url_parse(uri, &s->http);
  }
  if (strncmp(uri, "rist://", 7) == 0) {
    if (uri[7] != '@') return -1; /* rist:// as input always listens */
    if (strlen(uri) >= sizeof s->rist_uri) return -1;
    s->kind = URI_RIST;
    bufcpy(s->rist_uri, sizeof s->rist_uri, uri);
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    const char *rest = uri + 6;
    int listen = *rest == '@';
    if (listen) rest++;
    if (argutil_addrport_parse(rest, &s->srt_family, s->srt_host, sizeof s->srt_host, &s->srt_port)) return -1;
    s->kind = URI_SRT;
    s->srt_listen = listen;
    return 0;
  }
  if (strlen(uri) >= sizeof s->file_path) return -1;
  s->kind = URI_FILE;
  bufcpy(s->file_path, sizeof s->file_path, uri);
  return 0;
}

void source_describe(const source_t *s, char *buf, size_t n) {
  switch (s->kind) {
    case URI_RTP:
      describe_mcast_uri(buf, n, "rtp", s->family, s->group, s->port);
      break;
    case URI_UDP:
      describe_mcast_uri(buf, n, "udp", s->family, s->group, s->port);
      break;
    case URI_HTTP:
      describe_http_uri(buf, n, s->http.tls, s->http.host, s->http.port, s->http.path);
      break;
    case URI_FILE:
      bufcpy(buf, n, s->file_path[0] ? s->file_path : "- (stdin)");
      break;
    case URI_RIST:
      bufcpy(buf, n, s->rist_uri);
      break;
    case URI_SRT:
      describe_srt_uri(buf, n, s->srt_family, s->srt_listen, s->srt_host, s->srt_port);
      break;
  }
}

static int parse_out_uri(const char *uri, out_target_t *o) {
  int r;
  memset(o, 0, sizeof *o);
  if (strncmp(uri, "rtp://", 6) == 0) {
    o->kind = OUT_RTP;
    return parse_mcast_addrport(uri + 6, &o->family, o->group, sizeof o->group, &o->port);
  }
  if (strncmp(uri, "udp://", 6) == 0) {
    o->kind = OUT_UDP;
    return parse_mcast_addrport(uri + 6, &o->family, o->group, sizeof o->group, &o->port);
  }
  if (strncmp(uri, "rist://", 7) == 0) {
    if (strlen(uri) >= sizeof o->rist_uri) return -1;
    o->kind = OUT_RIST;
    bufcpy(o->rist_uri, sizeof o->rist_uri, uri);
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    if (uri[6] == '@') return -1; /* srt:// output always calls out, no listener mode */
    if (argutil_addrport_parse(uri + 6, &o->srt_family, o->srt_host, sizeof o->srt_host, &o->srt_port)) return -1;
    o->kind = OUT_SRT;
    return 0;
  }
  r = uriparse_rtmp_or_file(uri, o->rtmp_url, sizeof o->rtmp_url, o->file_path, sizeof o->file_path);
  if (r < 0) return -1;
  o->kind = r == 2 ? OUT_RTMPS : r == 1 ? OUT_RTMP : OUT_FILE;
  return 0;
}

void out_describe(const out_target_t *o, char *buf, size_t n) {
  switch (o->kind) {
    case OUT_RTP:
    case OUT_UDP: {
      const char *scheme = (o->kind == OUT_RTP) ? "rtp" : "udp";
      describe_mcast_uri(buf, n, scheme, o->family, o->group, o->port);
      break;
    }
    case OUT_RIST:
      bufcpy(buf, n, o->rist_uri);
      break;
    case OUT_RTMP:
    case OUT_RTMPS:
      bufcpy(buf, n, o->rtmp_url);
      break;
    case OUT_FILE:
      bufcpy(buf, n, strcmp(o->file_path, "-") == 0 ? "- (stdout)" : o->file_path);
      break;
    case OUT_SRT:
      describe_srt_uri(buf, n, o->srt_family, 0, o->srt_host, o->srt_port);
      break;
  }
}


int rec_cfg_set_in(config_t *cfg, const char *s) {
  if (parse_uri(s, &cfg->source)) return -1;
  cfg->fl.have_in = 1;
  return 0;
}

int rec_cfg_add_out(config_t *cfg, const char *s) {
  if (cfg->n_out >= DIPIREC_MAX_OUT || parse_out_uri(s, &cfg->out[cfg->n_out])) return -1;
  cfg->n_out++;
  return 0;
}

