/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_MUX_TSPACKETIZER_H
#define DIPIRADIOHEAD_MUX_TSPACKETIZER_H

#include <stddef.h>
#include <stdint.h>

#include "lib/mux/tspacket_write.h"

typedef struct {
  unsigned tsid, onid, sid;
  unsigned stream_type; /* PMT stream_type of the detected codec */
  const char *network_name; /* "" = no NIT network_name descriptor; pointer must outlive the packetizer */
  const char *service_name; /* pointer must outlive the packetizer */
} tspacketizer_cfg_t;

typedef struct tspacketizer tspacketizer_t;

tspacketizer_t *tspacketizer_new(const tspacketizer_cfg_t *cfg);
void tspacketizer_free(tspacketizer_t *t);

/* bumps the EIT version and forces an immediate re-send on the next feed */
void tspacketizer_set_metadata(tspacketizer_t *t, const char *artist, const char *title);

/* packetizes one audio frame as PES (with PCR), plus any PSI due by schedule/change. returns packet count */
size_t tspacketizer_feed(tspacketizer_t *t, uint64_t pts_90k, const unsigned char *frame, size_t frame_len, ts_packet_cb cb, void *ctx);

#endif
