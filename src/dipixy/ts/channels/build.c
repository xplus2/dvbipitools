/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/net/dvbstp.h"
#include "lib/net/dvbstp_seen.h"
#include "lib/net/multicast.h"
#include "lib/helper/playlist_in.h"
#include "lib/helper/signal.h"
#include "lib/helper/uriparse.h"

#include "../../core/route.h"
#include "../../version.h"

#define SDS_RECV_BUF 65536

static channel_item_t *list_append(channel_list_t *l) {
  channel_item_t *it;
  void *p = array_grow(l->items, &l->cap, l->count + 1, sizeof *l->items);
  if (!p)
    return NULL;
  l->items = p;
  it = &l->items[l->count++];
  memset(it, 0, sizeof *it);
  return it;
}

/* srt/http(s) entries get static_ctx. rtp/udp entries stay NULL */
static void playlist_item_open_static(channel_item_t *it, int insecure_tls) {
  if (!strncmp(it->uri, "srt://", 6)) {
    char host[256];
    unsigned port;
    if (!route_parse_srt_uri(it->uri, host, sizeof host, &port))
      it->static_ctx = capture_open_srt(host, port);
  } else if (!strncmp(it->uri, "http://", 7) || !strncmp(it->uri, "https://", 8)) {
    it->static_ctx = capture_open_http_static(it->uri, insecure_tls);
  }
}

static void list_from_playlist(channel_list_t *l, const playlist_list_t *pl, int insecure_tls) {
  int i;
  for (i = 0; i < pl->count; i++) {
    channel_item_t *it = list_append(l);
    if (!it)
      return;
    it->name = strdup(pl->items[i].name);
    it->uri = strdup(pl->items[i].uri);
    it->icon_uri = pl->items[i].icon_uri ? strdup(pl->items[i].icon_uri) : NULL;
    it->tsid = pl->items[i].tsid;
    it->onid = pl->items[i].onid;
    it->sid = pl->items[i].sid;
    playlist_item_open_static(it, insecure_tls);
  }
}

void build_from_m3u(channel_list_t *l, const char *path, int insecure_tls) {
  playlist_list_t *pl = playlist_in_parse_m3u(path);
  if (!pl) {
    log_line(TOOL_NAME ": --m3u %s: cannot open/parse, list left empty", path);
    return;
  }
  list_from_playlist(l, pl, insecure_tls);
  playlist_list_free(pl);
}

void build_from_xspf(channel_list_t *l, const char *path, int insecure_tls) {
  playlist_list_t *pl = playlist_in_parse_xspf(path);
  if (!pl) {
    log_line(TOOL_NAME ": --xspf %s: cannot open/parse, list left empty", path);
    return;
  }
  list_from_playlist(l, pl, insecure_tls);
  playlist_list_free(pl);
}

void build_from_csv(channel_list_t *l, const char *path, int insecure_tls) {
  playlist_list_t *pl = playlist_in_parse_csv(path);
  if (!pl) {
    log_line(TOOL_NAME ": --csv %s: cannot open, list left empty", path);
    return;
  }
  list_from_playlist(l, pl, insecure_tls);
  playlist_list_free(pl);
}

/* shared by build_from_sds()/build_from_xml(): sds_parse_broadcast() output to channel_item_t */
static void append_sds_entries(channel_list_t *l, const sds_service_t *entries, int count) {
  int i;
  for (i = 0; i < count; i++) {
    char addrbuf[80], uribuf[96];
    channel_item_t *it = list_append(l);
    if (!it)
      break;
    uriparse_mcast_describe(entries[i].family, entries[i].address, entries[i].port, addrbuf, sizeof addrbuf);
    {
      size_t off = bufcpy(uribuf, sizeof uribuf, entries[i].rtp ? "rtp" : "udp");
      off += bufcpy(uribuf + off, sizeof uribuf - off, "://@");
      bufcpy(uribuf + off, sizeof uribuf - off, addrbuf);
    }
    it->name = strdup(entries[i].name);
    it->uri = strdup(uribuf);
    it->tsid = entries[i].tsid;
    it->onid = entries[i].onid;
    it->sid = entries[i].sid;
    it->max_bitrate_kbps = entries[i].max_bitrate_kbps;
    it->has_bitrate = entries[i].has_bitrate;
    it->content_nibble = entries[i].content_nibble;
    it->has_content_nibble = entries[i].has_content_nibble;
    it->has_ret = entries[i].has_ret;
    it->ret = entries[i].ret;
    it->has_fcc = entries[i].has_fcc;
    it->fcc = entries[i].fcc;
  }
}

/* SD&S BroadcastDiscovery XML file, e.g. dipiscan -f xml output */
void build_from_xml(channel_list_t *l, const char *path) {
  FILE *f = fopen(path, "r");
  char *xml;
  size_t len;
  sds_service_t entries[SDS_MAX_SERVICES];
  int count;
  if (!f) {
    log_line(TOOL_NAME ": --xml %s: cannot open, list left empty", path);
    return;
  }
  if (read_all(f, &xml, &len)) {
    fclose(f);
    log_line(TOOL_NAME ": --xml %s: cannot read, list left empty", path);
    return;
  }
  fclose(f);
  count = sds_parse_broadcast(xml, entries, SDS_MAX_SERVICES, NULL);
  append_sds_entries(l, entries, count);
  free(xml);
  if (count == 0)
    log_line(TOOL_NAME ": --xml %s: no services found, list left empty", path);
}

void build_from_http(channel_list_t *l, const char *url, int insecure_tls) {
  capture_ctx_t *ctx = capture_open_http_static(url, insecure_tls);
  channel_item_t *it;

  if (!ctx)
    return;
  it = list_append(l);
  if (!it) {
    capture_close(ctx);
    return;
  }
  it->name = strdup(url);
  it->uri = strdup(url);
  it->static_ctx = ctx;
}

/* blocks up to timeout_s. appends discovered services into l, no clear */
void build_from_sds(channel_list_t *l, const char *addrport, const char *iface, double timeout_s) {
  int family;
  char group[64];
  unsigned port;
  mcast_t *m;
  dvbstp_reasm_t *r;
  seen_t seen[LISTEN_SEEN_MAX];
  int seen_count = 0;
  double deadline;
  int total = 0;
  if (argutil_addrport_parse(addrport, &family, group, sizeof group, &port)) {
    log_line(TOOL_NAME ": --sds %s: invalid address, list left empty", addrport);
    return;
  }
  m = mcast_open(family, group, port, iface, 500);
  if (!m) {
    log_line(TOOL_NAME ": --sds %s: cannot join, list left empty", addrport);
    return;
  }
  r = dvbstp_reasm_new();
  if (!r) {
    mcast_close(m);
    return;
  }
  deadline = mono_seconds() + timeout_s;
  while (mono_seconds() < deadline && !signal_stop_requested()) {
    unsigned char buf[SDS_RECV_BUF];
    dvbstp_header_t hdr;
    const unsigned char *data;
    size_t len;
    ssize_t n = mcast_recv(m, buf, sizeof buf, NULL);
    if (n <= 0)
      continue;
    if (!dvbstp_reasm_feed(r, buf, (size_t)n, &hdr, &data, &len))
      continue;
    if (hdr.payload_id != DVBSTP_PAYLOAD_BROADCAST_DISCOVERY)
      continue;
    if (already_seen(seen, &seen_count, &hdr))
      continue;
    {
      char *xml = malloc(len + 1);
      sds_service_t entries[SDS_MAX_SERVICES];
      int count;
      if (!xml)
        continue;
      memcpy(xml, data, len);
      xml[len] = '\0';
      count = sds_parse_broadcast(xml, entries, SDS_MAX_SERVICES, NULL);
      append_sds_entries(l, entries, count);
      total += count;
      free(xml);
    }
  }

  dvbstp_reasm_free(r);
  mcast_close(m);
  if (total == 0)
    log_line(TOOL_NAME ": --sds %s: no services discovered in %.0fs", addrport, timeout_s);
}
