/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "../../altsvc.h"
#include "../../core/htdocs.h"
#include "../../core/metrics.h"
#include "../../core/status.h"
#include "../../core/time_ep.h"
#include "../../dlna/dlna.h"
#include "../../dlna/gena.h"

#include "lib/sys/ioutil.h"

#include <stdlib.h>
#include <string.h>

void serve_body(conn_t *c, const char *content_type, const char *body, size_t len, int is_head, int keep_alive) {
  char hdr[256];
  size_t n = build_ok_header(hdr, sizeof hdr, content_type, len, keep_alive, c->ssl != NULL);
  conn_queue(c, hdr, n);
  if (!is_head) conn_queue(c, body, len);
  set_persistence(c, keep_alive);
}

void serve_metrics(conn_t *c, int is_head, int keep_alive) {
  char *body;
  size_t len;
  if (dipixy_metrics_render_prometheus(reactor_metrics(), &body, &len)) {
    respond_status(c, RESP_501, keep_alive);
    return;
  }
  serve_body(c, "text/plain; version=0.0.4", body, len, is_head, keep_alive);
  free(body);
}

void serve_status(conn_t *c, int is_head, int keep_alive) {
  char *body;
  size_t len;
  if (dipixy_status_render_json(reactor_cfg(), &body, &len)) {
    respond_status(c, RESP_501, keep_alive);
    return;
  }
  serve_body(c, "application/json", body, len, is_head, keep_alive);
}

void serve_time(conn_t *c, int is_head, int keep_alive) {
  char body[TIME_EP_BODY_MAX];
  size_t len = time_ep_render(body, sizeof body);
  if (!len) {
    respond_status(c, RESP_501, keep_alive);
    return;
  }
  serve_body(c, "text/plain; charset=utf-8", body, len, is_head, keep_alive);
}

void serve_playlist(conn_t *c, route_fmt_t fmt, playlist_type_t ptype, const char *host_hdr, const char *query, const pid_filter_t *filter, const lcevc_select_t *lcevc, int is_head, int keep_alive) {
  char *body;
  size_t len;
  if (playlist_render(reactor_cfg(), reactor_channels(), c->ssl != NULL, host_hdr, query, filter, lcevc, fmt, ptype, &body, &len)) {
    respond_status(c, RESP_501, keep_alive);
    return;
  }
  serve_body(c, playlist_mime(query, ptype), body, len, is_head, keep_alive);
  free(body);
}

void serve_dlna_desc(conn_t *c, int is_head, int keep_alive) {
  char *body;
  size_t len;
  if (dlna_device_desc_xml(reactor_cfg(), &body, &len)) {
    respond_status(c, RESP_501, keep_alive);
    return;
  }
  serve_body(c, "text/xml; charset=utf-8", body, len, is_head, keep_alive);
}

void serve_dlna_cd_scpd(conn_t *c, int is_head, int keep_alive) {
  const char *body;
  size_t len;
  dlna_cd_scpd_xml(&body, &len);
  serve_body(c, "text/xml; charset=utf-8", body, len, is_head, keep_alive);
}

void serve_dlna_cm_scpd(conn_t *c, int is_head, int keep_alive) {
  const char *body;
  size_t len;
  dlna_cm_scpd_xml(&body, &len);
  serve_body(c, "text/xml; charset=utf-8", body, len, is_head, keep_alive);
}

void serve_dlna_control(conn_t *c, const char *service, const struct phr_header *headers, size_t num_headers, const char *body, size_t body_len, int keep_alive) {
  char soapaction_raw[256];
  char action[64];
  char *resp;
  size_t resp_len;
  int status;
  char hdr[224];
  sbuf_t b;
  if (!find_header(headers, num_headers, "SOAPACTION", soapaction_raw, sizeof soapaction_raw) || dlna_soap_action_name(soapaction_raw, action, sizeof action)) {
    respond_status(c, RESP_400, keep_alive);
    return;
  }
  status = dlna_handle_control(reactor_cfg(), reactor_channels(), service, action, body, body_len, &resp, &resp_len);
  sbuf_init(&b, hdr, sizeof hdr);
  sbuf_add(&b, "HTTP/1.1 ");
  sbuf_add_u64(&b, (uint64_t)status);
  sbuf_add(&b, status == 200 ? " OK" : " Internal Server Error");
  sbuf_add(&b, "\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: ");
  sbuf_add_u64(&b, (uint64_t)resp_len);
  sbuf_add(&b, "\r\nConnection: ");
  sbuf_add(&b, keep_alive ? "keep-alive" : "close");
  sbuf_add(&b, "\r\n");
  sbuf_add(&b, altsvc_h1_line(c->ssl != NULL));
  sbuf_add(&b, "\r\n");
  conn_queue(c, hdr, b.len);
  conn_queue(c, resp, resp_len);
  set_persistence(c, keep_alive);
  if (status != 200) dipixy_metrics_note_http_error();
}

static const char *gena_status_line(int status) {
  switch (status) {
    case 400: return RESP_400;
    case 412: return RESP_412;
    default: return RESP_503;
  }
}

static void queue_gena_ok(conn_t *c, const char *sid, unsigned timeout_s, int keep_alive) {
  char hdr[320];
  sbuf_t b;
  sbuf_init(&b, hdr, sizeof hdr);
  sbuf_add(&b, "HTTP/1.1 200 OK\r\n");
  if (sid) {
    sbuf_add(&b, "SID: ");
    sbuf_add(&b, sid);
    sbuf_add(&b, "\r\nTIMEOUT: Second-");
    sbuf_add_uint(&b, timeout_s);
    sbuf_add(&b, "\r\n");
  }
  sbuf_add(&b, "Content-Length: 0\r\nConnection: ");
  sbuf_add(&b, keep_alive ? "keep-alive" : "close");
  sbuf_add(&b, "\r\n");
  sbuf_add(&b, altsvc_h1_line(c->ssl != NULL));
  sbuf_add(&b, "\r\n");
  conn_queue(c, hdr, b.len);
  set_persistence(c, keep_alive);
}

typedef struct {
  char sid[64];
  char callback[256];
  char nt[32];
  char timeout[32];
} gena_hdrs_t;

static void read_gena_headers(const struct phr_header *headers, size_t num_headers, gena_hdrs_t *g) {
  g->sid[0] = '\0';
  g->callback[0] = '\0';
  g->nt[0] = '\0';
  g->timeout[0] = '\0';
  find_header(headers, num_headers, "SID", g->sid, sizeof g->sid);
  find_header(headers, num_headers, "CALLBACK", g->callback, sizeof g->callback);
  find_header(headers, num_headers, "NT", g->nt, sizeof g->nt);
  find_header(headers, num_headers, "TIMEOUT", g->timeout, sizeof g->timeout);
}

void serve_dlna_subscribe(conn_t *c, gena_svc_t svc, const struct phr_header *headers, size_t num_headers, int keep_alive) {
  gena_hdrs_t g;
  char sid[64];
  unsigned timeout_s = 0;
  int status;
  read_gena_headers(headers, num_headers, &g);
  status = gena_subscribe(reactor_cfg(), svc, c->client_ip, g.sid, g.callback, g.nt, g.timeout, sid, sizeof sid, &timeout_s);
  if (status == 200) queue_gena_ok(c, sid, timeout_s, keep_alive);
  else respond_status(c, gena_status_line(status), keep_alive);
}

void serve_dlna_unsubscribe(conn_t *c, gena_svc_t svc, const struct phr_header *headers, size_t num_headers, int keep_alive) {
  gena_hdrs_t g;
  int status;
  read_gena_headers(headers, num_headers, &g);
  status = gena_unsubscribe(svc, c->client_ip, g.sid, g.callback, g.nt);
  if (status == 200) queue_gena_ok(c, NULL, 0, keep_alive);
  else respond_status(c, gena_status_line(status), keep_alive);
}

void serve_htdocs_index(conn_t *c, int is_head, int keep_alive) {
  const char *body;
  size_t len;
  htdocs_get(&body, &len);
  serve_body(c, "text/html; charset=utf-8", body, len, is_head, keep_alive);
}
