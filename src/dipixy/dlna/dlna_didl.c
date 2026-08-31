/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE /* fopencookie */

#include "dlna_int.h"

#include "lib/helper/ioutil.h"
#include "lib/helper/uriparse.h"
#include "lib/helper/xml_util.h"

#include "../core/route.h"
#include "../version.h"
#include "gena.h"

#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#define VIDEO_BROADCAST_CLASS "object.item.videoItem.videoBroadcast"
#define AUDIO_BROADCAST_CLASS "object.item.audioItem.audioBroadcast"

static unsigned root_child_count(const config_t *cfg) {
  return (unsigned)cfg->n_sources + (cfg->stdin_ordinal ? 1u : 0u) + (cfg->rist_ordinal ? 1u : 0u);
}

static void didl_container_open(FILE *f, const char *id, const char *parent, unsigned child_count) {
  fprintf(f, "<container id=\"%s\" parentID=\"%s\" restricted=\"1\" searchable=\"0\" childCount=\"%u\">", id, parent, child_count);
}

static void didl_title_class(FILE *f, const char *title, const char *upnp_class) {
  fputs("<dc:title>", f);
  xml_escape(f, title);
  fprintf(f, "</dc:title><upnp:class>%s</upnp:class>", upnp_class);
}

static void didl_container_close(FILE *f) {
  fputs("</container>", f);
}

static int didl_mcast_res(FILE *f, const config_t *cfg, const char *src_uri, unsigned bitrate_bps) {
  int family, rtp;
  char addr[64], hostport[80];
  unsigned port;

  if (!cfg->dlna_keep_multicast || !src_uri)
    return -1;
  if (route_resolve_channel_uri(src_uri, &family, addr, sizeof addr, &port, &rtp))
    return -1;
  uriparse_mcast_describe(family, addr, port, hostport, sizeof hostport);
  fputs(family == AF_INET6 ? "<res protocolInfo=\"" MCAST_MLD_PROTOCOL_INFO "\"" : "<res protocolInfo=\"" MCAST_IGMP_PROTOCOL_INFO "\"", f);
  if (bitrate_bps) fprintf(f, " bitrate=\"%u\"", bitrate_bps);
  fputs(">", f);
  fputs(rtp ? "rtp://" : "udp://", f);
  xml_escape(f, hostport);
  fputs("</res>", f);
  return 0;
}

typedef struct {
  const char *src_uri;
  const char *name;
  unsigned tsid, onid, sid;
  unsigned max_bitrate_kbps;
  int has_bitrate;
  unsigned content_nibble;
  int has_content_nibble;
} didl_item_meta_t;

static void didl_item(FILE *f, const config_t *cfg, const char *id, const char *parent, const char *title, const char *path, const didl_item_meta_t *meta, media_type_t media_type, const char *icon_uri) {
  unsigned bitrate_bps = meta && meta->has_bitrate ? meta->max_bitrate_kbps * 1000 / 8 : 0;
  fprintf(f, "<item id=\"%s\" parentID=\"%s\" restricted=\"1\">", id, parent);
  didl_title_class(f, title, media_type == MEDIA_RADIO ? AUDIO_BROADCAST_CLASS : VIDEO_BROADCAST_CLASS);
  if (icon_uri && *icon_uri) {
    fputs("<upnp:albumArtURI>", f);
    xml_escape(f, icon_uri);
    fputs("</upnp:albumArtURI>", f);
  }
  if (didl_mcast_res(f, cfg, meta ? meta->src_uri : NULL, bitrate_bps)) {
    fputs("<res protocolInfo=\"" TS_PROTOCOL_INFO "\"", f);
    if (bitrate_bps)
      fprintf(f, " bitrate=\"%u\"", bitrate_bps);
    fputs(">http://", f);
    xml_escape(f, cfg->dlna_host);
    xml_escape(f, path);
    fputs("</res>", f);
  }
  fputs("</item>", f);
}

typedef struct {
  char *buf;
  size_t cap;
  int got;
} first_name_ctx_t;

static void capture_first_name(void *vctx, const channel_item_t *item) {
  first_name_ctx_t *c = vctx;
  if (!c->got) {
    bufcpy(c->buf, c->cap, item->name);
    c->got = 1;
  }
}

typedef struct {
  unsigned target, idx;
  char name[192], uri[256], icon[512];
  unsigned tsid, onid, sid;
  unsigned max_bitrate_kbps;
  int has_bitrate;
  unsigned content_nibble;
  int has_content_nibble;
  int found;
} item_lookup_ctx_t;

static void item_lookup_cb(void *vctx, const channel_item_t *item) {
  item_lookup_ctx_t *c = vctx;
  c->idx++;
  if (c->idx == c->target) {
    bufcpy(c->name, sizeof c->name, item->name);
    bufcpy(c->uri, sizeof c->uri, item->uri);
    bufcpy(c->icon, sizeof c->icon, item->icon_uri ? item->icon_uri : "");
    c->tsid = item->tsid;
    c->onid = item->onid;
    c->sid = item->sid;
    c->max_bitrate_kbps = item->max_bitrate_kbps;
    c->has_bitrate = item->has_bitrate;
    c->content_nibble = item->content_nibble;
    c->has_content_nibble = item->has_content_nibble;
    c->found = 1;
  }
}

typedef struct {
  FILE *f;
  const config_t *cfg;
  unsigned ord;
  unsigned skip, take;
  char parent[24];
  unsigned idx, emitted;
  media_type_t media_type;
} list_walk_t;

static void list_emit_item(void *vctx, const channel_item_t *item) {
  list_walk_t *w = vctx;
  w->idx++;
  if (w->idx <= w->skip || (w->take && w->emitted >= w->take))
    return;

  {
    char id[32], path[224], title[192];
    didl_item_meta_t meta = {.src_uri = item->uri,
                              .name = item->name,
                              .tsid = item->tsid,
                              .onid = item->onid,
                              .sid = item->sid,
                              .max_bitrate_kbps = item->max_bitrate_kbps,
                              .has_bitrate = item->has_bitrate,
                              .content_nibble = item->content_nibble,
                              .has_content_nibble = item->has_content_nibble};
    {
      strbuf_t b;
      sb_init(&b, id, sizeof id);
      sb_add(&b, "L");
      sb_add_u64(&b, w->ord);
      sb_add(&b, "I");
      sb_add_u64(&b, w->idx);
    }
    build_play_path(w->cfg, OID_ITEM, w->ord, w->idx, w->media_type, path, sizeof path);
    {
      strbuf_t b;
      sb_init(&b, title, sizeof title);
      sb_add(&b, "#");
      sb_add_u64(&b, w->idx);
      sb_add(&b, " ");
      sb_add(&b, item->name && *item->name ? item->name : strip_scheme_at(item->uri));
    }
    didl_item(w->f, w->cfg, id, w->parent, title, path, &meta, w->media_type, item->icon_uri);
  }
  w->emitted++;
}

typedef struct {
  FILE *f;
  const config_t *cfg;
  const channels_t *channels;
  unsigned skip, take;
  unsigned idx, emitted;
} root_walk_t;

static int root_walk_should_emit(root_walk_t *w) {
  if (w->idx <= w->skip)
    return 0;
  if (w->take && w->emitted >= w->take)
    return 0;
  return 1;
}

static void root_emit_stdin(root_walk_t *w) {
  w->idx++;
  if (!root_walk_should_emit(w))
    return;
  {
    char path[224];
    build_play_path(w->cfg, OID_STDIN, 0, 0, w->cfg->stdin_media_type, path, sizeof path);
    didl_item(w->f, w->cfg, "stdin", "0", w->cfg->stdin_name ? w->cfg->stdin_name : "stdin", path, NULL, w->cfg->stdin_media_type, NULL);
  }
  w->emitted++;
}

static void root_emit_rist(root_walk_t *w) {
  w->idx++;
  if (!root_walk_should_emit(w))
    return;

  {
    char path[224];
    build_play_path(w->cfg, OID_RIST, 0, 0, w->cfg->rist_media_type, path, sizeof path);
    didl_item(w->f, w->cfg, "rist", "0", w->cfg->rist_name ? w->cfg->rist_name : "rist", path, NULL, w->cfg->rist_media_type, NULL);
  }
  w->emitted++;
}

static void root_emit_source(root_walk_t *w, const source_def_t *src) {
  w->idx++;
  if (!root_walk_should_emit(w))
    return;
  if (src->kind == SRC_HTTP) {
    char id[24], path[224], title[192];
    strbuf_t b;
    sb_init(&b, id, sizeof id);
    sb_add(&b, "H");
    sb_add_u64(&b, (uint64_t)src->ordinal);
    build_play_path(w->cfg, OID_HTTP, (unsigned)src->ordinal, 0, src->media_type, path, sizeof path);
    sb_init(&b, title, sizeof title);
    sb_add(&b, "#");
    sb_add_u64(&b, (uint64_t)src->ordinal);
    sb_add(&b, " ");
    if (src->name) {
      sb_add(&b, src->name);
    } else {
      char first[192] = "";
      first_name_ctx_t tc = {first, sizeof first, 0};
      channels_list_for_each(w->channels, (unsigned)src->ordinal, capture_first_name, &tc);
      sb_add_n(&b, first, sizeof title - 13);
    }
    didl_item(w->f, w->cfg, id, "0", title, path, NULL, src->media_type, NULL);
  } else {
    char id[24], title[192];
    unsigned count = (unsigned)channels_list_for_each(w->channels, (unsigned)src->ordinal, NULL, NULL);
    strbuf_t b;
    sb_init(&b, id, sizeof id);
    sb_add(&b, "L");
    sb_add_u64(&b, (uint64_t)src->ordinal);
    sb_init(&b, title, sizeof title);
    if (src->name) {
      sb_add(&b, "#");
      sb_add_u64(&b, (uint64_t)src->ordinal);
      sb_add(&b, " ");
      sb_add(&b, src->name);
    } else {
      sb_add(&b, "Playlist #");
      sb_add_u64(&b, (uint64_t)src->ordinal);
      sb_add(&b, " [");
      sb_add(&b, source_kind_str(src->kind));
      sb_add(&b, "]");
    }
    didl_container_open(w->f, id, "0", count);
    didl_title_class(w->f, title, "object.container.storageFolder");
    didl_container_close(w->f);
  }
  w->emitted++;
}

static void browse_root_children(root_walk_t *w) {
  int ord, si, max_ord;
  max_ord = w->cfg->stdin_ordinal;
  if (w->cfg->rist_ordinal > max_ord)
    max_ord = w->cfg->rist_ordinal;
  if (w->cfg->n_sources > 0 && w->cfg->sources[w->cfg->n_sources - 1].ordinal > max_ord)
    max_ord = w->cfg->sources[w->cfg->n_sources - 1].ordinal;
  si = 0;
  for (ord = 1; ord <= max_ord; ord++) {
    if (ord == w->cfg->stdin_ordinal) {
      root_emit_stdin(w);
    } else if (ord == w->cfg->rist_ordinal) {
      root_emit_rist(w);
    } else if (si < w->cfg->n_sources && w->cfg->sources[si].ordinal == ord) {
      root_emit_source(w, &w->cfg->sources[si]);
      si++;
    }
  }
}

static _Thread_local gbuf_t t_didl_gbuf;

int build_didl(const config_t *cfg, const channels_t *channels, const oid_t *oid, int metadata, unsigned starting_index, unsigned requested_count, char **out_didl, unsigned *number_returned, unsigned *total_matches) {
  FILE *f = gbuf_open(&t_didl_gbuf);
  if (!f)
    return -1;

  fputs("<DIDL-Lite xmlns=\"urn:schemas-upnp-org:didl-lite\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">", f);
  switch (oid->kind) {
    case OID_ROOT:
      if (metadata) {
        didl_container_open(f, "0", "-1", root_child_count(cfg));
        didl_title_class(f, TOOL_NAME, "object.container.storageFolder");
        didl_container_close(f);
        *number_returned = 1;
        *total_matches = 1;
      } else {
        root_walk_t w = {f, cfg, channels, starting_index, requested_count, 0, 0};
        browse_root_children(&w);
        *number_returned = w.emitted;
        *total_matches = root_child_count(cfg);
      }
      break;
    case OID_STDIN:
    case OID_RIST: {
      const char *id = oid->kind == OID_STDIN ? "stdin" : "rist";
      const char *name = oid->kind == OID_STDIN ? cfg->stdin_name : cfg->rist_name;
      media_type_t media_type = oid->kind == OID_STDIN ? cfg->stdin_media_type : cfg->rist_media_type;
      if (!metadata) {
        *number_returned = 0;
        *total_matches = 0;
        break;
      }
      {
        char path[224];
        build_play_path(cfg, oid->kind, 0, 0, media_type, path, sizeof path);
        didl_item(f, cfg, id, "0", name ? name : id, path, NULL, media_type, NULL);
      }
      *number_returned = 1;
      *total_matches = 1;
      break;
    }
    case OID_HTTP: {
      const source_def_t *src = find_source(cfg, oid->ord);
      char title[192];
      if (!src || src->kind != SRC_HTTP) {
        fclose(f);
        return -1;
      }
      if (!metadata) {
        *number_returned = 0;
        *total_matches = 0;
        break;
      }
      {
        char id[24], path[224];
        strbuf_t b;
        sb_init(&b, id, sizeof id);
        sb_add(&b, "H");
        sb_add_u64(&b, oid->ord);
        build_play_path(cfg, OID_HTTP, oid->ord, 0, src->media_type, path, sizeof path);
        sb_init(&b, title, sizeof title);
        sb_add(&b, "#");
        sb_add_u64(&b, oid->ord);
        sb_add(&b, " ");
        if (src->name) {
          sb_add(&b, src->name);
        } else {
          char first[192];
          first_name_ctx_t tc = {first, sizeof first, 0};
          first[0] = '\0';
          channels_list_for_each(channels, oid->ord, capture_first_name, &tc);
          sb_add_n(&b, first, sizeof title - 13);
        }
        didl_item(f, cfg, id, "0", title, path, NULL, src->media_type, NULL);
      }
      *number_returned = 1;
      *total_matches = 1;
      break;
    }
    case OID_LIST: {
      const source_def_t *src = find_source(cfg, oid->ord);
      if (!src || src->kind == SRC_HTTP) {
        fclose(f);
        return -1;
      }
      if (metadata) {
        char id[24], title[192];
        unsigned count = (unsigned)channels_list_for_each(channels, oid->ord, NULL, NULL);
        strbuf_t b;
        sb_init(&b, id, sizeof id);
        sb_add(&b, "L");
        sb_add_u64(&b, oid->ord);
        sb_init(&b, title, sizeof title);
        if (src->name) {
          sb_add(&b, "#");
          sb_add_u64(&b, oid->ord);
          sb_add(&b, " ");
          sb_add(&b, src->name);
        } else {
          sb_add(&b, "Playlist #");
          sb_add_u64(&b, (uint64_t)src->ordinal);
          sb_add(&b, " [");
          sb_add(&b, source_kind_str(src->kind));
          sb_add(&b, "]");
        }
        didl_container_open(f, id, "0", count);
        didl_title_class(f, title, "object.container.storageFolder");
        didl_container_close(f);
        *number_returned = 1;
        *total_matches = 1;
      } else {
        list_walk_t w;
        memset(&w, 0, sizeof w);
        w.f = f;
        w.cfg = cfg;
        w.ord = oid->ord;
        w.skip = starting_index;
        w.take = requested_count;
        w.media_type = src->media_type;
        {
          strbuf_t b;
          sb_init(&b, w.parent, sizeof w.parent);
          sb_add(&b, "L");
          sb_add_u64(&b, oid->ord);
        }
        *total_matches = (unsigned)channels_list_for_each(channels, oid->ord, list_emit_item, &w);
        *number_returned = w.emitted;
      }
      break;
    }
    case OID_ITEM: {
      const source_def_t *src = find_source(cfg, oid->ord);
      item_lookup_ctx_t lk;
      if (!src || src->kind == SRC_HTTP) {
        fclose(f);
        return -1;
      }
      memset(&lk, 0, sizeof lk);
      lk.target = oid->item_num;
      channels_list_for_each(channels, oid->ord, item_lookup_cb, &lk);
      if (!lk.found) {
        fclose(f);
        return -1;
      }
      if (!metadata) {
        *number_returned = 0;
        *total_matches = 0;
        break;
      }
      {
        char id[32], parent[24], path[224], title[192];
        didl_item_meta_t meta = {.src_uri = lk.uri,
                                  .name = lk.name,
                                  .tsid = lk.tsid,
                                  .onid = lk.onid,
                                  .sid = lk.sid,
                                  .max_bitrate_kbps = lk.max_bitrate_kbps,
                                  .has_bitrate = lk.has_bitrate,
                                  .content_nibble = lk.content_nibble,
                                  .has_content_nibble = lk.has_content_nibble};
        {
          strbuf_t b;
          sb_init(&b, id, sizeof id);
          sb_add(&b, "L");
          sb_add_u64(&b, oid->ord);
          sb_add(&b, "I");
          sb_add_u64(&b, oid->item_num);
          sb_init(&b, parent, sizeof parent);
          sb_add(&b, "L");
          sb_add_u64(&b, oid->ord);
        }
        build_play_path(cfg, OID_ITEM, oid->ord, oid->item_num, src->media_type, path, sizeof path);
        {
          strbuf_t b;
          sb_init(&b, title, sizeof title);
          sb_add(&b, "#");
          sb_add_u64(&b, oid->item_num);
          sb_add(&b, " ");
          sb_add_n(&b, lk.name[0] ? lk.name : strip_scheme_at(lk.uri), sizeof title - 13);
        }
        didl_item(f, cfg, id, parent, title, path, &meta, src->media_type, lk.icon[0] ? lk.icon : NULL);
      }
      *number_returned = 1;
      *total_matches = 1;
      break;
    }
    default:
      fclose(f);
      return -1;
  }

  fputs("</DIDL-Lite>", f);
  fclose(f);
  *out_didl = t_didl_gbuf.buf;
  return 0;
}
