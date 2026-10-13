/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* non-stream paths for h2/h3, matches reactor/dispatch/dispatch.c + content.c */

#define _GNU_SOURCE
#if defined(HAVE_HTTP2) || defined(HAVE_HTTP3)

#include "httpng.h"

#include "../core/htdocs.h"
#include "../core/metrics.h"
#include "../core/playlist.h"
#include "../core/status.h"
#include "../core/time_ep.h"
#include "../dlna/dlna.h"
#include "../dlna/gena.h"

#include "lib/sys/ioutil.h"

#include <stdlib.h>
#include <string.h>

#define CT_XML "text/xml; charset=utf-8"

/* ownership of body for transport, cp for static/thread-local sources */
static void respond_copy(const httpng_ops_t *ops, void *conn, void *req, int status, const char *ctype, const char *body, size_t len, int is_head, const char *origin) {
  uint8_t *copy = NULL;
  if (len && !is_head) {
    copy = malloc(len);
    if (!copy) {
      ops->respond_status(conn, req, "501");
      return;
    }
    memcpy(copy, body, len);
  }
  ops->respond_content(conn, req, status, ctype, NULL, 0, copy, len, origin);
}

static void respond_owned(const httpng_ops_t *ops, void *conn, void *req, const char *ctype, char *body, size_t len, int is_head, const char *origin) {
  if (is_head || !len) {
    free(body);
    body = NULL;
  }
  ops->respond_content(conn, req, 200, ctype, NULL, 0, (uint8_t *)body, len, origin);
}

static int dlna_get(const httpng_ops_t *ops, void *conn, void *req, const char *path, int is_head, const char *origin) {
  char *body;
  size_t len;
  const char *cbody;
  if (!reactor_cfg()->enable_dlna) return 0;
  if (!strcmp(path, "/dlna/desc.xml")) {
    if (dlna_device_desc_xml(reactor_cfg(), &body, &len))
      ops->respond_status(conn, req, "501");
    else
      respond_copy(ops, conn, req, 200, CT_XML, body, len, is_head, origin);
  } else if (!strcmp(path, "/dlna/cd_scpd.xml")) {
    dlna_cd_scpd_xml(&cbody, &len);
    respond_copy(ops, conn, req, 200, CT_XML, cbody, len, is_head, origin);
  } else if (!strcmp(path, "/dlna/cm_scpd.xml")) {
    dlna_cm_scpd_xml(&cbody, &len);
    respond_copy(ops, conn, req, 200, CT_XML, cbody, len, is_head, origin);
  } else {
    return 0;
  }
  return 1;
}

static void export_playlist(const httpng_ops_t *ops, void *conn, void *req, const httpng_req_hdrs_t *hdrs, const char *query, const pid_filter_t *filter, const lcevc_select_t *lcevc, int is_head, const char *origin) {
  route_fmt_t fmt;
  playlist_type_t ptype;
  char *body;
  size_t len;
  if (playlist_path_parse(hdrs->path, &fmt, &ptype) || playlist_fmt_disabled(reactor_cfg(), fmt)) {
    ops->respond_status(conn, req, "404");
    return;
  }
  if (playlist_render(reactor_cfg(), reactor_channels(), 1, hdrs->host[0] ? hdrs->host : NULL, query, filter, lcevc, fmt, ptype, &body, &len)) {
    ops->respond_status(conn, req, "501");
    return;
  }
  respond_owned(ops, conn, req, playlist_mime(query, ptype), body, len, is_head, origin);
}

int httpng_dispatch_special(const httpng_ops_t *ops, void *conn, void *req, const httpng_req_hdrs_t *hdrs, const char *query, const pid_filter_t *filter, const lcevc_select_t *lcevc, int is_head) {
  const char *path = hdrs->path;
  const char *origin = hdrs->origin[0] ? hdrs->origin : NULL;
  char *body;
  size_t len;
  const char *cbody;
  if (dlna_get(ops, conn, req, path, is_head, origin)) return 1;
  if (reactor_cfg()->metrics_http && !strcmp(path, "/metrics")) {
    if (dipixy_metrics_render_prometheus(reactor_metrics(), &body, &len))
      ops->respond_status(conn, req, "501");
    else
      respond_owned(ops, conn, req, "text/plain; version=0.0.4", body, len, is_head, origin);
    return 1;
  }
  if (!strcmp(path, TIME_EP_PATH)) {
    char tbuf[TIME_EP_BODY_MAX];
    size_t tlen = time_ep_render(tbuf, sizeof tbuf);
    if (!tlen)
      ops->respond_status(conn, req, "501");
    else
      respond_copy(ops, conn, req, 200, "text/plain; charset=utf-8", tbuf, tlen, is_head, origin);
    return 1;
  }
  if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
    if (reactor_cfg()->no_status) {
      ops->respond_status(conn, req, "404");
    } else {
      htdocs_get(&cbody, &len);
      respond_copy(ops, conn, req, 200, "text/html; charset=utf-8", cbody, len, is_head, origin);
    }
    return 1;
  }
  if (!reactor_cfg()->no_status && !strcmp(path, "/ui/status.js")) {
    if (dipixy_status_render_json(reactor_cfg(), &body, &len))
      ops->respond_status(conn, req, "501");
    else
      respond_copy(ops, conn, req, 200, "application/json", body, len, is_head, origin);
    return 1;
  }
  if (!strncmp(path, "/export/", 8)) {
    export_playlist(ops, conn, req, hdrs, query, filter, lcevc, is_head, origin);
    return 1;
  }
  return 0;
}

static void dlna_control(const httpng_ops_t *ops, void *conn, void *req, const httpng_req_hdrs_t *hdrs, const char *service) {
  char action[64];
  char *resp;
  size_t resp_len;
  int status;
  if (!hdrs->soapaction[0] || dlna_soap_action_name(hdrs->soapaction, action, sizeof action)) {
    ops->respond_status(conn, req, "400");
    return;
  }
  status = dlna_handle_control(reactor_cfg(), reactor_channels(), service, action, hdrs->body ? (const char *)hdrs->body : "", hdrs->body_len, &resp, &resp_len);
  respond_copy(ops, conn, req, status, CT_XML, resp, resp_len, 0, NULL);
  if (status != 200) dipixy_metrics_note_http_error();
}

static void dlna_gena_status(const httpng_ops_t *ops, void *conn, void *req, int status) {
  const char *text = "503";
  if (status == 400) text = "400";
  else if (status == 412) text = "412";
  ops->respond_status(conn, req, text);
}

static void dlna_subscribe(const httpng_ops_t *ops, void *conn, void *req, const httpng_req_hdrs_t *hdrs, const char *client_ip, gena_svc_t svc) {
  char sid[64];
  char timeout[16];
  httpng_hdr_t extra[HTTPNG_EXTRA_MAX];
  unsigned timeout_s = 0;
  int status = gena_subscribe(reactor_cfg(), svc, client_ip, hdrs->sid, hdrs->callback, hdrs->nt, hdrs->timeout, sid, sizeof sid, &timeout_s);
  if (status != 200) {
    dlna_gena_status(ops, conn, req, status);
    return;
  }
  bufcpy(timeout, sizeof timeout, "Second-");
  uint_to_str(timeout + 7, timeout_s);
  extra[0] = (httpng_hdr_t){"sid", sid};
  extra[1] = (httpng_hdr_t){"timeout", timeout};
  ops->respond_content(conn, req, 200, NULL, extra, 2, NULL, 0, NULL);
}

static void dlna_unsubscribe(const httpng_ops_t *ops, void *conn, void *req, const httpng_req_hdrs_t *hdrs, const char *client_ip, gena_svc_t svc) {
  int status = gena_unsubscribe(svc, client_ip, hdrs->sid, hdrs->callback, hdrs->nt);
  if (status == 200) ops->respond_content(conn, req, 200, NULL, NULL, 0, NULL, 0, NULL);
  else dlna_gena_status(ops, conn, req, status);
}

void httpng_dispatch_dlna(const httpng_ops_t *ops, void *conn, void *req, const httpng_req_hdrs_t *hdrs, const char *client_ip) {
  const char *path = hdrs->path;
  const char *method = hdrs->method;
  int is_cd_event = !strcmp(path, "/dlna/cd_event");
  int is_event = is_cd_event || !strcmp(path, "/dlna/cm_event");
  if (!reactor_cfg()->enable_dlna) {
    ops->respond_status(conn, req, "405");
  } else if (!strcmp(method, "POST")) {
    if (!strcmp(path, "/dlna/cd_control") || !strcmp(path, "/dlna/cm_control"))
      dlna_control(ops, conn, req, hdrs, !strcmp(path, "/dlna/cd_control") ? "cd" : "cm");
    else
      ops->respond_status(conn, req, "405");
  } else if (!is_event) {
    ops->respond_status(conn, req, "405");
  } else if (!strcmp(method, "SUBSCRIBE")) {
    dlna_subscribe(ops, conn, req, hdrs, client_ip, is_cd_event ? GENA_SVC_CD : GENA_SVC_CM);
  } else if (!strcmp(method, "UNSUBSCRIBE")) {
    dlna_unsubscribe(ops, conn, req, hdrs, client_ip, is_cd_event ? GENA_SVC_CD : GENA_SVC_CM);
  } else {
    ops->respond_status(conn, req, "405");
  }
}

#endif /* HAVE_HTTP2 || HAVE_HTTP3 */
