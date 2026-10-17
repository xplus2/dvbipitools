/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DISPATCH_PRIV_H
#define DIPIXY_DISPATCH_PRIV_H

#include "../internal.h"
#include "../reactor_tls.h"
#include "../../core/playlist.h"
#include "../../dlna/gena.h"

extern const char RESP_400[];
extern const char RESP_401[];
extern const char RESP_404[];
extern const char RESP_405[];
extern const char RESP_412[];
extern const char RESP_431[];
extern const char RESP_501[];
extern const char RESP_503[];
extern const char RESP_504[];

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
void serve_time(conn_t *c, int is_head, int keep_alive);
void serve_playlist(conn_t *c, route_fmt_t fmt, playlist_type_t ptype, const char *host_hdr, const char *query,
  const pid_filter_t *filter, const lcevc_select_t *lcevc, int is_head, int keep_alive);
void serve_dlna_desc(conn_t *c, int is_head, int keep_alive);
void serve_dlna_cd_scpd(conn_t *c, int is_head, int keep_alive);
void serve_dlna_cm_scpd(conn_t *c, int is_head, int keep_alive);
void serve_dlna_control(conn_t *c, const char *service, const struct phr_header *headers, size_t num_headers, const char *body, size_t body_len, int keep_alive);
void serve_dlna_subscribe(conn_t *c, gena_svc_t svc, const struct phr_header *headers, size_t num_headers, int keep_alive);
void serve_dlna_unsubscribe(conn_t *c, gena_svc_t svc, const struct phr_header *headers, size_t num_headers, int keep_alive);
void serve_htdocs_index(conn_t *c, int is_head, int keep_alive);

/* waiters.c */
int llhls_try_park(conn_t *c, const llhls_park_req_t *req);

/* want_ll: HLS_COLD_DASH only, ROUTE_FMT_LLDASH vs ROUTE_FMT_DASH */
int hls_cold_try_park(conn_t *c, const hls_cold_park_req_t *req);

/* 1 parked, 0 table full: caller serves */
int ts_cold_try_park(conn_t *c, const ts_cold_park_req_t *req);

#define REACTOR_MAX_REQ 8192

typedef enum { HTTP_METHOD_GET, HTTP_METHOD_HEAD, HTTP_METHOD_POST, HTTP_METHOD_SUBSCRIBE, HTTP_METHOD_UNSUBSCRIBE, HTTP_METHOD_OTHER } http_method_t;
typedef enum { ROUTE_RESULT_FINISH, ROUTE_RESULT_PARKED, ROUTE_RESULT_OPENING } route_result_t;
#define REACTOR_MAX_HEADERS 100

typedef enum { BODY_FRAMING_OK, BODY_FRAMING_BAD, BODY_FRAMING_UNSUPPORTED } body_framing_t;
http_method_t parse_http_method(const char *method, size_t method_len);
void dispatch_dlna_subprotocol(int epfd, conn_t *c, const char *path, http_method_t method_kind, const struct phr_header *headers, size_t num_headers, size_t body_len, int keep_alive);
int dispatch_special_path(conn_t *c, const char *path, int is_head, const struct phr_header *headers, size_t num_headers, int keep_alive, const pid_filter_t *filter, const lcevc_select_t *lcevc, const char *query);
route_result_t dispatch_route_ts(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive);
route_result_t dispatch_route_hls(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr);
route_result_t dispatch_route_llhls(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr, const char *query);
route_result_t dispatch_route_dash(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive, const char *origin_hdr, const char *host_hdr);
route_result_t dispatch_route_mp4(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive, const char *origin_hdr);
void reactor_dispatch(int epfd, conn_t *c, const char *method, size_t method_len, const char *path_in, size_t path_len, int minor_version, const struct phr_header *headers, size_t num_headers, size_t header_bytes, size_t body_len);
body_framing_t request_body_len(const struct phr_header *headers, size_t num_headers, size_t *body_len);

#endif
