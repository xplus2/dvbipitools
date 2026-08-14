/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_INPUT_SOURCE_H
#define DIPIRADIOHEAD_INPUT_SOURCE_H

#include <stddef.h>

#include "lib/net/netconnect.h"

typedef enum { SRC_MPEG_AUDIO, SRC_AAC_ADTS, SRC_AAC_LATM } source_codec_t;

typedef struct {
  source_codec_t codec;
  unsigned stream_type; /* PMT stream_type */
  unsigned sample_rate;
  unsigned samples; /* samples in this frame, for PTS advance */
  const unsigned char *data; /* into source_t's internal buffer, valid until next source_next_frame call */
  size_t len;
} source_frame_t;

typedef struct source source_t;

typedef void (*source_meta_cb)(void *ctx, const char *artist, const char *title);

/* resolves playlists, connects, detects codec + metadata mode. insecure skips TLS verify.
   NULL on failure. reason_out: nullable, set only on NULL return */
source_t *source_open(const char *uri, int insecure, source_meta_cb cb, void *ctx, net_err_reason_t *reason_out);

/* 1 + fills *out, 0 transient (retry), -1 hard error (caller should reconnect).
   reason_out: nullable, set only on -1 */
int source_next_frame(source_t *s, source_frame_t *out, net_err_reason_t *reason_out);

/* underlying socket fd, for caller's own poll(); valid for life of s */
int source_fd(const source_t *s);

/* cumulative bytes read off wire for this source_t's lifetime. resets on reconnect:
   caller folds into its own persistent total before discarding s */
unsigned long long source_bytes_total(const source_t *s);

void source_close(source_t *s);

typedef enum { SOURCE_OPEN_PENDING, SOURCE_OPEN_DONE, SOURCE_OPEN_ERROR } source_open_state_t;
typedef struct source_open source_open_t;

/* async source_open(): never blocks, caller polls. same semantics (playlist redirects, codec/metadata sniff).
   no internal timeout, caller decides when to give up. NULL only on immediate setup failure (calloc logged).
   reason_out: nullable, set only on NULL return */
source_open_t *source_open_async_start(const char *uri, int insecure, source_meta_cb cb, void *ctx, net_err_reason_t *reason_out);

int source_open_async_poll_fd(const source_open_t *o);
short source_open_async_poll_events(const source_open_t *o);
/* reason_out: nullable, set only on SOURCE_OPEN_ERROR */
source_open_state_t source_open_async_step(source_open_t *o, net_err_reason_t *reason_out);

/* DONE only: hands over source_t, frees async handle */
source_t *source_open_async_take(source_open_t *o);

/* frees handle + owned state; safe at any state incl. PENDING */
void source_open_async_free(source_open_t *o);

#endif
