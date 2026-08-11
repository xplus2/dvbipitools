/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_RET_H
#define DIPIFCCRET_RET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include "lib/demux/rtcp.h"

#include "../channel/channel.h"

/* F.9 IP_TOS byte values (DSCP << 2); fixed video-bearer mark, not a mirror of the original packet's own DSCP */
#define RET_DSCP_RTX  (0x22 << 2) /* 0b100010 */
#define RET_DSCP_RTCP (0x1A << 2) /* 0b011010 */

/* sends one built packet on c's MC RET session (F.6.2.2); caller owns the socket, sets IP_TOS to dscp */
typedef void (*ret_send_fn)(const channel_t *c, const unsigned char *pkt, size_t len, int dscp, void *user);

/* sends one built packet back to the requester via fd (F.3.1/Figure F.2 mandatory unicast reply) */
typedef void (*ret_send_unicast_fn)(int fd, const struct sockaddr *to, socklen_t tolen, const unsigned char *pkt, size_t len, int dscp, void *user);

typedef struct ret_ctx ret_ctx_t;

ret_ctx_t *ret_ctx_new(channel_table_t *channels, unsigned char rtx_pt, ret_send_fn send_mc, ret_send_unicast_fn send_unicast, void *user);
void ret_ctx_free(ret_ctx_t *r);

/* one parsed Generic NACK; unicast-replies to `from` (F.2 baseline) plus repairs via send_mc when MC is on */
void ret_handle_nack(ret_ctx_t *r, const rtcp_nack_t *nack, int fd, const struct sockaddr *from, socklen_t fromlen);

/* self-detected gap [gap_start, gap_end] on ssrc's channel; sends an FF then repairs via send_mc, no unicast reply */
void ret_on_self_detected_gap(ret_ctx_t *r, uint32_t ssrc, uint16_t gap_start, uint16_t gap_end);

#endif
