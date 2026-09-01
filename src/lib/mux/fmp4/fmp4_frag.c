/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "fmp4_int.h"

#include <string.h>

static uint32_t sample_flags(int keyframe) {
  /* ISOBMFF sample_flags bitfield. key: depends_on=2 (no deps). other: depends_on=1, non_sync=1 */
  return keyframe ? 0x02000000u : 0x01010000u;
}

size_t fmp4_segment_end(fmp4_mux_t *m, unsigned char **out) {
  mp4buf_t styp, moof;
  size_t patch_pos[FMP4_MAX_TRACKS];
  size_t track_mdat_off[FMP4_MAX_TRACKS];
  size_t out_moof_start, mdat_payload_start, running;
  int i;

  mp4buf_free(&m->out);
  memset(&styp, 0, sizeof styp);
  mb_fourcc(&styp, "msdh");
  mb_u32(&styp, 0);
  mb_fourcc(&styp, "msdh");
  mb_fourcc(&styp, "msix");
  mb_box(&m->out, "styp", &styp);
  out_moof_start = m->out.len;
  memset(&moof, 0, sizeof moof);
  {
    mp4buf_t mfhd;
    memset(&mfhd, 0, sizeof mfhd);
    mb_u8(&mfhd, 0);
    mb_u24(&mfhd, 0);
    mb_u32(&mfhd, m->seq);
    mb_box(&moof, "mfhd", &mfhd);
  }

  for (i = 0; i < m->ntrk; i++) {
    const frag_track_t *f = &m->frag[i];
    mp4buf_t traf, tfhd, tfdt, trun;
    size_t traf_trun_start, moof_traf_start;

    memset(&tfhd, 0, sizeof tfhd);
    mb_u8(&tfhd, 0);
    mb_u24(&tfhd, 0x020000); /* default-base-is-moof */
    mb_u32(&tfhd, m->trk[i].cfg.track_id);

    memset(&tfdt, 0, sizeof tfdt);
    mb_u8(&tfdt, 1); /* version 1: 64-bit baseMediaDecodeTime */
    mb_u24(&tfdt, 0);
    mb_u64(&tfdt, m->trk[i].frag_base_dts);

    memset(&trun, 0, sizeof trun);
    mb_u8(&trun, 1);
    mb_u24(&trun, 0x000F01); /* data-offset, duration, size, flags, cts */
    mb_u32(&trun, (uint32_t)f->nsamples);
    patch_pos[i] = trun.len; /* rebased below at each nesting level */
    mb_u32(&trun, 0);
    for (int j = 0; j < f->nsamples; j++) {
      const frag_sample_t *fs = &f->samples[j];
      mb_u32(&trun, fs->duration);
      mb_u32(&trun, fs->size);
      mb_u32(&trun, sample_flags(fs->keyframe));
      mb_u32(&trun, (uint32_t)fs->cts_offset);
    }

    memset(&traf, 0, sizeof traf);
    mb_box(&traf, "tfhd", &tfhd);
    mb_box(&traf, "tfdt", &tfdt);
    traf_trun_start = traf.len;
    mb_box(&traf, "trun", &trun);
    patch_pos[i] += traf_trun_start + 8;
    moof_traf_start = moof.len;
    mb_box(&moof, "traf", &traf);
    patch_pos[i] += moof_traf_start + 8;
  }
  mb_box(&m->out, "moof", &moof);
  for (i = 0; i < m->ntrk; i++)
    patch_pos[i] += out_moof_start + 8;

  running = 0;
  for (i = 0; i < m->ntrk; i++) {
    track_mdat_off[i] = running;
    running += m->frag[i].data_len;
  }
  mdat_payload_start = m->out.len + 8; /* mdat header not yet appended */
  for (i = 0; i < m->ntrk; i++) {
    uint32_t data_offset = (uint32_t)((mdat_payload_start + track_mdat_off[i]) - out_moof_start);
    mb_patch_u32(&m->out, patch_pos[i], data_offset);
  }

  mb_u32(&m->out, (uint32_t)(8 + running));
  mb_fourcc(&m->out, "mdat");
  for (i = 0; i < m->ntrk; i++)
    mb_bytes(&m->out, m->frag[i].data, m->frag[i].data_len);

  *out = m->out.p;
  return m->out.err ? 0 : m->out.len;
}
