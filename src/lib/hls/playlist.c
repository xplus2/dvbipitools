/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "playlist.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/helper/ioutil.h"
#include "m3u_lines.h"

static int attr_find(const char *line, const char *name, char *out, size_t outcap) {
  size_t nl = strlen(name);
  const char *p = line;
  while ((p = strstr(p, name)) != NULL) {
    if ((p == line || p[-1] == ',') && p[nl] == '=') {
      const char *v = p + nl + 1;
      const char *end;
      size_t len;
      if (*v == '"') {
        v++;
        end = strchr(v, '"');
        if (!end) return 0;
      } else {
        end = v;
        while (*end && *end != ',') end++;
      }
      len = (size_t)(end - v);
      if (len >= outcap) len = outcap - 1;
      memcpy(out, v, len);
      out[len] = '\0';
      return 1;
    }
    p += nl;
  }
  return 0;
}

#define TAG_MATCH(line, tag) (!strncmp((line), (tag), sizeof(tag) - 1))
#define TAG_VALUE(line, tag) ((line) + sizeof(tag) - 1)

int hls_playlist_parse(char *body, const http_url_t *base, hls_playlist_t *out) {
  char *cur = playlist_skip_blank(body);
  const char *line;
  int have_extinf = 0;

  memset(out, 0, sizeof *out);
  if (strncmp(cur, "#EXTM3U", 7)) return 0;
  while ((line = playlist_next_line(&cur)) != NULL) {
    hls_segment_t *seg;
    if (!*line) continue;
    if (TAG_MATCH(line, "#EXT-X-TARGETDURATION:")) {
      out->target_duration = (unsigned)strtoul(TAG_VALUE(line, "#EXT-X-TARGETDURATION:"), NULL, 10);
      continue;
    }
    if (TAG_MATCH(line, "#EXT-X-MEDIA-SEQUENCE:")) {
      out->media_sequence = strtoull(TAG_VALUE(line, "#EXT-X-MEDIA-SEQUENCE:"), NULL, 10);
      continue;
    }
    if (TAG_MATCH(line, "#EXT-X-ENDLIST")) {
      out->endlist = 1;
      continue;
    }
    if (TAG_MATCH(line, "#EXT-X-PART:") || TAG_MATCH(line, "#EXT-X-PRELOAD-HINT:")) {
      out->low_latency = 1;
      continue;
    }
    if (TAG_MATCH(line, "#EXT-X-SERVER-CONTROL:")) {
      char v[8];
      if (attr_find(TAG_VALUE(line, "#EXT-X-SERVER-CONTROL:"), "CAN-BLOCK-RELOAD", v, sizeof v) && !strcasecmp(v, "YES")) out->low_latency = 1;
      continue;
    }
    if (TAG_MATCH(line, "#EXT-X-MAP:")) {
      char uri[sizeof out->map_uri];
      if (attr_find(TAG_VALUE(line, "#EXT-X-MAP:"), "URI", uri, sizeof uri)) playlist_resolve_uri(base, uri, out->map_uri, sizeof out->map_uri);
      continue;
    }
    if (TAG_MATCH(line, "#EXTINF:")) {
      have_extinf = 1;
      continue;
    }
    if (line[0] == '#') continue;
    if (!have_extinf) continue;
    have_extinf = 0;
    if (out->n_segments >= HLS_MAX_SEGMENTS) continue;
    seg = &out->segments[out->n_segments];
    playlist_resolve_uri(base, line, seg->url, sizeof seg->url);
    if (seg->url[0]) out->n_segments++;
  }
  return 1;
}

int hls_body_is_master(const char *body) {
  const char *p = body;
  while ((p = strstr(p, "#EXT-X-STREAM-INF")) != NULL) {
    if (p == body || p[-1] == '\n') return 1;
    p++;
  }
  return 0;
}

int hls_master_parse(char *body, const http_url_t *base, hls_master_t *out) {
  char *cur = playlist_skip_blank(body);
  const char *line;
  int pending = 0;
  unsigned pending_bw = 0;
  unsigned pending_w = 0;
  unsigned pending_h = 0;
  char pending_audio_group[64];

  memset(out, 0, sizeof *out);
  pending_audio_group[0] = '\0';
  if (strncmp(cur, "#EXTM3U", 7)) return 0;
  while ((line = playlist_next_line(&cur)) != NULL) {
    if (!*line) continue;
    if (TAG_MATCH(line, "#EXT-X-MEDIA:")) {
      char type[16];
      char gid[64];
      char uri[2048];
      const char *attrs = TAG_VALUE(line, "#EXT-X-MEDIA:");
      if (attr_find(attrs, "TYPE", type, sizeof type) && !strcasecmp(type, "AUDIO") &&
          attr_find(attrs, "GROUP-ID", gid, sizeof gid) && attr_find(attrs, "URI", uri, sizeof uri) &&
          out->n_audio_renditions < HLS_MAX_VARIANTS) {
        hls_audio_rendition_t *ar = &out->audio_renditions[out->n_audio_renditions];
        bufcpy(ar->group_id, sizeof ar->group_id, gid);
        playlist_resolve_uri(base, uri, ar->url, sizeof ar->url);
        if (ar->url[0]) out->n_audio_renditions++;
      }
      continue;
    }
    if (TAG_MATCH(line, "#EXT-X-STREAM-INF:")) {
      char v[32];
      const char *attrs = TAG_VALUE(line, "#EXT-X-STREAM-INF:");
      pending = 1;
      pending_bw = attr_find(attrs, "BANDWIDTH", v, sizeof v) ? (unsigned)strtoul(v, NULL, 10) : 0;
      pending_w = pending_h = 0;
      if (attr_find(attrs, "RESOLUTION", v, sizeof v)) {
        char *x = strchr(v, 'x');
        if (x) {
          *x = '\0';
          pending_w = (unsigned)strtoul(v, NULL, 10);
          pending_h = (unsigned)strtoul(x + 1, NULL, 10);
        }
      }
      if (!attr_find(attrs, "AUDIO", pending_audio_group, sizeof pending_audio_group)) pending_audio_group[0] = '\0';
      continue;
    }
    if (line[0] == '#') continue;
    if (!pending) continue;
    pending = 0;
    if (out->n_variants >= HLS_MAX_VARIANTS) continue;
    hls_variant_t *v = &out->variants[out->n_variants];
    playlist_resolve_uri(base, line, v->url, sizeof v->url);
    if (!v->url[0]) continue;
    v->bandwidth = pending_bw;
    v->width = pending_w;
    v->height = pending_h;
    bufcpy(v->audio_group_id, sizeof v->audio_group_id, pending_audio_group);
    out->n_variants++;
  }
  return out->n_variants > 0;
}

const hls_audio_rendition_t *hls_master_find_audio(const hls_master_t *m, const char *group_id) {
  unsigned i;
  if (!group_id || !group_id[0]) return NULL;
  for (i = 0; i < m->n_audio_renditions; i++) if (!strcmp(m->audio_renditions[i].group_id, group_id)) return &m->audio_renditions[i];
  return NULL;
}

int hls_master_pick_highest(const hls_master_t *m) {
  int best = -1;
  unsigned long long best_bw = 0;
  unsigned long long best_area = 0;
  for (unsigned i = 0; i < m->n_variants; i++) {
    unsigned long long bw = m->variants[i].bandwidth;
    unsigned long long area = (unsigned long long)m->variants[i].width * m->variants[i].height;
    if (best < 0 || bw > best_bw || (bw == best_bw && area > best_area)) {
      best = (int)i;
      best_bw = bw;
      best_area = area;
    }
  }
  return best;
}
