/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* HLS + LL-HLS serving. Generic segment store engine: ../segstore.h */

#ifndef DIPIXY_HLS_H
#define DIPIXY_HLS_H

#include "../core/route.h"
#include "../segstore.h"

/* f: INDEX, SEG or INIT. queues HTTP/1.1-style status line + headers + body via conn_queue(),
   if_none_match: raw If-None-Match header value or NULL, checked against init.mp4/segments only.
   origin_hdr: raw Origin request header value or NULL, drives Access-Control-Allow-Origin.
   out_bytes: response byte count, NULL ok. 1 handled, 0 other file kind */
int hls_serve(conn_t *c, const stream_key_t *k, const hls_store_t *st, const route_file_t *f, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr, size_t *out_bytes);

/* f: INDEX_LL or PART, PART ext matching k->container. same conn_queue()/
   keep_alive/if_none_match/origin_hdr/out_bytes convention as hls_serve() (if_none_match checked against parts, not index_ll.m3u8) */
int hls_serve_ll(conn_t *c, const stream_key_t *k, const hls_store_t *st, const route_file_t *f, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr, size_t *out_bytes);

/* same lookup as hls_serve() */
int hls_render(const stream_key_t *k, const hls_store_t *st, const route_file_t *f, int is_head, const char *if_none_match, hls_resp_t *out);

/* same lookup as hls_serve_ll() */
int hls_render_ll(const stream_key_t *k, const hls_store_t *st, const route_file_t *f, int is_head, const char *if_none_match, hls_resp_t *out);

#endif
