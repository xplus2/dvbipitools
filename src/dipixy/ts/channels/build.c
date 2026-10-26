/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/bim/sdns.h"
#include "lib/net/dvbstp.h"
#include "lib/net/dvbstp_seen.h"
#include "lib/net/multicast.h"
#include "lib/helper/playlist_in.h"
#include "lib/helper/uriparse.h"

#include "../../core/route.h"
#include "../../version.h"


static channel_item_t *list_append(channel_list_t *l) {
  channel_item_t *it;
  void *p = array_grow(l->items, &l->cap, l->count + 1, sizeof *l->items);
  if (!p) return NULL;
  l->items = p;
  it = &l->items[l->count++];
  memset(it, 0, sizeof *it);
  return it;
}

static void list_from_playlist(channel_list_t *l, const playlist_list_t *pl, int insecure_tls) {
  for (int i = 0; i < pl->count; i++) {
    channel_item_t *it = list_append(l);
    if (!it) return;
    it->name = strdup(pl->items[i].name);
    it->uri = strdup(pl->items[i].uri);
    it->icon_uri = pl->items[i].icon_uri ? strdup(pl->items[i].icon_uri) : NULL;
    it->tsid = pl->items[i].tsid;
    it->onid = pl->items[i].onid;
    it->sid = pl->items[i].sid;
    it->insecure_tls = insecure_tls;
  }
}

void build_from_m3u(channel_list_t *l, const char *path, int insecure_tls) {
  playlist_list_t *pl = playlist_in_parse_m3u(path);
  if (!pl) {
    log_code("YW000677", TOOL_NAME ": --m3u %s: cannot open/parse, list left empty", path);
    return;
  }
  list_from_playlist(l, pl, insecure_tls);
  playlist_list_free(pl);
}

void build_from_xspf(channel_list_t *l, const char *path, int insecure_tls) {
  playlist_list_t *pl = playlist_in_parse_xspf(path);
  if (!pl) {
    log_code("YW000678", TOOL_NAME ": --xspf %s: cannot open/parse, list left empty", path);
    return;
  }
  list_from_playlist(l, pl, insecure_tls);
  playlist_list_free(pl);
}

void build_from_csv(channel_list_t *l, const char *path, int insecure_tls) {
  playlist_list_t *pl = playlist_in_parse_csv(path);
  if (!pl) {
    log_code("YW000679", TOOL_NAME ": --csv %s: cannot open/parse, list left empty", path);
    return;
  }
  list_from_playlist(l, pl, insecure_tls);
  playlist_list_free(pl);
}

/* shared by build_from_sds()/build_from_xml(): sds_parse_broadcast() output to channel_item_t */
static void append_sds_entries(channel_list_t *l, const sds_service_t *entries, int count) {
  for (int i = 0; i < count; i++) {
    char uribuf[160];
    channel_item_t *it = list_append(l);
    if (!it) break;
    uriparse_mcast_src_uri(uribuf, sizeof uribuf, entries[i].rtp ? "rtp" : "udp", entries[i].family, entries[i].source, entries[i].address, entries[i].port);
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
    it->has_fec = entries[i].has_fec;
    it->fec = entries[i].fec;
  }
}

/* SD&S BroadcastDiscovery XML file, e.g. dipiscan -f xml output */
void build_from_xml(channel_list_t *l, const char *path) {
  char *xml;
  size_t len;
  sds_service_t entries[SDS_MAX_SERVICES];
  int count;
  int rc = read_path(path, &xml, &len);
  if (rc == -1) {
    log_code("YW000680", TOOL_NAME ": --xml %s: cannot open, list left empty", path);
    return;
  }
  if (rc) {
    log_code("YW000681", TOOL_NAME ": --xml %s: cannot read, list left empty", path);
    return;
  }
  count = sds_parse_broadcast(xml, entries, SDS_MAX_SERVICES, NULL);
  append_sds_entries(l, entries, count);
  free(xml);
  if (count == 0) log_code("YW000682", TOOL_NAME ": --xml %s: no services found, list left empty", path);
}

void build_from_http(channel_list_t *l, const char *url, int insecure_tls) {
  channel_item_t *it = list_append(l);
  if (!it) return;
  it->name = strdup(url);
  it->uri = strdup(url);
  it->insecure_tls = insecure_tls;
}

/* -i rtp://addr:port, -i udp://addr:port:, lazy join */
void build_from_mcast(channel_list_t *l, const char *uri) {
  channel_item_t *it = list_append(l);
  if (!it) return;
  it->name = strdup(uri);
  it->uri = strdup(uri);
}

void channels_join_all(channel_list_t *l, const config_t *cfg) {
  if (!cfg->join_all) return;
  for (int i = 0; i < l->count; i++) {
    channel_item_t *it = &l->items[i];
    int family;
    int rtp;
    char addr[64];
    char src[64];
    unsigned port;
    if (channel_uri_is_static(it->uri)) continue;
    if (route_resolve_channel_uri(it->uri, &family, addr, sizeof addr, src, sizeof src, &port, &rtp)) continue;
    it->join_ctx = capture_open(family, addr, src, port, cfg->iface, rtp, it->has_ret && !cfg->no_ret ? &it->ret : NULL,
      it->has_fcc && !cfg->no_fcc ? &it->fcc : NULL, it->has_fec && !cfg->no_al_fec ? &it->fec : NULL, cfg->al_fec_l, cfg->al_fec_d);
    if (!it->join_ctx) log_code("YW600158", TOOL_NAME ": --join-all: %s unreachable, left unjoined", it->uri);
  }
}

/* blocks up to timeout_s. appends discovered services into l, no clear */
typedef struct {
  channel_list_t *l;
  const char *addrport;
  const char *ns;
  const bim_sdnsdoc_t *sch; /* NULL: compiled-in schemas */
  dvbstp_seen_t seen[LISTEN_SEEN_MAX];
  int seen_count;
  int total;
  sds_service_t entries[SDS_MAX_SERVICES];
} sds_listen_t;

static int sds_segment(void *ctx, const dvbstp_header_t *hdr, const unsigned char *data, size_t len) {
  sds_listen_t *c = ctx;
  const char *xml;
  size_t xlen;
  char *owned;
  int count;
  int rc;
  if (hdr->payload_id != DVBSTP_PAYLOAD_BROADCAST_DISCOVERY || dvbstp_already_seen(c->seen, &c->seen_count, hdr)) return 0;
  rc = sdns_doc_segment_xml(c->sch, hdr->compr, data, len, c->ns, &xml, &xlen, &owned);
  if (rc == -1) log_code("YW400243", TOOL_NAME ": --sds %s: segment %u: BiM-encoded SD&S not decodable, dropped", c->addrport, hdr->segment_id);
  if (rc == -2) log_code("YW400244", TOOL_NAME ": --sds %s: segment %u: compression %u not supported, dropped", c->addrport, hdr->segment_id, hdr->compr);
  if (rc) return 0;
  count = sds_parse_broadcast(xml, c->entries, SDS_MAX_SERVICES, NULL);
  free(owned);
  append_sds_entries(c->l, c->entries, count);
  c->total += count;
  return 0;
}

void build_from_sds(channel_list_t *l, const char *addrport, const config_t *cfg) {
  int family;
  char group[64];
  unsigned port;
  mcast_t *m;
  sds_listen_t *c;
  bim_sdnsdoc_t sch;
  if (argutil_addrport_parse(addrport, &family, group, sizeof group, &port)) {
    log_code("YE000683", TOOL_NAME ": --sds %s: invalid address, list left empty", addrport);
    return;
  }
  if (cfg->nschemas && sdns_doc_open(&sch, "YS", cfg->schemas, cfg->nschemas, cfg->sdns_ns)) {
    log_code("YW600160", TOOL_NAME ": --sds %s: schema not usable, list left empty", addrport);
    return;
  }
  m = mcast_open(family, group, port, cfg->iface, 500);
  if (!m) {
    log_code("YW500223", TOOL_NAME ": --sds %s: cannot join, list left empty", addrport);
    return;
  }
  c = calloc(1, sizeof *c);
  if (!c) {
    mcast_close(m);
    return;
  }
  c->l = l;
  c->addrport = addrport;
  c->ns = cfg->sdns_ns;
  c->sch = cfg->nschemas ? &sch : NULL;
  dvbstp_listen(m, cfg->sds_timeout_s, sds_segment, c);
  mcast_close(m);
  if (c->total == 0) log_code("YW600159", TOOL_NAME ": --sds %s: no services discovered in %.0fs", addrport, cfg->sds_timeout_s);
  if (cfg->nschemas) bim_sdnsdoc_close(&sch);
  free(c);
}
