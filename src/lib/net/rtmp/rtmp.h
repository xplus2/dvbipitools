/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RTMP_H
#define DVBIPITOOLS_LIB_NET_RTMP_H

#include <stddef.h>
#include <stdint.h>

/* publish-only: no play/pause/seek. transport-agnostic via write_cb/rtmp_feed. */

typedef struct rtmp rtmp_t;

typedef void (*rtmp_write_cb)(void *ctx, const unsigned char *data, size_t len);
typedef void (*rtmp_ready_cb)(void *ctx); /* rtmp_send_* valid from here on */
typedef void (*rtmp_error_cb)(void *ctx, const char *msg);

typedef struct {
  const char *app;
  const char *tcurl; /* rtmp(s)://host[:port]/app, informational only */
  const char *stream_name;
  rtmp_write_cb write_cb;
  rtmp_ready_cb ready_cb;
  rtmp_error_cb error_cb;
  void *cb_ctx;
} rtmp_cfg_t;

rtmp_t *rtmp_new(const rtmp_cfg_t *cfg);
void rtmp_free(rtmp_t *r);

void rtmp_start(rtmp_t *r); /* emits C0+C1 */

/* 0 ok, -1 fatal (caller reconnects) */
int rtmp_feed(rtmp_t *r, const unsigned char *data, size_t len);

/* only after ready_cb */
int rtmp_send_video(rtmp_t *r, uint32_t timestamp_ms, const unsigned char *data, size_t len);
int rtmp_send_audio(rtmp_t *r, uint32_t timestamp_ms, const unsigned char *data, size_t len);
int rtmp_send_data(rtmp_t *r, const unsigned char *data, size_t len); /* onMetaData, timestamp always 0 */

#endif
