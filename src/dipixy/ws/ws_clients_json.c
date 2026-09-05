/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ws_clients_int.h"
#include "ws_broadcast.h"

#include <string.h>

void snapshot_client(ws_client_snapshot_t *dst, const ws_client_t *src) {
  memcpy(dst->ip, src->ip, sizeof dst->ip);
  dst->http_ver = src->http_ver;
  dst->connect_time = src->connect_time;
  dst->fmt = src->fmt;
  dst->pmt_pid = src->pmt_pid;
  memcpy(dst->filter, src->filter, sizeof dst->filter);
  memcpy(dst->src_proto, src->src_proto, sizeof dst->src_proto);
  memcpy(dst->src_addr, src->src_addr, sizeof dst->src_addr);
  dst->src_ordinal = src->src_ordinal;
  memcpy(dst->src_name, src->src_name, sizeof dst->src_name);
  dst->item_num = src->item_num;
  memcpy(dst->item_name, src->item_name, sizeof dst->item_name);
}

static const char *route_fmt_name(route_fmt_t fmt) {
  switch (fmt) {
    case ROUTE_FMT_TS: return "ts";
    case ROUTE_FMT_SPTS: return "spts";
    case ROUTE_FMT_HLS: return "hls";
    case ROUTE_FMT_HLS_FMP4: return "hls-fmp4";
    case ROUTE_FMT_LLHLS: return "llhls";
    case ROUTE_FMT_DASH: return "dash";
    case ROUTE_FMT_LLDASH: return "lldash";
    case ROUTE_FMT_RAWAUDIO: return "rawaudio";
    case ROUTE_FMT_MP4: return "mp4";
  }
  return "?";
}

void jbuf_i64(jbuf_t *j, long long v) {
  char tmp[20], buf[21];
  size_t n = 0, off = 0;
  unsigned long long uv;
  int neg = v < 0;
  uv = neg ? (unsigned long long)(-(v + 1)) + 1ULL : (unsigned long long)v;
  if (!uv) {
    tmp[n++] = '0';
  } else {
    while (uv) {
      tmp[n++] = (char)('0' + uv % 10);
      uv /= 10;
    }
  }
  if (neg)
    buf[off++] = '-';
  for (size_t i = 0; i < n; i++)
    buf[off++] = tmp[n - 1 - i];
  jbuf_raw(j, buf, off);
}

static void jbuf_u64(jbuf_t *j, unsigned long long v) {
  char tmp[20], buf[20];
  size_t n = 0;
  if (!v) {
    tmp[n++] = '0';
  } else {
    while (v) {
      tmp[n++] = (char)('0' + v % 10);
      v /= 10;
    }
  }
  for (size_t i = 0; i < n; i++)
    buf[i] = tmp[n - 1 - i];
  jbuf_raw(j, buf, n);
}

/* 3-decimal fixed point, assumes v >= 0 (mbps, always non-negative here) */
void jbuf_fixed3(jbuf_t *j, double v) {
  uint32_t scaled = (uint32_t)(v * 1000.0 + 0.5);
  jbuf_u64(j, scaled / 1000);
  jbuf_str(j, ".");
  {
    char buf[3];
    unsigned frac = scaled % 1000;
    buf[0] = (char)('0' + frac / 100);
    buf[1] = (char)('0' + (frac / 10) % 10);
    buf[2] = (char)('0' + frac % 10);
    jbuf_raw(j, buf, 3);
  }
}

void emit_client_json(jbuf_t *j, int id, const ws_client_snapshot_t *e) {
  jbuf_str(j, "{\"id\":");
  jbuf_i64(j, id);
  jbuf_str(j, ",\"ip\":");
  jbuf_json_string(j, e->ip);
  jbuf_str(j, ",\"http_ver\":");
  jbuf_i64(j, e->http_ver);
  jbuf_str(j, ",\"connect_time\":");
  jbuf_i64(j, (long long)e->connect_time);
  jbuf_str(j, ",\"fmt\":");
  jbuf_json_string(j, route_fmt_name(e->fmt));
  jbuf_str(j, ",\"pmt\":");
  jbuf_u64(j, e->pmt_pid);
  jbuf_str(j, ",\"filter\":");
  jbuf_json_string(j, e->filter);
  jbuf_str(j, ",\"src_proto\":");
  jbuf_json_string(j, e->src_proto);
  jbuf_str(j, ",\"src_addr\":");
  jbuf_json_string(j, e->src_addr);
  jbuf_str(j, ",\"src_ordinal\":");
  jbuf_i64(j, e->src_ordinal);
  jbuf_str(j, ",\"src_name\":");
  jbuf_json_string(j, e->src_name);
  jbuf_str(j, ",\"item_num\":");
  jbuf_u64(j, e->item_num);
  jbuf_str(j, ",\"item_name\":");
  jbuf_json_string(j, e->item_name);
  jbuf_str(j, "}");
}

void publish_client_event(const char *type, int id) {
  ws_client_snapshot_t snap;
  static _Thread_local jbuf_t j;
  if (!ws_broadcast_has_sinks()) return;
  jbuf_reset(&j);
  if (strcmp(type, "clients.remove") == 0) {
    jbuf_str(&j, "{\"type\":\"clients.remove\",\"id\":");
    jbuf_i64(&j, id);
    jbuf_str(&j, "}");
  } else {
    pthread_mutex_lock(&g_clients_mtx);
    snapshot_client(&snap, &g_clients[id]);
    pthread_mutex_unlock(&g_clients_mtx);
    jbuf_str(&j, "{\"type\":\"");
    jbuf_str(&j, type);
    jbuf_str(&j, "\",\"client\":");
    emit_client_json(&j, id, &snap);
    jbuf_str(&j, "}");
  }
  if (!j.failed)
    ws_broadcast_publish(j.buf);
}
