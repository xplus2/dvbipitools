/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_MP4PUSH_H
#define DIPIXY_MP4PUSH_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "../ts/capture/capture.h"
#include "../ts/pidfilter.h"
#include "../reactor/conn.h"

void mp4push_init(int max_clients);

int mp4push_subscribe(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, int proto);
void mp4push_sub_close(int slot);

int mp4push_try_attach(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, int ws_handle);

void mp4push_h2_bind(int slot, int fd, int reactor_tid, int ws_handle);
int mp4push_sub_fd(int slot);

void mp4push_h3_bind(int slot, void *h3c, int64_t h3_sid, int reactor_tid, int ws_handle);
void *mp4push_sub_h3c(int slot);
int64_t mp4push_sub_h3_sid(int slot);

size_t mp4push_ring_read(int slot, uint8_t *buf, size_t maxlen);
const uint8_t *mp4push_ring_peek(int slot, size_t *len);
void mp4push_ring_advance(int slot, size_t n);
int mp4push_ring_pending(int slot);
int mp4push_ring_errored(int slot);

void mp4push_register_reactor_efd(int tid, int efd);
void mp4push_flush_ready(int tid);

void h2_mp4push_wake(int sub_idx);
void h3_mp4push_wake(int sub_idx);

#endif
