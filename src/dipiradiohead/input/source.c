/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/ioutil.h"
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
  unsigned long long bytes_total;
};

unsigned long long source_bytes_total(const source_t *s) { return s->bytes_total; }

static ssize_t sniff_fill(http_t *h, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  size_t got = 0;
  int stalls = 0;

  while (got < cap && stalls < 3) {
    ssize_t n = http_read(h, buf + got, cap - got, reason_out);
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

static int refill(source_t *s, net_err_reason_t *reason_out) {
  unsigned char tmp[4096];
  ssize_t n = http_read(s->http, tmp, sizeof tmp, reason_out);
  size_t clean_cap, produced;

  if (n < 0)
    return -1;
  if (n == 0)
    return 0;
  s->bytes_total += (unsigned long long)n;

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

/* h absorbed either way: closed on failure, owned by the returned source_t on success */
static source_t *build_source(http_t *h, const unsigned char *sniff, size_t got, source_meta_cb cb, void *ctx) {
  source_t *s = calloc(1, sizeof *s);
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
    s->buf_len = icy_feed(s->icy, sniff, got, s->buf, sizeof s->buf);
  else {
    s->buf_len = got < sizeof s->buf ? got : sizeof s->buf;
    memcpy(s->buf, sniff, s->buf_len);
  }
  return s;
}

source_t *source_open(const char *uri, int insecure, source_meta_cb cb, void *ctx, net_err_reason_t *reason_out) {
  char cur_uri[2048];
  int hops;

  bufcpy(cur_uri, sizeof cur_uri, uri);
  for (hops = 0; hops < SRC_MAX_HOPS; hops++) {
    http_url_t u;
    http_t *h;
    unsigned char sniff[SRC_SNIFF_CAP];
    ssize_t got;
    char next[2048];
    if (http_url_parse(cur_uri, &u)) {
      log_line("source: invalid uri: %s", cur_uri);
      if (reason_out)
        *reason_out = NET_ERR_FORMAT;
      return NULL;
    }
    h = http_get(&u, TOOL_NAME "/" TOOL_VERSION, insecure, NULL, reason_out);
    if (!h)
      return NULL;

    if (reason_out)
      *reason_out = NET_ERR_TIMEOUT; /* default if sniff_fill stalls out without a harder error */
    got = sniff_fill(h, sniff, sizeof sniff, reason_out);
    if (got <= 0) {
      log_line("source: empty response from %s", cur_uri);
      http_close(h);
      return NULL;
    }

    if (playlist_extract(sniff, (size_t)got, next, sizeof next)) {
      http_close(h);
      bufcpy(cur_uri, sizeof cur_uri, next);
      continue;
    }

    if (reason_out)
      *reason_out = NET_ERR_OTHER; /* build_source() only fails on allocation */
    return build_source(h, sniff, (size_t)got, cb, ctx);
  }
  log_line("source: too many playlist redirects");
  if (reason_out)
    *reason_out = NET_ERR_FORMAT;
  return NULL;
}

typedef enum { SO_FETCHING, SO_SNIFFING } source_open_phase_t;

struct source_open {
  source_open_phase_t phase;
  char cur_uri[2048];
  int hops;
  int insecure;
  source_meta_cb cb;
  void *ctx;
  http_async_t *ha; /* during SO_FETCHING */
  http_t *h;        /* during SO_SNIFFING */
  unsigned char sniff[SRC_SNIFF_CAP];
  size_t sniff_got;
  source_t *result; /* set on SOURCE_OPEN_DONE */
};

static int so_start_fetch(source_open_t *o, net_err_reason_t *reason_out) {
  http_url_t u;

  if (http_url_parse(o->cur_uri, &u)) {
    log_line("source: invalid uri: %s", o->cur_uri);
    if (reason_out)
      *reason_out = NET_ERR_FORMAT;
    return -1;
  }
  o->ha = http_async_start(&u, TOOL_NAME "/" TOOL_VERSION, o->insecure, NULL, reason_out);
  if (!o->ha)
    return -1;
  o->phase = SO_FETCHING;
  return 0;
}

source_open_t *source_open_async_start(const char *uri, int insecure, source_meta_cb cb, void *ctx, net_err_reason_t *reason_out) {
  source_open_t *o = calloc(1, sizeof *o);
  if (!o)
    return NULL;
  bufcpy(o->cur_uri, sizeof o->cur_uri, uri);
  o->insecure = insecure;
  o->cb = cb;
  o->ctx = ctx;
  if (so_start_fetch(o, reason_out) != 0) {
    free(o);
    return NULL;
  }
  return o;
}

int source_open_async_poll_fd(const source_open_t *o) {
  if (o->phase == SO_FETCHING)
    return http_async_poll_fd(o->ha);
  return http_fd(o->h);
}

short source_open_async_poll_events(const source_open_t *o) {
  if (o->phase == SO_FETCHING)
    return http_async_poll_events(o->ha);
  return POLLIN;
}

source_open_state_t source_open_async_step(source_open_t *o, net_err_reason_t *reason_out) {
  if (o->phase == SO_FETCHING) {
    http_async_state_t st = http_async_step(o->ha, reason_out);
    if (st == HTTP_ASYNC_PENDING)
      return SOURCE_OPEN_PENDING;
    if (st == HTTP_ASYNC_ERROR) {
      http_async_free(o->ha);
      o->ha = NULL;
      return SOURCE_OPEN_ERROR;
    }
    o->h = http_async_take(o->ha);
    o->ha = NULL;
    o->phase = SO_SNIFFING;
    o->sniff_got = 0;
  }

  for (;;) {
    ssize_t n;
    if (o->sniff_got >= sizeof o->sniff)
      break; /* buffer full: enough to sniff regardless of what else might follow */
    n = http_read(o->h, o->sniff + o->sniff_got, sizeof o->sniff - o->sniff_got, reason_out);
    if (n < 0) {
      if (o->sniff_got > 0)
        break; /* connection close after a full small (e.g. playlist) body is not an error */
      log_line("source: empty response from %s", o->cur_uri);
      http_close(o->h);
      o->h = NULL;
      return SOURCE_OPEN_ERROR;
    }
    if (n == 0)
      return SOURCE_OPEN_PENDING;
    o->sniff_got += (size_t)n;
  }

  {
    char next[2048];
    if (playlist_extract(o->sniff, o->sniff_got, next, sizeof next)) {
      http_close(o->h);
      o->h = NULL;
      o->hops++;
      if (o->hops >= SRC_MAX_HOPS) {
        log_line("source: too many playlist redirects");
        if (reason_out)
          *reason_out = NET_ERR_FORMAT;
        return SOURCE_OPEN_ERROR;
      }
      bufcpy(o->cur_uri, sizeof o->cur_uri, next);
      if (so_start_fetch(o, reason_out) != 0)
        return SOURCE_OPEN_ERROR;
      return SOURCE_OPEN_PENDING;
    }
  }

  {
    http_t *h = o->h;
    o->h = NULL; /* build_source() absorbs h either way: owns it on success, closes it on failure */
    o->result = build_source(h, o->sniff, o->sniff_got, o->cb, o->ctx);
  }
  if (!o->result) {
    if (reason_out)
      *reason_out = NET_ERR_OTHER; /* build_source() only fails on allocation */
    return SOURCE_OPEN_ERROR;
  }
  return SOURCE_OPEN_DONE;
}

source_t *source_open_async_take(source_open_t *o) {
  source_t *s = o->result;
  free(o);
  return s;
}

void source_open_async_free(source_open_t *o) {
  if (!o)
    return;
  if (o->ha)
    http_async_free(o->ha);
  if (o->h)
    http_close(o->h);
  if (o->result)
    source_close(o->result);
  free(o);
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

int source_next_frame(source_t *s, source_frame_t *out, net_err_reason_t *reason_out) {
  if (s->pending_consume) {
    memmove(s->buf, s->buf + s->pending_consume, s->buf_len - s->pending_consume);
    s->buf_len -= s->pending_consume;
    s->pending_consume = 0;
  }

  for (;;) {
    int tr = try_consume_tag(s);
    if (tr == -1) {
      if (reason_out)
        *reason_out = NET_ERR_FORMAT;
      return -1;
    }
    if (tr == 1) {
      int rf = refill(s, reason_out);
      if (rf <= 0)
        return rf;
      continue;
    }
    if (tr == 2)
      continue;

    if (!s->codec_known) {
      if (s->buf_len < 2) {
        int rf = refill(s, reason_out);
        if (rf <= 0)
          return rf;
        continue;
      }
      if (aac_latm_is_sync(s->buf, s->buf_len)) {
        s->codec = SRC_AAC_LATM;
        s->latm = aac_latm_new();
        if (!s->latm) {
          if (reason_out)
            *reason_out = NET_ERR_OTHER;
          return -1;
        }
      } else if (aac_adts_is_sync(s->buf, s->buf_len)) {
        s->codec = SRC_AAC_ADTS;
      } else if (mpegaudio_is_sync(s->buf, s->buf_len)) {
        s->codec = SRC_MPEG_AUDIO;
      } else {
        log_line("source: unrecognized audio sync (%02x %02x)", s->buf[0], s->buf[1]);
        if (reason_out)
          *reason_out = NET_ERR_FORMAT;
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
        int rf = refill(s, reason_out);
        if (rf <= 0)
          return rf;
        continue;
      }
      if (r < 0) {
        if (s->buf_len == 0) {
          int rf = refill(s, reason_out);
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
          if (reason_out)
            *reason_out = NET_ERR_FORMAT;
          return -1;
        }
        int rf = refill(s, reason_out);
        if (rf <= 0)
          return rf;
        continue;
      }
      if (s->buf_len < frame_len + 2) {
        int rf = refill(s, reason_out);
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

int source_fd(const source_t *s) { return http_fd(s->http); }

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
