/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_SOURCE_H
#define DIPITVHEAD_SOURCE_H

#include <stddef.h>
#include <sys/types.h>

#include "lib/net/netconnect.h"
#include "lib/net/ts/source.h"

#include "../cli/args.h"

typedef struct tvsrc tvsrc_t;

/* opens per input->input (rtp/udp multicast, http(s) TS/HLS/DASH, or stdin). HTTP open bounded: 30s.
   cfg->insecure_tls applies to all inputs. NULL on failure. reason_out: nullable, set only on NULL return */
tvsrc_t *tvsrc_open(const config_t *cfg, const dipitvhead_input_t *input, net_err_reason_t *reason_out);

/* TS bytes, RTP unwrapped if present. >0 len, 0 transient (retry), -1 hard error/EOF.
   reason_out: nullable, set only on -1 */
ssize_t tvsrc_read(tvsrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out);

/* waits for readable data, up to timeout_ms. 1 readable, 0 timeout/EINTR, -1 error.
   handles buffered and multi-fd http sources */
int tvsrc_wait(const tvsrc_t *s, int timeout_ms);

/* underlying fd, for caller's own poll(); valid for life of s */
int tvsrc_fd(const tvsrc_t *s);
short tvsrc_poll_events(const tvsrc_t *s);

/* 1: fd poll alone never wakes for this src (HLS/DASH timers). read every tick, poll at most TVSRC_TIMER_WAIT_MS */
int tvsrc_timer_driven(const tvsrc_t *s);
#define TVSRC_TIMER_WAIT_MS TSSRC_TIMER_WAIT_MS
int tvsrc_enable_rx_timestamps(tvsrc_t *s);
uint64_t tvsrc_last_rx_ns(const tvsrc_t *s);
int64_t tvsrc_buffer_ms(const tvsrc_t *s);

void tvsrc_close(tvsrc_t *s);

typedef enum { TVSRC_OPEN_PENDING, TVSRC_OPEN_DONE, TVSRC_OPEN_ERROR } tvsrc_open_state_t;
typedef struct tvsrc_open tvsrc_open_t;

/* async tvsrc_open(): never blocks. no internal timeout, caller decides when to give up.
   NULL only on immediate setup failure (calloc - logged). */
tvsrc_open_t *tvsrc_open_async_start(const config_t *cfg, const dipitvhead_input_t *input, net_err_reason_t *reason_out);

int tvsrc_open_async_poll_fd(const tvsrc_open_t *o);
short tvsrc_open_async_poll_events(const tvsrc_open_t *o);
/* reason_out: nullable, set only on TVSRC_OPEN_ERROR */
tvsrc_open_state_t tvsrc_open_async_step(tvsrc_open_t *o, net_err_reason_t *reason_out);

/* DONE only: hands over tvsrc_t, frees async handle */
tvsrc_t *tvsrc_open_async_take(tvsrc_open_t *o);

/* frees handle + owned state; safe at any state incl. PENDING */
void tvsrc_open_async_free(tvsrc_open_t *o);

#endif
