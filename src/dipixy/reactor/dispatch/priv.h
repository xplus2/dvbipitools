/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DISPATCH_PRIV_H
#define DIPIXY_DISPATCH_PRIV_H

#include "../internal.h"
#include "../reactor_tls.h"
#include "../../core/playlist.h"

extern const char RESP_400[];
extern const char RESP_401[];
extern const char RESP_404[];
extern const char RESP_405[];
extern const char RESP_431[];
extern const char RESP_501[];

/* resp.c */
void respond_status(conn_t *c, const char *status, int keep_alive);
void respond_401(conn_t *c, int keep_alive);
size_t build_ok_header(char *hdr, size_t hdrsz, const char *content_type, size_t body_len, int keep_alive, int is_tls);
size_t build_stream_header(char *hdr, size_t hdrsz, const char *content_type, int is_tls);
int wants_keepalive(int minor_version, const struct phr_header *headers, size_t num_headers);
void set_persistence(conn_t *c, int keep_alive);

/* content.c */
void serve_body(conn_t *c, const char *content_type, const char *body, size_t len, int is_head, int keep_alive);
void serve_metrics(conn_t *c, int is_head, int keep_alive);
void serve_status(conn_t *c, int is_head, int keep_alive);
void serve_playlist(conn_t *c, route_fmt_t fmt, playlist_type_t ptype, const char *host_hdr, const char *query,
  const pid_filter_t *filter, const lcevc_select_t *lcevc, int is_head, int keep_alive);
void serve_dlna_desc(conn_t *c, int is_head, int keep_alive);
void serve_dlna_cd_scpd(conn_t *c, int is_head, int keep_alive);
void serve_dlna_cm_scpd(conn_t *c, int is_head, int keep_alive);
void serve_dlna_control(conn_t *c, const char *service, const struct phr_header *headers, size_t num_headers, const char *body, size_t body_len, int keep_alive);
void serve_dlna_subscribe(conn_t *c, const char *service, const struct phr_header *headers, size_t num_headers, int keep_alive);
void serve_dlna_unsubscribe(conn_t *c, const struct phr_header *headers, size_t num_headers, int keep_alive);
void serve_htdocs_index(conn_t *c, int is_head, int keep_alive);

/* waiters.c */
int llhls_try_park(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename,
  int is_head, int keep_alive, const char *origin_hdr, uint32_t want_seg, int want_part, int timeout_ms, int ws_handle);

/* want_ll: HLS_COLD_DASH only, ROUTE_FMT_LLDASH vs ROUTE_FMT_DASH */
int hls_cold_try_park(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename,
  hls_cold_kind_t kind, seg_container_t container, int want_ll, int is_head, int keep_alive, const char *origin_hdr, int timeout_ms, int ws_handle);

#endif
