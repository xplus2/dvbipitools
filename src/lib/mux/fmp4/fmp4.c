/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "fmp4_int.h"

#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"

static int frag_reserve_samples(frag_track_t *f, int need) {
  size_t cap = (size_t)f->cap;
  int rc = growbuf_reserve((void **)&f->samples, &cap, sizeof *f->samples, (size_t)need, 16);
  f->cap = (int)cap;
  return rc;
}

static int frag_reserve_data(frag_track_t *f, size_t need) {
  return growbuf_reserve((void **)&f->data, &f->data_cap, 1, need, 65536);
}

fmp4_mux_t *fmp4_mux_new(const fmp4_track_cfg_t *tracks, int ntracks) {
  fmp4_mux_t *m;
  if (ntracks < 1 || ntracks > FMP4_MAX_TRACKS)
    return NULL;
  m = calloc(1, sizeof *m);
  if (!m)
    return NULL;
  m->ntrk = ntracks;
  for (int i = 0; i < ntracks; i++) {
    size_t cl = tracks[i].cpriv_len;
    if (cl > FMP4_CPRIV_MAX)
      cl = FMP4_CPRIV_MAX;
    m->trk[i].cfg = tracks[i];
    memcpy(m->trk[i].cpriv, tracks[i].cpriv, cl);
    m->trk[i].cfg.cpriv = m->trk[i].cpriv;
    m->trk[i].cfg.cpriv_len = cl;
  }
  return m;
}

void fmp4_mux_free(fmp4_mux_t *m) {
  if (!m)
    return;
  mp4buf_free(&m->out);
  for (int i = 0; i < m->ntrk; i++) {
    free(m->frag[i].samples);
    free(m->frag[i].data);
  }
  free(m);
}

size_t fmp4_init_segment(fmp4_mux_t *m, unsigned char **out) {
  mp4buf_t ftyp, moov;

  mp4buf_free(&m->out);

  memset(&ftyp, 0, sizeof ftyp);
  mb_fourcc(&ftyp, "iso5"); /* major_brand */
  mb_u32(&ftyp, 0);        /* minor_version */
  mb_fourcc(&ftyp, "iso5");
  mb_fourcc(&ftyp, "iso6");
  mb_fourcc(&ftyp, "mp41");
  mb_box(&m->out, "ftyp", &ftyp);

  memset(&moov, 0, sizeof moov);
  build_mvhd(&moov, m->ntrk);
  for (int i = 0; i < m->ntrk; i++)
    build_trak(&moov, &m->trk[i]);
  build_mvex(&moov, m);
  mb_box(&m->out, "moov", &moov);

  *out = m->out.p;
  return m->out.err ? 0 : m->out.len;
}

void fmp4_segment_begin(fmp4_mux_t *m, uint32_t sequence_number) {
  m->seq = sequence_number;
  for (int i = 0; i < m->ntrk; i++) {
    m->frag[i].nsamples = 0;
    m->frag[i].data_len = 0;
    m->trk[i].frag_base_dts = m->trk[i].next_dts;
  }
}

void fmp4_segment_add_sample(fmp4_mux_t *m, const fmp4_sample_t *s) {
  frag_track_t *f;
  frag_sample_t *fs;
  if (s->track_idx < 0 || s->track_idx >= m->ntrk)
    return;
  f = &m->frag[s->track_idx];
  if (frag_reserve_samples(f, f->nsamples + 1) < 0)
    return;
  if (frag_reserve_data(f, f->data_len + s->size) < 0)
    return;
  fs = &f->samples[f->nsamples++];
  fs->duration = s->duration;
  fs->size = (uint32_t)s->size;
  fs->cts_offset = s->cts_offset;
  fs->keyframe = s->keyframe;
  memcpy(f->data + f->data_len, s->data, s->size);
  f->data_len += s->size;
  m->trk[s->track_idx].next_dts += s->duration;
}

void fmp4_track_seed_dts(fmp4_mux_t *m, int track_idx, uint64_t dts) {
  if (track_idx < 0 || track_idx >= m->ntrk)
    return;
  m->trk[track_idx].next_dts = dts;
  /* first sample only: frag_base_dts stale from segment_begin, patch too */
  if (m->frag[track_idx].nsamples == 0)
    m->trk[track_idx].frag_base_dts = dts;
}
