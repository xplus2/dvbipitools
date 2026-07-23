/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "lib/demux/rtcp.h"
#include "lib/mux/rtcp_build.h"
#include "lib/mux/rtx.h"

#include "ret.h"

struct ret_ctx {
  channel_table_t *channels;
  unsigned char rtx_pt;
  ret_send_fn send_mc;
  void *send_mc_user;
  ret_send_unicast_fn send_unicast;
  void *send_unicast_user;
  rtx_ctx_t *rtx; /* shared by every send, not true per-session per F.3.2.1 - no per-client tracking exists yet, flagged in the plan */
};

ret_ctx_t *ret_ctx_new(channel_table_t *channels, unsigned char rtx_pt, ret_send_fn send_mc, ret_send_unicast_fn send_unicast, void *user) {
  ret_ctx_t *r = calloc(1, sizeof *r);
  if (!r)
    return NULL;
  r->channels = channels;
  r->rtx_pt = rtx_pt;
  r->send_mc = send_mc;
  r->send_mc_user = user;
  r->send_unicast = send_unicast;
  r->send_unicast_user = user;
  r->rtx = rtx_ctx_new();
  if (!r->rtx) {
    free(r);
    return NULL;
  }
  return r;
}

void ret_ctx_free(ret_ctx_t *r) {
  if (!r)
    return;
  rtx_ctx_free(r->rtx);
  free(r);
}

static void repair_one(ret_ctx_t *r, channel_t *c, uint16_t seq) {
  channel_slot_t slot;
  unsigned char out[12 + 2 + CHANNEL_MAX_PAYLOAD];
  size_t n;
  if (!channel_find(c, seq, &slot))
    return;
  n = rtx_build(r->rtx, c->ssrc, r->rtx_pt, slot.timestamp, slot.seq, slot.payload, slot.payload_len, out, sizeof out);
  if (n > 0)
    r->send_mc(c, out, n, RET_DSCP_RTX, r->send_mc_user);
}

static void repair_one_unicast(ret_ctx_t *r, channel_t *c, uint16_t seq, int fd, const struct sockaddr *from, socklen_t fromlen) {
  channel_slot_t slot;
  unsigned char out[12 + 2 + CHANNEL_MAX_PAYLOAD];
  size_t n;
  if (!channel_find(c, seq, &slot))
    return;
  n = rtx_build(r->rtx, c->ssrc, r->rtx_pt, slot.timestamp, slot.seq, slot.payload, slot.payload_len, out, sizeof out);
  if (n > 0)
    r->send_unicast(fd, from, fromlen, out, n, RET_DSCP_RTX, r->send_unicast_user);
}

static void repair_range(ret_ctx_t *r, channel_t *c, uint16_t start, uint16_t end) {
  uint16_t seq = start;
  for (;;) {
    repair_one(r, c, seq);
    if (seq == end)
      break;
    seq++;
  }
}

typedef struct {
  ret_ctx_t *r;
  int fd;
  const struct sockaddr *from;
  socklen_t fromlen;
} nack_ctx_t;

static void nack_cb(const rtcp_nack_t *nack, void *user) {
  nack_ctx_t *nc = (nack_ctx_t *)user;
  ret_ctx_t *r = nc->r;
  channel_t *c = channel_find_by_ssrc(r->channels, nack->media_ssrc);
  unsigned char ff[12 + 4 * RTCP_NACK_MAX_ENTRIES];
  size_t ff_len, i;
  if (!c)
    return;

  /* F.5.2 multicast repair/suppression - additional, not instead of the unicast reply below */
  ff_len = rtcp_build_ff(nack->sender_ssrc, nack->media_ssrc, nack->entry, nack->entry_count, ff, sizeof ff);
  if (ff_len > 0)
    r->send_mc(c, ff, ff_len, RET_DSCP_RTCP, r->send_mc_user);

  for (i = 0; i < nack->entry_count; i++) {
    uint16_t pid = nack->entry[i].pid;
    uint16_t blp = nack->entry[i].blp;
    unsigned bit;

    /* F.3.1/Figure F.2 mandatory baseline: always reply directly to the requester */
    repair_one_unicast(r, c, pid, nc->fd, nc->from, nc->fromlen);
    repair_one(r, c, pid);
    for (bit = 0; bit < 16; bit++) {
      if (blp & (1u << bit)) {
        uint16_t seq = (uint16_t)(pid + bit + 1);
        repair_one_unicast(r, c, seq, nc->fd, nc->from, nc->fromlen);
        repair_one(r, c, seq);
      }
    }
  }
}

void ret_on_rtcp(ret_ctx_t *r, const unsigned char *rtcp_pkt, size_t len, int fd, const struct sockaddr *from, socklen_t fromlen) {
  nack_ctx_t nc;
  nc.r = r;
  nc.fd = fd;
  nc.from = from;
  nc.fromlen = fromlen;
  rtcp_parse(rtcp_pkt, len, nack_cb, &nc);
}

void ret_on_self_detected_gap(ret_ctx_t *r, uint32_t ssrc, uint16_t gap_start, uint16_t gap_end) {
  channel_t *c = channel_find_by_ssrc(r->channels, ssrc);
  rtcp_nack_entry_t entry;
  unsigned char ff[16];
  size_t ff_len;
  if (!c)
    return;

  entry.pid = gap_start;
  entry.blp = 0; /* signals only the gap start; the actual repair below covers the full range regardless */
  ff_len = rtcp_build_ff(0, ssrc, &entry, 1, ff, sizeof ff);
  if (ff_len > 0)
    r->send_mc(c, ff, ff_len, RET_DSCP_RTCP, r->send_mc_user);

  repair_range(r, c, gap_start, gap_end);
}
