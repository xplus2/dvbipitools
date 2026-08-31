/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_WS_BROADCAST_H
#define DIPIXY_WS_BROADCAST_H

#include <stddef.h>
#include <stdint.h>

/* one WS conn's sink, ctx type owned per-protocol. frame: shared across sinks, read-only, copy before returning */
typedef void (*ws_sink_fn)(void *ctx, const uint8_t *frame, size_t frame_len);

/* callable from any thread. registry mutex protected */
void ws_broadcast_register(ws_sink_fn fn, void *ctx);
void ws_broadcast_unregister(ws_sink_fn fn, void *ctx);

/* frames msg once, delivers to every registered sink, synchronously */
void ws_broadcast_publish(const char *msg);

/* 1 if any sink registered, 0 otherwise */
int ws_broadcast_has_sinks(void);

#endif
