/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_MUX_TSPACKETIZER_H
#define DIPIRADIOHEAD_MUX_TSPACKETIZER_H

#include <stddef.h>
#include <stdint.h>

#include "lib/mux/psi_build.h"
#include "lib/mux/tspacket_write.h"

#include "../cas/cas.h"

#define TSPACKETIZER_PID_AUDIO 0x0101 /* default single-program-out audio ES pid */

typedef struct {
  unsigned tsid, onid, sid;
  unsigned stream_type; /* PMT stream_type of detected codec */
  const char *network_name; /* "" = no NIT network_name descriptor; pointer must outlive packetizer */
  const char *service_name; /* pointer must outlive packetizer */
  unsigned pmt_pid;   /* 0 = default 0x0100 */
  unsigned audio_pid; /* 0 = default TSPACKETIZER_PID_AUDIO (0x0101) */
  /* 1: self-contained SPTS, emits PAT/CAT/NIT/SDT/EIT/ECM/EMM too (today's behavior).
     0: MPTS program, only PMT+audio PES; mux pulls SDT/EIT via build_sdt()/build_eit(). */
  int standalone;
} tspacketizer_cfg_t;

typedef struct tspacketizer tspacketizer_t;

tspacketizer_t *tspacketizer_new(const tspacketizer_cfg_t *cfg);
void tspacketizer_free(tspacketizer_t *t);

/* bumps EIT version, forces resend on next feed (standalone) or build_eit() (non-standalone) */
void tspacketizer_set_metadata(tspacketizer_t *t, const char *artist, const char *title);

/* enables CAS: CAT sending, ECM/EMM injection (each vendor's own ecm_pid/emm_pid, queried off cas),
   scramble audio ES. CAS must outlive packetizer. ECM/EMM only sent in standalone mode. */
void tspacketizer_set_cas(tspacketizer_t *t, cas_t *cas);

/* packetizes one audio frame as PES, plus PSI due by schedule (standalone: all tables; always:
   PMT, audio PES). now: caller's mono_seconds(), for CAS ECM-repeat/parity-flip timing.
   ret: pkg count */
size_t tspacketizer_feed(tspacketizer_t *t, uint64_t pts_90k, double now, const unsigned char *frame, size_t frame_len, ts_packet_cb cb, void *ctx);

/* non-standalone only: fills *out with this program's current SDT service info, for a mux to
   fold into its composite SDT section. 0 on success. */
int tspacketizer_get_sdt_info(tspacketizer_t *t, psi_sdt_entry_t *out);
/* non-standalone only: pull this program's current EIT section, for a mux to packetize on its
   own shared pid+cc. 0 on overflow. */
size_t tspacketizer_build_eit(tspacketizer_t *t, unsigned char *out, size_t cap);

/* 1 if metadata changed since last build_eit() - mux should refresh now, not wait for its
   schedule. cleared by build_eit(). */
int tspacketizer_eit_pending(const tspacketizer_t *t);

#endif
