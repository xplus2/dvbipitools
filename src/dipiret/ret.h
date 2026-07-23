/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRET_RET_H
#define DIPIRET_RET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include "channel.h"

/* F.9 IP_TOS byte values (DSCP << 2); RTX uses the video-bearer mark, RTCP (FF/RSI) the signalling mark.
   RET_DSCP_RTX is a fixed video-bearer-class value, not a mirror of each original packet's own
   DSCP byte - that would need capturing and threading through one more field, not done here. */
#define RET_DSCP_RTX  (0x22 << 2) /* 0b100010 */
#define RET_DSCP_RTCP (0x1A << 2) /* 0b011010 */

/* sends one already-built packet on channel c's MC RET session (same group as c, different source, per F.6.2.2);
   caller owns the socket, must set IP_TOS to dscp before sending */
typedef void (*ret_send_fn)(const channel_t *c, const unsigned char *pkt, size_t len, int dscp, void *user);

/* sends one already-built packet directly back to a client (F.3.1/Figure F.2's mandatory unicast reply), out via fd
   (the same socket the request arrived on); caller must set IP_TOS to dscp before sending */
typedef void (*ret_send_unicast_fn)(int fd, const struct sockaddr *to, socklen_t tolen, const unsigned char *pkt, size_t len, int dscp, void *user);

typedef struct ret_ctx ret_ctx_t;

ret_ctx_t *ret_ctx_new(channel_table_t *channels, unsigned char rtx_pt, ret_send_fn send_mc, ret_send_unicast_fn send_unicast, void *user);
void ret_ctx_free(ret_ctx_t *r);

/* client RTCP arrived; always unicast-replies to `from` (F.2 baseline) plus repairs via send_mc when MC is on - both, not either/or */
void ret_on_rtcp(ret_ctx_t *r, const unsigned char *rtcp_pkt, size_t len, int fd, const struct sockaddr *from, socklen_t fromlen);

/* capture self-detected a gap [gap_start, gap_end] on the channel with this ssrc; sends an FF then any buffered matches via send_mc
   - no unicast reply, there is no requesting client for a self-detected gap */
void ret_on_self_detected_gap(ret_ctx_t *r, uint32_t ssrc, uint16_t gap_start, uint16_t gap_end);

#endif
