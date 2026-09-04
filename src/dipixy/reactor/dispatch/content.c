/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "../../core/htdocs.h"
#include "../../core/metrics.h"
#include "../../core/status.h"
#include "../../dlna/dlna.h"
#include "../../dlna/gena.h"

#include "lib/helper/ioutil.h"

#include <stdlib.h>
#include <string.h>

void serve_metrics(conn_t *c, int is_head, int keep_alive) {
  char *body;
  size_t len;
  char hdr[192];
  size_t n;

  if (dipixy_metrics_render_prometheus(&body, &len)) {
    respond_status(c, RESP_501, keep_alive);
    return;
  }
  n = build_ok_header(hdr, sizeof hdr, "text/plain; version=0.0.4", len, keep_alive);
  conn_queue(c, hdr, n);
  if (!is_head)
    conn_queue(c, body, len);
  c->keep_alive = keep_alive ? 1 : 0;
  c->close_after_flush = keep_alive ? 0 : 1;
}

void serve_status(conn_t *c, int is_head, int keep_alive) {
  char *body;
  size_t len;
  char hdr[192];
  size_t n;

  if (dipixy_status_render_json(reactor_cfg(), &body, &len)) {
    respond_status(c, RESP_501, keep_alive);
    return;
  }
  n = build_ok_header(hdr, sizeof hdr, "application/json", len, keep_alive);
  conn_queue(c, hdr, n);
  if (!is_head)
    conn_queue(c, body, len);
  c->keep_alive = keep_alive ? 1 : 0;
  c->close_after_flush = keep_alive ? 0 : 1;
}

void serve_playlist(conn_t *c, route_fmt_t fmt, playlist_type_t ptype, const char *host_hdr, const char *query, const pid_filter_t *filter, int is_head, int keep_alive) {
  char *body;
  size_t len;
  char hdr[192];
  size_t n;
  const char *mime;
  if (playlist_render(reactor_cfg(), reactor_channels(), c->ssl != NULL, host_hdr, query, filter, fmt, ptype, &body, &len)) {
    respond_status(c, RESP_501, keep_alive);
    return;
  }
  mime = playlist_query_has_flag(query, "plain") ? "text/plain; charset=utf-8" : ptype == PLAYLIST_M3U ? "audio/x-mpegurl" : "application/xspf+xml";
  n = build_ok_header(hdr, sizeof hdr, mime, len, keep_alive);
  conn_queue(c, hdr, n);
  if (!is_head) conn_queue(c, body, len);
  free(body);
  c->keep_alive = keep_alive ? 1 : 0;
  c->close_after_flush = keep_alive ? 0 : 1;
}

void serve_dlna_xml(conn_t *c, const char *body, size_t len, int is_head, int keep_alive) {
  char hdr[128];
  size_t n = build_ok_header(hdr, sizeof hdr, "text/xml; charset=utf-8", len, keep_alive);
  conn_queue(c, hdr, n);
  if (!is_head)
    conn_queue(c, body, len);
  c->keep_alive = keep_alive ? 1 : 0;
  c->close_after_flush = keep_alive ? 0 : 1;
}

void serve_dlna_desc(conn_t *c, int is_head, int keep_alive) {
  char *body;
  size_t len;
  if (dlna_device_desc_xml(reactor_cfg(), &body, &len)) {
    respond_status(c, RESP_501, keep_alive);
    return;
  }
  serve_dlna_xml(c, body, len, is_head, keep_alive);
}

void serve_dlna_cd_scpd(conn_t *c, int is_head, int keep_alive) {
  const char *body;
  size_t len;
  dlna_cd_scpd_xml(&body, &len);
  serve_dlna_xml(c, body, len, is_head, keep_alive);
}

void serve_dlna_cm_scpd(conn_t *c, int is_head, int keep_alive) {
  const char *body;
  size_t len;
  dlna_cm_scpd_xml(&body, &len);
  serve_dlna_xml(c, body, len, is_head, keep_alive);
}

void serve_dlna_control(conn_t *c, const char *service, const struct phr_header *headers, size_t num_headers, const char *body, size_t body_len, int keep_alive) {
  char soapaction_raw[256], action[64];
  char *resp;
  size_t resp_len;
  int status;
  if (!find_header(headers, num_headers, "SOAPACTION", soapaction_raw, sizeof soapaction_raw) || dlna_soap_action_name(soapaction_raw, action, sizeof action)) {
    respond_status(c, RESP_400, keep_alive);
    return;
  }
  status = dlna_handle_control(reactor_cfg(), reactor_channels(), service, action, body, body_len, &resp, &resp_len);
  {
    char hdr[160];
    strbuf_t b;
    dispatch_sb_init(&b, hdr, sizeof hdr);
    dispatch_sb_add(&b, "HTTP/1.1 ");
    dispatch_sb_add_u64(&b, (uint64_t)status);
    dispatch_sb_add(&b, status == 200 ? " OK" : " Internal Server Error");
    dispatch_sb_add(&b, "\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: ");
    dispatch_sb_add_u64(&b, (uint64_t)resp_len);
    dispatch_sb_add(&b, "\r\nConnection: ");
    dispatch_sb_add(&b, keep_alive ? "keep-alive" : "close");
    dispatch_sb_add(&b, "\r\n\r\n");
    conn_queue(c, hdr, b.len);
    conn_queue(c, resp, resp_len);
  }
  c->keep_alive = keep_alive ? 1 : 0;
  c->close_after_flush = keep_alive ? 0 : 1;
  if (status != 200)
    dipixy_metrics_note_http_error();
}

void serve_dlna_subscribe(conn_t *c, const char *service, const struct phr_header *headers, size_t num_headers, int keep_alive) {
  char callback_buf[600], sid_buf[64], sid[64], hdr[192];
  const char *callback, *sid_hdr;
  strbuf_t b;

  callback = find_header(headers, num_headers, "CALLBACK", callback_buf, sizeof callback_buf) ? callback_buf : NULL;
  sid_hdr = find_header(headers, num_headers, "SID", sid_buf, sizeof sid_buf) ? sid_buf : NULL;
  if (sid_hdr)
    gena_renew(sid_hdr, sid, sizeof sid);
  else
    gena_subscribe_new(reactor_cfg(), service, callback, sid, sizeof sid);
  dispatch_sb_init(&b, hdr, sizeof hdr);
  dispatch_sb_add(&b, "HTTP/1.1 200 OK\r\nSID: ");
  dispatch_sb_add(&b, sid);
  dispatch_sb_add(&b, "\r\nTIMEOUT: Second-1800\r\nContent-Length: 0\r\nConnection: ");
  dispatch_sb_add(&b, keep_alive ? "keep-alive" : "close");
  dispatch_sb_add(&b, "\r\n\r\n");
  conn_queue(c, hdr, b.len);
  c->keep_alive = keep_alive ? 1 : 0;
  c->close_after_flush = keep_alive ? 0 : 1;
}

void serve_dlna_unsubscribe(conn_t *c, const struct phr_header *headers, size_t num_headers, int keep_alive) {
  char sid_buf[64], hdr[96];
  const char *sid_hdr = find_header(headers, num_headers, "SID", sid_buf, sizeof sid_buf) ? sid_buf : NULL;
  strbuf_t b;
  gena_unsubscribe(sid_hdr);
  dispatch_sb_init(&b, hdr, sizeof hdr);
  dispatch_sb_add(&b, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: ");
  dispatch_sb_add(&b, keep_alive ? "keep-alive" : "close");
  dispatch_sb_add(&b, "\r\n\r\n");
  conn_queue(c, hdr, b.len);
  c->keep_alive = keep_alive ? 1 : 0;
  c->close_after_flush = keep_alive ? 0 : 1;
}

void serve_htdocs_index(conn_t *c, int is_head, int keep_alive) {
  char hdr[128];
  const char *body;
  size_t len;
  size_t n;
  htdocs_get(&body, &len);
  n = build_ok_header(hdr, sizeof hdr, "text/html; charset=utf-8", len, keep_alive);
  conn_queue(c, hdr, n);
  if (!is_head)
    conn_queue(c, body, len);
  c->keep_alive = keep_alive ? 1 : 0;
  c->close_after_flush = keep_alive ? 0 : 1;
}
