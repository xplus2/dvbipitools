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
  if (method_len == 7 && !memcmp(method, "CONNECT", 7)) return HTTP_METHOD_CONNECT;
  return HTTP_METHOD_OTHER;
}

void dispatch_dlna_subprotocol(int epfd, conn_t *c, special_path_t sp, http_method_t method_kind, const struct phr_header *headers, size_t num_headers, size_t body_len, int keep_alive) {
  int cd = sp == SPECIAL_DLNA_CD_CONTROL || sp == SPECIAL_DLNA_CD_EVENT;
  int event = sp == SPECIAL_DLNA_CD_EVENT || sp == SPECIAL_DLNA_CM_EVENT;
  if (!reactor_cfg()->enable_dlna)
    respond_status(c, RESP_404, keep_alive);
  else if (method_kind == HTTP_METHOD_POST && (sp == SPECIAL_DLNA_CD_CONTROL || sp == SPECIAL_DLNA_CM_CONTROL))
    serve_dlna_control(c, cd ? GENA_SVC_CD : GENA_SVC_CM, headers, num_headers, (char *)c->in.buf + c->req_bytes - body_len, body_len, keep_alive);
  else if (method_kind == HTTP_METHOD_SUBSCRIBE && event)
    serve_dlna_subscribe(c, cd ? GENA_SVC_CD : GENA_SVC_CM, headers, num_headers, keep_alive);
  else if (method_kind == HTTP_METHOD_UNSUBSCRIBE && event)
    serve_dlna_unsubscribe(c, cd ? GENA_SVC_CD : GENA_SVC_CM, headers, num_headers, keep_alive);
  else
    respond_status(c, RESP_405, keep_alive);
  c->state = CONN_WRITING;
  reactor_finish(epfd, c);
}

/* 1 handled (caller finishes), 0 fall through to route dispatch */
int dispatch_special_path(conn_t *c, special_path_t sp, const char *path, int is_head, const struct phr_header *headers, size_t num_headers, int keep_alive, const pid_filter_t *filter, const lcevc_select_t *lcevc, const char *query) {
  const config_t *cfg = reactor_cfg();
  switch (sp) {
    case SPECIAL_DLNA_DESC:
    case SPECIAL_DLNA_CD_SCPD:
    case SPECIAL_DLNA_CM_SCPD:
      if (!cfg->enable_dlna) return 0;
      if (sp == SPECIAL_DLNA_DESC) serve_dlna_desc(c, is_head, keep_alive);
      else if (sp == SPECIAL_DLNA_CD_SCPD) serve_dlna_cd_scpd(c, is_head, keep_alive);
      else serve_dlna_cm_scpd(c, is_head, keep_alive);
      return 1;
    case SPECIAL_METRICS:
      if (!cfg->metrics_http) return 0;
      serve_metrics(c, is_head, keep_alive);
      return 1;
    case SPECIAL_TIME:
      serve_time(c, is_head, keep_alive);
      return 1;
    case SPECIAL_INDEX:
      if (cfg->no_status) respond_status(c, RESP_404, keep_alive);
      else serve_htdocs_index(c, is_head, keep_alive);
      return 1;
    case SPECIAL_STATUS_JS:
      if (cfg->no_status) return 0;
      serve_status(c, is_head, keep_alive);
      return 1;
    case SPECIAL_UI_WS:
      if (cfg->no_status || is_head) return 0;
      ws_upgrade(c, headers, num_headers);
      return 1;
    case SPECIAL_EXPORT: {
      route_fmt_t exp_fmt;
      playlist_type_t exp_ptype;
      char host_buf[128];
      const char *host_hdr = find_header(headers, num_headers, "Host", host_buf, sizeof host_buf) ? host_buf : NULL;
      if (playlist_path_parse(path, &exp_fmt, &exp_ptype) || playlist_fmt_disabled(cfg, exp_fmt)) respond_status(c, RESP_404, keep_alive);
      else serve_playlist(c, exp_fmt, exp_ptype, host_hdr, query, filter, lcevc, is_head, keep_alive);
      return 1;
    }
    default:
      return 0;
  }
}
