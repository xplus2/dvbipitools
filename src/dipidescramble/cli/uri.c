/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdio.h>
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

static int input_parse(const char *uri, input_t *s) {
  memset(s, 0, sizeof *s);
  if (strcmp(uri, "-") == 0) {
    s->kind = INPUT_STDIN;
    return 0;
  }
  if (strncmp(uri, "rtp://", 6) == 0) {
    s->kind = INPUT_RTP;
    return mcast_group_parse(uri + 6, &s->family, s->group, sizeof s->group, &s->port);
  }
  if (strncmp(uri, "udp://", 6) == 0) {
    s->kind = INPUT_UDP;
    return mcast_group_parse(uri + 6, &s->family, s->group, sizeof s->group, &s->port);
  }
  if (strncmp(uri, "rist://", 7) == 0) {
    if (uri[7] != '@') return -1; /* rist:// as input always listens */
    if (strlen(uri) >= sizeof s->rist_uri) return -1;
    s->kind = INPUT_RIST;
    bufcpy(s->rist_uri, sizeof s->rist_uri, uri);
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    const char *rest = uri + 6;
    int listen = *rest == '@';
    if (listen) rest++;
    if (argutil_addrport_parse(rest, &s->srt_family, s->srt_host, sizeof s->srt_host, &s->srt_port)) return -1;
    s->kind = INPUT_SRT;
    s->srt_listen = listen;
    return 0;
  }
  return -1;
}

void input_describe(const input_t *s, char *buf, size_t n) {
  switch (s->kind) {
    case INPUT_RTP:
      describe_mcast_uri(buf, n, "rtp", s->family, s->group, s->port);
      break;
    case INPUT_UDP:
      describe_mcast_uri(buf, n, "udp", s->family, s->group, s->port);
      break;
    case INPUT_STDIN:
      bufcpy(buf, n, "-");
      break;
    case INPUT_RIST:
      bufcpy(buf, n, s->rist_uri);
      break;
    case INPUT_SRT:
      describe_srt_uri(buf, n, s->srt_family, s->srt_listen, s->srt_host, s->srt_port);
      break;
  }
}

static int parse_out_uri(const char *uri, out_target_t *o) {
  int r;
  memset(o, 0, sizeof *o);
  if (strncmp(uri, "srt://", 6) == 0) {
    if (uri[6] == '@') return -1; /* srt:// output always calls out, no listener mode */
    if (argutil_addrport_parse(uri + 6, &o->srt_family, o->srt_host, sizeof o->srt_host, &o->srt_port)) return -1;
    o->kind = OUT_SRT;
    return 0;
  }
  r = uriparse_rtmp_or_file(uri, o->rtmp_url, sizeof o->rtmp_url, o->file_path, sizeof o->file_path);
  if (r < 0) return -1;
  if (r == 2) o->kind = OUT_RTMPS;
  else if (r == 1) o->kind = OUT_RTMP;
  else o->kind = OUT_FILE;
  return 0;
}

void out_describe(const out_target_t *o, char *buf, size_t n) {
  switch (o->kind) {
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

int dscr_cfg_set_input(config_t *cfg, const char *s) {
  if (input_parse(s, &cfg->input)) return -1;
  cfg->have_input = 1;
  return 0;
}

int dscr_cfg_add_out(config_t *cfg, const char *s) {
  if (cfg->n_out >= DIPIDESCRAMBLE_MAX_OUT || parse_out_uri(s, &cfg->out[cfg->n_out])) return -1;
  cfg->n_out++;
  return 0;
}

