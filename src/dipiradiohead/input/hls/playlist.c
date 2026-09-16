/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "playlist.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/helper/ioutil.h"
#include "../playlist.h"

static int is_url_line(const char *l) { return !strncasecmp(l, "http://", 7) || !strncasecmp(l, "https://", 8); }

int hls_playlist_parse(char *body, const http_url_t *base, hls_playlist_t *out) {
  char *cur = playlist_skip_blank(body);
  char *line;
  int have_extinf = 0;

  memset(out, 0, sizeof *out);
  if (strncmp(cur, "#EXTM3U", 7)) return 0;
  while ((line = playlist_next_line(&cur)) != NULL) {
    if (!*line) continue;
    if (!strncmp(line, "#EXT-X-TARGETDURATION:", 22)) {
      out->target_duration = (unsigned)strtoul(line + 22, NULL, 10);
      continue;
    }
    if (!strncmp(line, "#EXT-X-MEDIA-SEQUENCE:", 22)) {
      out->media_sequence = strtoull(line + 22, NULL, 10);
      continue;
    }
    if (!strncmp(line, "#EXT-X-ENDLIST", 14)) {
      out->endlist = 1;
      continue;
    }
    if (!strncmp(line, "#EXTINF:", 8)) {
      have_extinf = 1;
      continue;
    }
    if (line[0] == '#') continue;
    if (!have_extinf) continue;
    have_extinf = 0;
    if (out->n_segments >= HLS_MAX_SEGMENTS) continue;
    {
      hls_segment_t *seg = &out->segments[out->n_segments];
      if (is_url_line(line)) {
        if (bufcpy(seg->url, sizeof seg->url, line) < sizeof seg->url) out->n_segments++;
      } else if (playlist_resolve_relative(base, line, seg->url, sizeof seg->url)) out->n_segments++;
    }
  }
  return 1;
}
