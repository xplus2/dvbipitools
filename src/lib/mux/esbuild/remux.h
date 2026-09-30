/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_ESBUILD_REMUX_H
#define DVBIPITOOLS_LIB_MUX_ESBUILD_REMUX_H

#include <stddef.h>
#include <stdint.h>

#include "esbuild.h"
#include "pmtbuild.h"
#include "lib/mux/tspacket_write.h"

typedef struct {
  esbuild_track_t track;
  esbuild_es_t es;
  unsigned fmp4_track_id;
  unsigned stream_idx;
  unsigned timescale;
  uint64_t next_dts;
  unsigned char cc;
} esbuild_remux_track_t;

typedef struct {
  esbuild_remux_track_t tracks[ESBUILD_MAX_ES];
  unsigned n_tracks;
  unsigned char pat_cc;
  unsigned char pmt_cc;
  unsigned pmt_version;
  int have_init;
  unsigned char pesbuf[65536];
  unsigned char esbuf[65536];
} esbuild_remux_t;

int esbuild_remux_init(esbuild_remux_t *r, const unsigned char *init_data, size_t init_len);

int esbuild_remux_add_init(esbuild_remux_t *r, unsigned stream_idx, const unsigned char *init_data, size_t init_len);

void esbuild_remux_feed(esbuild_remux_t *r, unsigned stream_idx, const unsigned char *seg_data, size_t seg_len, ts_packet_cb cb, void *cb_ctx);

#endif
