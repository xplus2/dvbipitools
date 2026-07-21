/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/log.h"
#include "lib/net/httpclient.h"

#include "../framer/aac_adts.h"
#include "../framer/aac_latm.h"
#include "../framer/mpegaudio.h"
#include "../version.h"
#include "icy.h"
#include "id3.h"
#include "playlist.h"
#include "source.h"

#define SRC_BUF_CAP 16384
#define SRC_SNIFF_CAP 2048
#define SRC_MAX_HOPS 5

struct source {
  http_t *http;
  icy_t *icy; /* NULL: no icy-metaint, ID3-only metadata */
  id3_t *id3;

  int codec_known;
  source_codec_t codec;
  aac_latm_t *latm;

  unsigned char buf[SRC_BUF_CAP];
  size_t buf_len;
  size_t pending_consume; /* bytes of the last returned frame, dropped at the next call */
};

static ssize_t sniff_fill(http_t *h, unsigned char *buf, size_t cap) {
  size_t got = 0;
  int stalls = 0;

  while (got < cap && stalls < 3) {
    ssize_t n = http_read(h, buf + got, cap - got);
    if (n < 0)
      return got > 0 ? (ssize_t)got : -1; /* connection close after a full small (e.g. playlist) body is not an error */
    if (n == 0) {
      stalls++;
      continue;
    }
    got += (size_t)n;
    stalls = 0;
  }
  return (ssize_t)got;
}

static int refill(source_t *s) {
  unsigned char tmp[4096];
  ssize_t n = http_read(s->http, tmp, sizeof tmp);
  size_t clean_cap, produced;

  if (n < 0)
    return -1;
  if (n == 0)
    return 0;

  clean_cap = SRC_BUF_CAP - s->buf_len;
  if (s->icy) {
    produced = icy_feed(s->icy, tmp, (size_t)n, s->buf + s->buf_len, clean_cap);
  } else {
    produced = (size_t)n < clean_cap ? (size_t)n : clean_cap;
    memcpy(s->buf + s->buf_len, tmp, produced);
  }
  s->buf_len += produced;
  return 1;
}

/* 0: not a tag, -1: hard error, 1: is a tag, need more bytes, 2: tag consumed */
static int try_consume_tag(source_t *s) {
  size_t need;

  if (!id3_is_tag(s->buf, s->buf_len))
    return 0;
  if (s->buf_len < 10)
    return 1;
  need = id3_tag_size(s->buf, s->buf_len);
  if (need > SRC_BUF_CAP) {
    log_line("source: ID3 tag too large (%zu bytes)", need);
    return -1;
  }
  if (s->buf_len < need)
    return 1;
  id3_consume(s->id3, s->buf, need);
  memmove(s->buf, s->buf + need, s->buf_len - need);
  s->buf_len -= need;
  return 2;
}

source_t *source_open(const char *uri, int insecure, source_meta_cb cb, void *ctx) {
  char cur_uri[2048];
  int hops;

  snprintf(cur_uri, sizeof cur_uri, "%s", uri);
  for (hops = 0; hops < SRC_MAX_HOPS; hops++) {
    http_url_t u;
    http_t *h;
    unsigned char sniff[SRC_SNIFF_CAP];
    ssize_t got;
    char next[2048];
    source_t *s;
    if (http_url_parse(cur_uri, &u)) {
      log_line("source: invalid uri: %s", cur_uri);
      return NULL;
    }
    h = http_get(&u, TOOL_NAME "/" TOOL_VERSION, insecure);
    if (!h)
      return NULL;

    got = sniff_fill(h, sniff, sizeof sniff);
    if (got <= 0) {
      log_line("source: empty response from %s", cur_uri);
      http_close(h);
      return NULL;
    }

    if (playlist_extract(sniff, (size_t)got, next, sizeof next)) {
      http_close(h);
      snprintf(cur_uri, sizeof cur_uri, "%s", next);
      continue;
    }

    s = calloc(1, sizeof *s);
    if (!s) {
      http_close(h);
      return NULL;
    }
    s->http = h;
    s->id3 = id3_new(cb, ctx);
    if (!s->id3) {
      http_close(h);
      free(s);
      return NULL;
    }
    {
      const char *metaint_hdr = http_header(h, "icy-metaint");
      size_t metaint = metaint_hdr ? strtoul(metaint_hdr, NULL, 10) : 0;
      if (metaint) {
        s->icy = icy_new(metaint, cb, ctx);
        if (!s->icy) {
          id3_free(s->id3);
          http_close(h);
          free(s);
          return NULL;
        }
      }
    }

    if (s->icy)
      s->buf_len = icy_feed(s->icy, sniff, (size_t)got, s->buf, sizeof s->buf);
    else {
      s->buf_len = (size_t)got < sizeof s->buf ? (size_t)got : sizeof s->buf;
      memcpy(s->buf, sniff, s->buf_len);
    }
    return s;
  }
  log_line("source: too many playlist redirects");
  return NULL;
}

/* confirms a sync word at buf+frame_len too, since an 11/12-bit sync can appear by chance in compressed audio */
static int next_sync_ok(source_t *s, size_t frame_len) {
  const unsigned char *p = s->buf + frame_len;
  size_t avail = s->buf_len - frame_len;

  if (s->codec == SRC_MPEG_AUDIO)
    return mpegaudio_is_sync(p, avail);
  if (s->codec == SRC_AAC_ADTS)
    return aac_adts_is_sync(p, avail);
  return aac_latm_is_sync(p, avail);
}

int source_next_frame(source_t *s, source_frame_t *out) {
  if (s->pending_consume) {
    memmove(s->buf, s->buf + s->pending_consume, s->buf_len - s->pending_consume);
    s->buf_len -= s->pending_consume;
    s->pending_consume = 0;
  }

  for (;;) {
    int tr = try_consume_tag(s);
    if (tr == -1)
      return -1;
    if (tr == 1) {
      int rf = refill(s);
      if (rf <= 0)
        return rf;
      continue;
    }
    if (tr == 2)
      continue;

    if (!s->codec_known) {
      if (s->buf_len < 2) {
        int rf = refill(s);
        if (rf <= 0)
          return rf;
        continue;
      }
      if (aac_latm_is_sync(s->buf, s->buf_len)) {
        s->codec = SRC_AAC_LATM;
        s->latm = aac_latm_new();
        if (!s->latm)
          return -1;
      } else if (aac_adts_is_sync(s->buf, s->buf_len)) {
        s->codec = SRC_AAC_ADTS;
      } else if (mpegaudio_is_sync(s->buf, s->buf_len)) {
        s->codec = SRC_MPEG_AUDIO;
      } else {
        log_line("source: unrecognized audio sync (%02x %02x)", s->buf[0], s->buf[1]);
        return -1;
      }
      s->codec_known = 1;
    }

    {
      int r = 0;
      size_t frame_len = 0;
      unsigned sample_rate = 0, samples = 0, stream_type = 0;

      if (s->codec == SRC_MPEG_AUDIO) {
        mpegaudio_info_t info;
        r = mpegaudio_probe(s->buf, s->buf_len, &info);
        if (r == 1) {
          frame_len = info.frame_len;
          sample_rate = info.sample_rate;
          samples = info.samples_per_frame;
          stream_type = 0x03;
        }
      } else if (s->codec == SRC_AAC_ADTS) {
        aac_adts_info_t info;
        r = aac_adts_probe(s->buf, s->buf_len, &info);
        if (r == 1) {
          frame_len = info.frame_len;
          sample_rate = info.sample_rate;
          samples = info.samples_per_frame;
          stream_type = 0x0F;
        }
      } else {
        aac_latm_info_t info;
        r = aac_latm_probe(s->latm, s->buf, s->buf_len, &info);
        if (r == 1) {
          frame_len = info.frame_len;
          sample_rate = info.sample_rate;
          samples = info.samples_per_frame;
          stream_type = 0x11;
        }
      }

      if (r == 0) {
        int rf = refill(s);
        if (rf <= 0)
          return rf;
        continue;
      }
      if (r < 0) {
        if (s->buf_len == 0) {
          int rf = refill(s);
          if (rf <= 0)
            return rf;
          continue;
        }
        memmove(s->buf, s->buf + 1, s->buf_len - 1);
        s->buf_len -= 1;
        continue;
      }
      if (frame_len > s->buf_len) {
        if (frame_len > SRC_BUF_CAP) {
          log_line("source: frame too large (%zu bytes)", frame_len);
          return -1;
        }
        int rf = refill(s);
        if (rf <= 0)
          return rf;
        continue;
      }
      if (s->buf_len < frame_len + 2) {
        int rf = refill(s);
        if (rf <= 0)
          return rf;
        continue;
      }
      if (!next_sync_ok(s, frame_len)) {
        memmove(s->buf, s->buf + 1, s->buf_len - 1);
        s->buf_len -= 1;
        continue;
      }
      out->codec = s->codec;
      out->stream_type = stream_type;
      out->sample_rate = sample_rate;
      out->samples = samples;
      out->data = s->buf;
      out->len = frame_len;
      s->pending_consume = frame_len;
      return 1;
    }
  }
}

void source_close(source_t *s) {
  if (!s)
    return;
  if (s->latm)
    aac_latm_free(s->latm);
  if (s->icy)
    icy_free(s->icy);
  if (s->id3)
    id3_free(s->id3);
  if (s->http)
    http_close(s->http);
  free(s);
}
