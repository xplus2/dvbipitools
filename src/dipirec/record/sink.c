/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "lib/log.h"

#include "../version.h"
#include "priv.h"

int src_open(const config_t *cfg, src_t *s) {
  tssrc_cfg_t tc;
  memset(s, 0, sizeof *s);
  s->kind = cfg->source.kind;
  memset(&tc, 0, sizeof tc);
  tc.user_agent = TOOL_NAME "/" TOOL_VERSION;
  if (s->kind == URI_HTTP) {
    tc.kind = TSSRC_HTTP;
    tc.http = cfg->source.http;
    tc.insecure_tls = cfg->insecure_tls;
  } else if (s->kind == URI_FILE) {
    if (cfg->source.file_path[0]) {
      tc.kind = TSSRC_FILE;
      tc.file_path = cfg->source.file_path;
    } else {
      tc.kind = TSSRC_STDIN;
    }
  } else {
    tc.kind = (s->kind == URI_RTP) ? TSSRC_RTP : TSSRC_UDP;
    tc.family = cfg->source.family;
    tc.group = cfg->source.group;
    tc.port = cfg->source.port;
    tc.iface = cfg->iface_in;
  }

  s->t = tssrc_open(&tc, NULL);
  if (!s->t)
    return -1;

  if (s->kind != URI_HTTP && cfg->ret.enabled) {
    s->ret = ret_client_open(cfg);
    if (!s->ret) {
      tssrc_close(s->t);
      return -1;
    }
  }
  return 0;
}

ssize_t src_read(src_t *s, unsigned char *buf, size_t cap) {
  if (s->ret)
    return ret_client_read(s->ret, tssrc_mcast(s->t), buf, cap);
  return tssrc_read(s->t, buf, cap, NULL);
}

void src_close(src_t *s) {
  if (s->ret)
    ret_client_close(s->ret);
  tssrc_close(s->t);
}

int open_output(const char *path) {
  int fd;

  if (strcmp(path, "-") == 0)
    return STDOUT_FILENO;
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0640);
  if (fd < 0)
    log_line("open %s: %s", path, strerror(errno));
  return fd;
}

static int write_all(int fd, const unsigned char *p, size_t n) {
  while (n) {
    ssize_t w = write(fd, p, n);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      log_line("w:%s", strerror(errno));
      return -1;
    }
    p += w;
    n -= (size_t)w;
  }
  return 0;
}

int sink_open(const config_t *cfg, const out_target_t *t, out_sink_t *o) {
  o->net = NULL;
  o->rist = NULL;
  o->net_had_error = 0;
  o->rist_had_error = 0;
  o->errors_total = 0;
  if (t->kind == OUT_FILE) {
    o->fd = open_output(t->file_path);
    return o->fd < 0 ? -1 : 0;
  }
  o->fd = -1;
  if (t->kind == OUT_RIST) {
    ristout_cfg_t rc;
    memset(&rc, 0, sizeof rc);
    rc.peer_uri[0] = t->rist_uri;
    rc.npeers = 1;
    rc.profile = cfg->rist_profile == RIST_PROF_MAIN ? RISTOUT_PROFILE_MAIN : RISTOUT_PROFILE_SIMPLE;
    rc.secret = cfg->rist_secret;
    rc.cname = cfg->rist_cname;
    rc.buffer_ms = cfg->rist_buffer_ms;
    rc.verbose = cfg->verbose;
    o->rist = ristout_open(&rc);
    return o->rist ? 0 : -1;
  }
  {
    tssink_cfg_t tc;
    memset(&tc, 0, sizeof tc);
    tc.kind = (t->kind == OUT_RTP) ? TSSINK_RTP : TSSINK_UDP;
    tc.family = t->family;
    tc.group = t->group;
    tc.port = t->port;
    tc.iface = cfg->iface_out;
    tc.ttl = cfg->out_ttl;
    o->net = tssink_open(&tc);
  }
  return o->net ? 0 : -1;
}

void note_send_result(int ok, int *had_error, uint64_t *errors_total, const char *label) {
  if (!ok) {
    (*errors_total)++;
    if (!*had_error) {
      log_line("%s output: write failed, will keep retrying", label);
      *had_error = 1;
    }
  } else if (*had_error) {
    log_line("%s output: recovered", label);
    *had_error = 0;
  }
}

int sink_write(out_sink_t *o, const unsigned char *p, size_t n) {
  if (o->rist) {
    note_send_result(ristout_write(o->rist, p, n) >= 0, &o->rist_had_error, &o->errors_total, "rist");
    return 0;
  }
  if (o->net) {
    note_send_result(tssink_write(o->net, p, n) >= 0, &o->net_had_error, &o->errors_total, "net");
    return 0;
  }
  return write_all(o->fd, p, n);
}

void sink_close(out_sink_t *o) {
  if (o->rist) {
    ristout_close(o->rist);
    return;
  }
  if (o->net) {
    tssink_close(o->net);
    return;
  }
  if (o->fd >= 0 && o->fd != STDOUT_FILENO)
    close(o->fd);
}
