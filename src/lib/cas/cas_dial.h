/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_CAS_DIAL_H
#define DVBIPITOOLS_LIB_CAS_CAS_DIAL_H

#include <stdatomic.h>

int cas_dial_stopping(const _Atomic int *stop);

int cas_tcp_dial(const _Atomic int *stop, const char *host, unsigned port, unsigned poll_interval_ms, unsigned timeout_ms, const char *log_tag);

void cas_interruptible_backoff(const _Atomic int *stop, unsigned ms, unsigned poll_interval_ms);

#endif
