/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/log.h"
#include "lib/sys/signal.h"

#include "../../framer/aac_adts.h"
#include "../../framer/mpegaudio.h"
#include "priv.h"

unsigned long long source_bytes_total(const source_t *s) { return s->bytes_total; }

/* queue fill must not block on wire */
static int would_block(const source_t *s) {
  struct pollfd p;
  int buffered = s->hls ? hls_live_has_buffered(s->hls) : http_has_buffered(s->http);
  p.fd = source_fd(s);
  p.events = s->hls ? hls_live_poll_events(s->hls) : POLLIN;
  p.revents = 0;
  if (!s->fq || buffered || p.fd < 0) return 0;
  return poll(&p, 1, 0) <= 0;
}

static int refill(source_t *s, net_err_reason_t *reason_out) {
  unsigned char tmp[4096];
  ssize_t n;
  size_t clean_cap;
  size_t want;
  size_t produced;

  clean_cap = SRC_BUF_CAP - s->buf_len;
  if (clean_cap == 0) {
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return -1;
  }
  if (would_block(s)) return 0;
  want = clean_cap < sizeof tmp ? clean_cap : sizeof tmp;
  n = s->hls ? hls_live_read(s->hls, tmp, want, reason_out) : http_read(s->http, tmp, want, reason_out);

  if (n < 0) return -1;
  if (n == 0) return 0;
  s->bytes_total += (unsigned long long)n;
  s->last_rx = mono_seconds();
  if (s->icy) {
    produced = icy_feed(s->icy, tmp, (size_t)n, s->buf + s->buf_len, clean_cap);
  } else {
    produced = (size_t)n;
    memcpy(s->buf + s->buf_len, tmp, produced);
  }
  s->buf_len += produced;
  return 1;
}

static void drop_front(source_t *s, size_t n) {
  memmove(s->buf, s->buf + n, s->buf_len - n);
  s->buf_len -= n;
}

/* 0: not a tag, 1: tag, need more bytes, 2: tag consumed or skipped */
static int try_consume_tag(source_t *s) {
  size_t need;
  if (s->tag_skip) {
    size_t n = s->tag_skip < s->buf_len ? s->tag_skip : s->buf_len;
    drop_front(s, n);
    s->tag_skip -= n;
    return s->tag_skip ? 1 : 2;
  }
  if (!id3_is_tag(s->buf, s->buf_len)) return 0;
  if (s->buf_len < 10) return 1;
  need = id3_tag_size(s->buf, s->buf_len);
  if (need > SRC_BUF_CAP) {
    if (s->buf_len < SRC_BUF_CAP) return 1;
    id3_consume(s->id3, s->buf, s->buf_len);
    s->tag_skip = need - s->buf_len;
    s->buf_len = 0;
    return 2;
  }
  if (s->buf_len < need) return 1;
  id3_consume(s->id3, s->buf, need);
  memmove(s->buf, s->buf + need, s->buf_len - need);
  s->buf_len -= need;
  return 2;
}

static int codec_is_sync(source_codec_t codec, const unsigned char *p, size_t avail) {
  if (codec == SRC_MPEG_AUDIO) return mpegaudio_is_sync(p, avail);
  if (codec == SRC_AAC_ADTS) return aac_adts_is_sync(p, avail);
  return aac_latm_is_sync(p, avail);
}

/* confirms a sync word at buf+frame_len too, since an 11/12-bit sync can appear by chance in compressed audio */
static int next_sync_ok(const source_t *s, size_t frame_len) {
  return codec_is_sync(s->codec, s->buf + frame_len, s->buf_len - frame_len);
}

/* first accepted sync offset in buf[1..buf_len), cheap codec_is_sync() per byte,
   no repeated probe() calls. buf_len if none: caller drops buffer, waits for more data */
static size_t find_resync_offset(const source_t *s) {
  for (size_t i = 1; i < s->buf_len; i++) {
    if (codec_is_sync(s->codec, s->buf + i, s->buf_len - i)) return i;
  }
  return s->buf_len;
}

static int probe_len(source_t *s, source_codec_t codec, const unsigned char *p, size_t avail, size_t *len) {
  int r;
  if (codec == SRC_MPEG_AUDIO) {
    mpegaudio_info_t info;
    r = mpegaudio_probe(p, avail, &info);
    if (r == 1) *len = info.frame_len;
  } else if (codec == SRC_AAC_ADTS) {
    aac_adts_info_t info;
    r = aac_adts_probe(p, avail, &info);
    if (r == 1) *len = info.frame_len;
  } else {
    aac_latm_info_t info;
    r = aac_latm_probe(s->latm, p, avail, &info);
    if (r == 1) *len = info.frame_len;
  }
  return r;
}

/* 1: valid frame at off followed by another sync word, 0: need more bytes, -1: no */
static int confirm_codec(source_t *s, source_codec_t codec, size_t off) {
  const unsigned char *p = s->buf + off;
  size_t avail = s->buf_len - off;
  size_t len = 0;
  int r;

  if (!codec_is_sync(codec, p, avail)) return -1;
  r = probe_len(s, codec, p, avail, &len);
  if (r <= 0) return r;
  if (off + len + 2 > SRC_BUF_CAP) return -1;
  if (off + len + 2 > s->buf_len) return 0;
  return codec_is_sync(codec, p + len, avail - len) ? 1 : -1;
}

static void reset_detect(source_t *s) {
  if (s->latm) aac_latm_free(s->latm);
  s->latm = NULL;
  s->codec_known = 0;
  s->resync_fails = 0;
  s->detect_skipped = 0;
}

static void resync(source_t *s) {
  drop_front(s, find_resync_offset(s));
  if (++s->resync_fails >= SRC_REDETECT_FAILS) reset_detect(s);
}

static int detect_at(source_t *s, size_t off) {
  static const source_codec_t order[3] = {SRC_AAC_LATM, SRC_AAC_ADTS, SRC_MPEG_AUDIO};
  int pending = 0;

  for (int i = 0; i < 3; i++) {
    int r;
    if (order[i] == SRC_AAC_LATM) {
      if (!aac_latm_is_sync(s->buf + off, s->buf_len - off)) continue;
      if (!s->latm) {
        s->latm = aac_latm_new();
        if (!s->latm) return -2;
      }
    }
    r = confirm_codec(s, order[i], off);
    if (r == 1) {
      s->codec = order[i];
      return 1;
    }
    if (r == 0) pending = 1;
    else if (order[i] == SRC_AAC_LATM) {
      aac_latm_free(s->latm); /* rejected probe may leave stale mux config */
      s->latm = NULL;
    }
  }
  return pending ? 0 : -1;
}

/* 1: codec now known (just resolved or already was), caller proceeds this iteration.
   0: refilled, caller should continue its loop. -1: caller should return *ret */
static int ensure_codec_known(source_t *s, net_err_reason_t *reason_out, int *ret) {
  int pending = 0;
  int rf;

  if (s->codec_known) return 1;
  for (size_t off = 0; off + 1 < s->buf_len; off++) {
    int r = detect_at(s, off);
    if (r == -1) continue;
    if (r == -2) {
      if (reason_out) *reason_out = NET_ERR_OTHER;
      *ret = -1;
      return -1;
    }
    if (off) drop_front(s, off);
    if (r == 1) {
      if (s->codec != SRC_AAC_LATM && s->latm) {
        aac_latm_free(s->latm);
        s->latm = NULL;
      }
      s->codec_known = 1;
      s->resync_fails = 0;
      s->detect_skipped = 0;
      return 1;
    }
    pending = 1;
    break;
  }
  if (!pending && s->buf_len > 1) {
    s->detect_skipped += s->buf_len - 1;
    drop_front(s, s->buf_len - 1);
  }
  if (s->detect_skipped >= SRC_BUF_CAP) {
    log_line_ansi("input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;33munrecognized audio sync\e[0m", s->idx, s->label ? s->label : "?");
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    *ret = -1;
    return -1;
  }
  rf = refill(s, reason_out);
  if (rf <= 0) {
    *ret = rf;
    return -1;
  }
  return 0;
}

enum { PROBE_STEP_RETURN, PROBE_STEP_CONTINUE, PROBE_STEP_PROCEED };

/* decides what to do after a codec-specific probe */
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
    resync(s);
    return PROBE_STEP_CONTINUE;
  }
  if (frame_len > s->buf_len) {
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

static int pull_frame(source_t *s, source_frame_t *out, net_err_reason_t *reason_out) {
  if (s->pending_consume) {
    memmove(s->buf, s->buf + s->pending_consume, s->buf_len - s->pending_consume);
    s->buf_len -= s->pending_consume;
    s->pending_consume = 0;
  }

  for (;;) {
    int tr = try_consume_tag(s);
    if (tr == 1) {
      int rf = refill(s, reason_out);
      if (rf <= 0) return rf;
      continue;
    }
    if (tr == 2) continue;
    if (!s->codec_known) {
      int ret;
      int step = ensure_codec_known(s, reason_out, &ret);
      if (step < 0) return ret;
      if (step == 0) continue;
    }

    {
      int r = 0;
      int ret;
      int step;
      size_t frame_len = 0;
      unsigned sample_rate = 0;
      unsigned samples = 0;
      unsigned stream_type = 0;
      unsigned aac_profile_level = 0;

      if (s->codec == SRC_MPEG_AUDIO) {
        mpegaudio_info_t info;
        r = mpegaudio_probe(s->buf, s->buf_len, &info);
        if (r == 1) {
          frame_len = info.frame_len;
          sample_rate = info.sample_rate;
          samples = info.samples_per_frame;
          stream_type = info.half_rate ? 0x04 : 0x03;
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
          aac_profile_level = info.aac_profile_level;
        }
      }

      step = handle_probe_result(s, reason_out, r, frame_len, &ret);
      if (step == PROBE_STEP_RETURN) return ret;
      if (step == PROBE_STEP_CONTINUE) continue;
      if (!next_sync_ok(s, frame_len)) {
        resync(s);
        continue;
      }
      s->resync_fails = 0;
      out->codec = s->codec;
      out->stream_type = stream_type;
      out->sample_rate = sample_rate;
      out->samples = samples;
      out->aac_profile_level = aac_profile_level;
      out->data = s->buf;
      out->len = frame_len;
      s->pending_consume = frame_len;
      return 1;
    }
  }
}

#define FQ_MAX_FRAMES 4096

int source_set_prefill_ms(source_t *s, unsigned ms) {
  if (!ms) return 0;
  s->fq = framequeue_new();
  if (!s->fq) return -1;
  s->prefill_ms = ms;
  return 0;
}

int source_take_resumed(source_t *s) {
  int r = s->fq_resumed;
  s->fq_resumed = 0;
  return r;
}

static int next_frame_queued(source_t *s, source_frame_t *out, net_err_reason_t *reason_out) {
  if (!s->fq) return pull_frame(s, out, reason_out);
  while (framequeue_ms(s->fq) < 2 * s->prefill_ms && framequeue_count(s->fq) < FQ_MAX_FRAMES) {
    source_frame_t f;
    int r = pull_frame(s, &f, reason_out);
    if (r < 0) return -1;
    if (r == 0) break;
    if (framequeue_push(s->fq, &f) < 0) {
      if (reason_out) *reason_out = NET_ERR_OTHER;
      return -1;
    }
  }
  if (s->insp_slot && *s->insp_slot) tsinspect_set_buffer_ms(*s->insp_slot, framequeue_ms(s->fq));
  if (!s->fq_running) {
    if (framequeue_ms(s->fq) < s->prefill_ms) return 0;
    s->fq_running = 1;
    s->fq_resumed = 1;
  }
  if (!framequeue_pop(s->fq, out)) {
    s->fq_running = 0;
    return 0;
  }
  return 1;
}

int source_next_frame(source_t *s, source_frame_t *out, net_err_reason_t *reason_out) {
  int r = next_frame_queued(s, out, reason_out);
  if (r == 0 && mono_seconds() - s->last_rx > SRC_STALL_TIMEOUT_S) {
    log_line_ansi("input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;31mno data\e[0m for %.0fs", s->idx, s->label ? s->label : "?", SRC_STALL_TIMEOUT_S);
    if (reason_out) *reason_out = NET_ERR_TIMEOUT;
    return -1;
  }
  return r;
}

int source_fd(const source_t *s) { return s->hls ? hls_live_poll_fd(s->hls) : http_fd(s->http); }

int source_has_buffered(const source_t *s) {
  if (s->fq && framequeue_count(s->fq)) return 1;
  return s->hls ? hls_live_has_buffered(s->hls) : http_has_buffered(s->http);
}

void source_close(source_t *s) {
  if (!s) return;
  if (s->fq) framequeue_free(s->fq);
  if (s->latm) aac_latm_free(s->latm);
  if (s->icy) icy_free(s->icy);
  if (s->id3) id3_free(s->id3);
  if (s->hls) hls_live_free(s->hls);
  if (s->hls_demux) rawaudio_demux_free(s->hls_demux);
  free(s->hls_remux);
  if (s->http) http_close(s->http);
  free(s);
}
