/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>

#include "dipixy/reactor/dispatch/priv.h"
#include "dipixy/ts/lcevcselect.h"

http_method_t parse_http_method(const char *method, size_t method_len) {
  if (method_len == 3 && !memcmp(method, "GET", 3)) return HTTP_METHOD_GET;
  if (method_len == 4 && !memcmp(method, "HEAD", 4)) return HTTP_METHOD_HEAD;
  if (method_len == 4 && !memcmp(method, "POST", 4)) return HTTP_METHOD_POST;
  if (method_len == 9 && !memcmp(method, "SUBSCRIBE", 9)) return HTTP_METHOD_SUBSCRIBE;
  if (method_len == 11 && !memcmp(method, "UNSUBSCRIBE", 11)) return HTTP_METHOD_UNSUBSCRIBE;
  return HTTP_METHOD_OTHER;
}

void dispatch_dlna_subprotocol(int epfd, conn_t *c, const char *path, http_method_t method_kind, const struct phr_header *headers, size_t num_headers, size_t body_len, int keep_alive) {
  if (!reactor_cfg()->enable_dlna) {
    respond_status(c, RESP_404, keep_alive);
  } else {
    switch (method_kind) {
      case HTTP_METHOD_POST:
        if (!strcmp(path, "/dlna/cd_control") || !strcmp(path, "/dlna/cm_control")) {
          serve_dlna_control(c, !strcmp(path, "/dlna/cd_control") ? "cd" : "cm", headers, num_headers, (char *)c->in.buf + c->req_bytes - body_len, body_len, keep_alive);
        } else {
          respond_status(c, RESP_405, keep_alive);
        }
        break;
      case HTTP_METHOD_SUBSCRIBE:
        if (!strcmp(path, "/dlna/cd_event") || !strcmp(path, "/dlna/cm_event")) serve_dlna_subscribe(c, !strcmp(path, "/dlna/cd_event") ? GENA_SVC_CD : GENA_SVC_CM, headers, num_headers, keep_alive);
        else respond_status(c, RESP_405, keep_alive);
        break;
      case HTTP_METHOD_UNSUBSCRIBE:
        if (!strcmp(path, "/dlna/cd_event") || !strcmp(path, "/dlna/cm_event")) serve_dlna_unsubscribe(c, !strcmp(path, "/dlna/cd_event") ? GENA_SVC_CD : GENA_SVC_CM, headers, num_headers, keep_alive);
        else respond_status(c, RESP_405, keep_alive);
        break;
      case HTTP_METHOD_GET:
      case HTTP_METHOD_HEAD:
      case HTTP_METHOD_OTHER:
        respond_status(c, RESP_405, keep_alive);
        break;
    }
  }
  c->state = CONN_WRITING;
  reactor_finish(epfd, c);
}

/* desc.xml/scpd/metrics/time/index/status.js/ws-upgrade/export: 1 = handled (caller finishes), 0 = fall through to route dispatch */
int dispatch_special_path(conn_t *c, const char *path, int is_head, const struct phr_header *headers, size_t num_headers, int keep_alive, const pid_filter_t *filter, const lcevc_select_t *lcevc, const char *query) {
  if (reactor_cfg()->enable_dlna) {
    static const struct { const char *path; void (*fn)(conn_t *, int, int); } dlna_paths[] = {
      {"/dlna/desc.xml", serve_dlna_desc}, {"/dlna/cd_scpd.xml", serve_dlna_cd_scpd}, {"/dlna/cm_scpd.xml", serve_dlna_cm_scpd},
    };
    for (size_t i = 0; i < sizeof dlna_paths / sizeof dlna_paths[0]; i++)
      if (!strcmp(path, dlna_paths[i].path)) {
        dlna_paths[i].fn(c, is_head, keep_alive);
        return 1;
      }
  }

  if (reactor_cfg()->metrics_http && !strcmp(path, "/metrics")) {
    serve_metrics(c, is_head, keep_alive);
    return 1;
  }
  if (!strcmp(path, TIME_EP_PATH)) {
    serve_time(c, is_head, keep_alive);
    return 1;
  }
  if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
    if (reactor_cfg()->no_status)
      respond_status(c, RESP_404, keep_alive);
    else
      serve_htdocs_index(c, is_head, keep_alive);
    return 1;
  }
  if (!reactor_cfg()->no_status && !strcmp(path, "/ui/status.js")) {
    serve_status(c, is_head, keep_alive);
    return 1;
  }
  if (!reactor_cfg()->no_status && !is_head && ws_try_upgrade(c, path, headers, num_headers))
    return 1;

  if (!strncmp(path, "/export/", 8)) {
    route_fmt_t exp_fmt;
    playlist_type_t exp_ptype;
    char host_buf[128];
    const char *host_hdr = find_header(headers, num_headers, "Host", host_buf, sizeof host_buf) ? host_buf : NULL;
    if (playlist_path_parse(path, &exp_fmt, &exp_ptype) || playlist_fmt_disabled(reactor_cfg(), exp_fmt)) {
      respond_status(c, RESP_404, keep_alive);
      return 1;
    }
    serve_playlist(c, exp_fmt, exp_ptype, host_hdr, query, filter, lcevc, is_head, keep_alive);
    return 1;
  }
  return 0;
}
