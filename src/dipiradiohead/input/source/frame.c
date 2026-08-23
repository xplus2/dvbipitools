/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/log.h"

#include "../../framer/aac_adts.h"
#include "../../framer/mpegaudio.h"
#include "priv.h"

unsigned long long source_bytes_total(const source_t *s) { return s->bytes_total; }

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

/* 0: not a tag, -1: hard error, 1: tag, need more bytes, 2: tag consumed */
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

/* 1: codec now known (just resolved or already was), caller proceeds this iteration.
   0: refilled, caller should continue its loop. -1: caller should return *ret */
static int ensure_codec_known(source_t *s, net_err_reason_t *reason_out, int *ret) {
  if (s->codec_known)
    return 1;
  if (s->buf_len < 2) {
    int rf = refill(s, reason_out);
    if (rf <= 0) {
      *ret = rf;
      return -1;
    }
    return 0;
  }
  if (aac_latm_is_sync(s->buf, s->buf_len)) {
    s->codec = SRC_AAC_LATM;
    s->latm = aac_latm_new();
    if (!s->latm) {
      if (reason_out)
        *reason_out = NET_ERR_OTHER;
      *ret = -1;
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
    *ret = -1;
    return -1;
  }
  s->codec_known = 1;
  return 1;
}

enum { PROBE_STEP_RETURN, PROBE_STEP_CONTINUE, PROBE_STEP_PROCEED };

/* decides what to do after a codec-specific probe. PROBE_STEP_PROCEED: frame_len is a
   complete, buffered frame ready for the caller's next_sync_ok()/emit step */
static int handle_probe_result(source_t *s, net_err_reason_t *reason_out, int r, size_t frame_len, int *ret) {
  if (r == 0) {
    int rf = refill(s, reason_out);
    if (rf <= 0) {
      *ret = rf;
      return PROBE_STEP_RETURN;
    }
    return PROBE_STEP_CONTINUE;
  }
  if (r < 0) {
    if (s->buf_len == 0) {
      int rf = refill(s, reason_out);
      if (rf <= 0) {
        *ret = rf;
        return PROBE_STEP_RETURN;
      }
      return PROBE_STEP_CONTINUE;
    }
    memmove(s->buf, s->buf + 1, s->buf_len - 1);
    s->buf_len -= 1;
    return PROBE_STEP_CONTINUE;
  }
  if (frame_len > s->buf_len) {
    if (frame_len > SRC_BUF_CAP) {
      log_line("source: frame too large (%zu bytes)", frame_len);
      if (reason_out)
        *reason_out = NET_ERR_FORMAT;
      *ret = -1;
      return PROBE_STEP_RETURN;
    }
    int rf = refill(s, reason_out);
    if (rf <= 0) {
      *ret = rf;
      return PROBE_STEP_RETURN;
    }
    return PROBE_STEP_CONTINUE;
  }
  if (s->buf_len < frame_len + 2) {
    int rf = refill(s, reason_out);
    if (rf <= 0) {
      *ret = rf;
      return PROBE_STEP_RETURN;
    }
    return PROBE_STEP_CONTINUE;
  }
  return PROBE_STEP_PROCEED;
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
      int ret, step = ensure_codec_known(s, reason_out, &ret);
      if (step < 0)
        return ret;
      if (step == 0)
        continue;
    }

    {
      int r = 0, ret, step;
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

      step = handle_probe_result(s, reason_out, r, frame_len, &ret);
      if (step == PROBE_STEP_RETURN)
        return ret;
      if (step == PROBE_STEP_CONTINUE)
        continue;

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
