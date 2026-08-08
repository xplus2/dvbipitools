/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_CAS_CAS_H
#define DIPIRADIOHEAD_CAS_CAS_H

#include <stddef.h>
#include <stdint.h>

#include "lib/cas/cas_group.h"
#include "lib/scrambler/scrambler.h"

#include "../args.h"

typedef struct cas cas_t;

/* audio_pids: every program's audio pid, one shared CW across all vendors. audio_pids[0] is the
   flush pid (cas_group.h). only pid never CSA2-batch-delayed. NULL if n_audio_pids 0 or over
   CAS_CORE_MAX_PIDS (logged). */
cas_t *cas_start(const config_t *cfg, const unsigned *audio_pids, size_t n_audio_pids);
void cas_stop(cas_t *c);

/* 1 once grace period passed with no frame seen: fatal, caller must stop */
int cas_failed(cas_t *c);

/* call once per audio frame, before tspacketizer_feed(). pts_90k: radiohead's own sample clock.
   first call lazy-starts ECMG/EMMG. */
void cas_clock_tick(cas_t *c, uint64_t pts_90k);

/* no-op unless out_pid is the audio pid or ECMG hasn't started. emits exactly once. */
void cas_scramble_packet(cas_t *c, unsigned out_pid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx);

/* flushes any batched packet. call at end of stream. */
void cas_flush(cas_t *c, scrambler_emit_cb emit, void *ctx);

/* shared scramble engine counters, one value across all vendors */
void cas_get_metrics(cas_t *c, cas_metrics_t *out);
/* per-vendor counters (ecmg/emmg), see cas_group_vendor_metrics() */
void cas_vendor_metrics(cas_t *c, size_t idx, cas_metrics_t *out);
unsigned cas_vendor_super_cas_id(cas_t *c, size_t idx);

/* N CA_descriptor(ecm)s (one per vendor) + one scrambling_descriptor for PMT program_info. 0 on overflow. */
size_t cas_prog_desc(cas_t *c, unsigned char *out, size_t cap);
/* one CAT section, N CA_descriptor(emm)s (one per vendor). 0 on overflow. */
size_t cas_build_cat(cas_t *c, unsigned char *out, size_t cap);

size_t cas_vendor_count(cas_t *c);
unsigned cas_vendor_ecm_pid(cas_t *c, size_t idx);
unsigned cas_vendor_emm_pid(cas_t *c, size_t idx);
/* 0 = filled out/out_len with the current ECM (due for resend), -1 = not due / none yet / silent+disconnected */
int cas_vendor_ecm_due(cas_t *c, size_t idx, double now, unsigned char *out, size_t cap, size_t *out_len);
/* 0 = filled, -1 = queue empty */
int cas_vendor_next_emm(cas_t *c, size_t idx, unsigned char *out, size_t cap, size_t *out_len);

/* exact 90kHz -> ms via remainder carry (*rem_inout, starts at 0): no drift over time.
   pure, exposed for unit tests only. */
unsigned long cas_90k_to_ms(uint64_t delta_90k, uint64_t *rem_inout);

#endif
