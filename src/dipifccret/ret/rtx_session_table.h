/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_RTX_SESSION_TABLE_H
#define DIPIFCCRET_RTX_SESSION_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>

/* F.3.2.1: one independent RTX seq space per unicast RET client, keyed by
   client transport address only, not per channel too. matches "N unicast RET
   RTP sessions, one per HNED". client switching channel keeps its counter;
   RTX packet's own ssrc (new channel's) already marks stream boundary. */
typedef struct rtx_session_table rtx_session_table_t;

rtx_session_table_t *rtx_session_table_new(size_t cap);
void rtx_session_table_free(rtx_session_table_t *t);

/* finds or claims session for this client address, returns its next seq and
   advances it, all under the stripe lock: no session pointer escapes, so slot
   reuse cannot race the caller. full address-sharded stripe evicts its own
   coldest session. addr NULL or addrlen 0 (unit-test convenience, never happens
   on real recvfrom() path) maps to one shared session. */
uint16_t rtx_session_table_next_seq(rtx_session_table_t *t, const struct sockaddr *addr, socklen_t addrlen);

/* amortized reap, at most max_scan slots per call: same pattern as channel_table_reap_step */
void rtx_session_table_reap_step(rtx_session_table_t *t, time_t max_age_s, size_t max_scan);

/* valid sessions across every stripe */
size_t rtx_session_table_active_count(rtx_session_table_t *t);

#endif
