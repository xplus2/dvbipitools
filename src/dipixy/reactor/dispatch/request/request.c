/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>

#include "dipixy/reactor/dispatch/priv.h"
#include "dipixy/core/metrics.h"
#include "dipixy/ts/lcevcselect.h"
#include "dipixy/ts/pmtselect.h"

/* dispatche request from c->in: parse reqln */
void reactor_dispatch(int epfd, conn_t *c, const char *method, size_t method_len, const char *path_in, size_t path_len, int minor_version, const struct phr_header *headers, size_t num_headers, size_t header_bytes, size_t body_len) {
  char *path;
  char path_end;
  char *qmark;
  const char *query;
  char inm_buf[256];
  char origin_buf[128];
  const char *if_none_match;
  const char *origin_hdr;
  route_t rt;
  pid_filter_t filter;
  lcevc_select_t lcevc;
  unsigned pmt_pid;
  route_result_t rr;
  http_method_t method_kind;
  int is_head;
  int keep_alive;
  const char *want_auth;
  c->req_bytes = header_bytes + body_len;
  if (!t_open_replay) dipixy_metrics_note_request();
  path = (char *)c->in.buf + (path_in - (const char *)c->in.buf);
  path_end = path[path_len];
  path[path_len] = '\0';
  keep_alive = wants_keepalive(minor_version, headers, num_headers);
  if (atomic_load_explicit(&c->read_done, memory_order_relaxed) && c->in.len <= c->req_bytes) keep_alive = 0; /* FIN seen, nothing pipelined */
  if_none_match = NULL;
  if (find_header(headers, num_headers, "If-None-Match", inm_buf, sizeof inm_buf)) {
    if_none_match = inm_buf;
  }
  origin_hdr = find_header(headers, num_headers, "Origin", origin_buf, sizeof origin_buf) ? origin_buf : NULL;
  method_kind = parse_http_method(method, method_len);
  is_head = method_kind == HTTP_METHOD_HEAD;
  if (method_kind == HTTP_METHOD_OTHER) {
    respond_status(c, RESP_405, keep_alive);
    goto finish;
  }
  qmark = strchr(path, '?');
  query = NULL;
  if (qmark) {
    *qmark = '\0';
    query = qmark + 1;
  }

  if (method_kind == HTTP_METHOD_POST || method_kind == HTTP_METHOD_SUBSCRIBE || method_kind == HTTP_METHOD_UNSUBSCRIBE) {
    dispatch_dlna_subprotocol(epfd, c, path, method_kind, headers, num_headers, body_len, keep_alive);
    return;
  }

  filter.count = 0;
  if (!reactor_cfg()->no_pid_filters) pid_filter_parse_query(query, &filter);
  pmt_pid = pmt_select_parse_query(query);
  lcevc_select_parse_query(reactor_cfg()->no_lcevc ? NULL : query, &lcevc);
  want_auth = special_path_auth(path);
  if (want_auth) {
    char authz_buf[200];
    const char *authz = find_header(headers, num_headers, "Authorization", authz_buf, sizeof authz_buf) ? authz_buf : NULL;
    if (!http_auth_ok(want_auth, authz)) {
      respond_401(c, keep_alive);
      goto finish;
    }
  }

  if (dispatch_special_path(c, path, is_head, headers, num_headers, keep_alive, &filter, &lcevc, query))
    goto finish;

  if (route_parse(path, &rt) || route_disabled(&rt)) {
    respond_status(c, RESP_404, keep_alive);
    goto finish;
  }

  switch (rt.fmt) {
    case ROUTE_FMT_TS:
    case ROUTE_FMT_SPTS:
    case ROUTE_FMT_RAWAUDIO:
      rr = dispatch_route_ts(c, &rt, &filter, pmt_pid, &lcevc, is_head, keep_alive);
      break;
    case ROUTE_FMT_HLS:
    case ROUTE_FMT_HLS_FMP4:
      rr = dispatch_route_hls(c, &rt, &filter, pmt_pid, &lcevc, is_head, keep_alive, if_none_match, origin_hdr);
      break;
    case ROUTE_FMT_LLHLS:
      rr = dispatch_route_llhls(c, &rt, &filter, pmt_pid, &lcevc, is_head, keep_alive, if_none_match, origin_hdr, query);
      break;
    case ROUTE_FMT_DASH:
    case ROUTE_FMT_LLDASH: {
      char host_buf[128];
      const char *host_hdr = find_header(headers, num_headers, "Host", host_buf, sizeof host_buf) ? host_buf : NULL;
      rr = dispatch_route_dash(c, &rt, &filter, pmt_pid, &lcevc, is_head, keep_alive, origin_hdr, host_hdr);
      break;
    }
    case ROUTE_FMT_MP4:
      rr = dispatch_route_mp4(c, &rt, &filter, pmt_pid, &lcevc, is_head, keep_alive, origin_hdr);
      break;
    default:
      respond_status(c, RESP_501, keep_alive);
      rr = ROUTE_RESULT_FINISH;
      break;
  }
  if (rr == ROUTE_RESULT_PARKED) return;
  if (rr == ROUTE_RESULT_OPENING) {
    if (qmark) *qmark = '?';
    path[path_len] = path_end;
    return;
  }

finish:
  c->state = CONN_WRITING;
  reactor_finish(epfd, c);
}

/* strict: single all-digit Content-Length, no Transfer-Encoding. else BAD (TE+CL) or UNSUPPORTED (TE) */
body_framing_t request_body_len(const struct phr_header *headers, size_t num_headers, size_t *body_len) {
  int have_cl = 0;
  int have_te = 0;
  *body_len = 0;
  for (size_t i = 0; i < num_headers; i++) {
    const struct phr_header *h = &headers[i];
    if (h->name_len == 17 && !strncasecmp(h->name, "Transfer-Encoding", 17)) {
      have_te = 1;
    } else if (h->name_len == 14 && !strncasecmp(h->name, "Content-Length", 14)) {
      size_t v = 0;
      if (have_cl || h->value_len == 0 || h->value_len > 9) return BODY_FRAMING_BAD;
      for (size_t k = 0; k < h->value_len; k++) {
        if (h->value[k] < '0' || h->value[k] > '9') return BODY_FRAMING_BAD;
        v = v * 10 + (size_t)(h->value[k] - '0');
      }
      have_cl = 1;
      *body_len = v;
    }
  }
  if (have_te) return have_cl ? BODY_FRAMING_BAD : BODY_FRAMING_UNSUPPORTED;
  return BODY_FRAMING_OK;
}
