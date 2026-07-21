/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_INPUT_SOURCE_H
#define DIPIRADIOHEAD_INPUT_SOURCE_H

#include <stddef.h>

typedef enum { SRC_MPEG_AUDIO, SRC_AAC_ADTS, SRC_AAC_LATM } source_codec_t;

typedef struct {
  source_codec_t codec;
  unsigned stream_type; /* PMT stream_type */
  unsigned sample_rate;
  unsigned samples; /* samples in this frame, for PTS advance */
  const unsigned char *data; /* into source_t's internal buffer, valid until the next source_next_frame call */
  size_t len;
} source_frame_t;

typedef struct source source_t;

typedef void (*source_meta_cb)(void *ctx, const char *artist, const char *title);

/* resolves playlists, connects, detects codec + metadata mode. insecure skips TLS verify. NULL on failure (logged) */
source_t *source_open(const char *uri, int insecure, source_meta_cb cb, void *ctx);

/* 1 + fills *out, 0 transient (retry), -1 hard error (caller should reconnect) */
int source_next_frame(source_t *s, source_frame_t *out);

void source_close(source_t *s);

#endif
