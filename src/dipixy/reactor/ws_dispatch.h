/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_REACTOR_WS_DISPATCH_H
#define DIPIXY_REACTOR_WS_DISPATCH_H

#include <stddef.h>
#include <stdint.h>

typedef void (*ws_dispatch_queue_fn)(void *ctx, int opcode, const void *payload, size_t len);

void ws_dispatch_frame(void *ctx, ws_dispatch_queue_fn queue, int opcode, const uint8_t *payload, size_t plen);

#endif
