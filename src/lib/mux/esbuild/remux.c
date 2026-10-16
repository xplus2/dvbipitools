/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "remux.h"

#include <stdlib.h>
#include <string.h>

#include "lib/demux/fmp4/box.h"
#include "lib/demux/fmp4/sample.h"
#include "lib/demux/fmp4/track.h"
#include "lib/helper/log.h"
#include "lib/mux/tspacket_write.h"
#include "pes.h"
#include "tspacketize.h"

static unsigned char stream_id_for(codec_t c) {
  if (c == CODEC_H264 || c == CODEC_HEVC || c == CODEC_VVC || c == CODEC_AV1 || c == CODEC_LCEVC || c == CODEC_MPEG2V) return 0xE0;
  return 0xC0;
}

static uint64_t to_90k(uint64_t ticks, unsigned timescale) {
  return (ticks / timescale) * 90000ULL + (ticks % timescale) * 90000ULL / timescale;
}

typedef struct {
  esbuild_track_t track;
  unsigned track_id;
  unsigned stream_idx;
  unsigned timescale;
} remux_cand_t;

int esbuild_remux_add_init(esbuild_remux_t *r, unsigned stream_idx, const unsigned char *init_data, size_t init_len) {
  fmp4_box_t moov;
  remux_cand_t cand[ESBUILD_MAX_ES];
  unsigned n = r->n_tracks;
  unsigned new_n;
  const unsigned char *p;
  const unsigned char *moov_end;
  unsigned i;
  codec_t codecs[ESBUILD_MAX_ES] = {0};
  esbuild_es_t assigned[ESBUILD_MAX_ES];

  for (i = 0; i < r->n_tracks; i++) {
    cand[i].track = r->tracks[i].track;
    cand[i].track_id = r->tracks[i].fmp4_track_id;
    cand[i].stream_idx = r->tracks[i].stream_idx;
    cand[i].timescale = r->tracks[i].timescale;
  }

  if (!fmp4_box_find(init_data, init_len, "moov", &moov)) return 0;

  p = moov.body;
  moov_end = moov.body + moov.body_len;
  while ((size_t)(moov_end - p) >= 8 && n < ESBUILD_MAX_ES) {
    fmp4_box_t trak;
    fmp4_box_t tkhd;
    fmp4_box_t mdia;
    fmp4_box_t mdhd;
    fmp4_box_t minf;
    fmp4_box_t stbl;
    fmp4_box_t stsd;
    fmp4_box_t entry;
    fmp4_stsd_entry_t stsd_entry;
    if (!fmp4_box_read(p, moov_end, &trak)) break;
    if (memcmp(trak.fourcc, "trak", 4)) {
      p = trak.next;
      continue;
    }
    if (!fmp4_box_find(trak.body, trak.body_len, "tkhd", &tkhd) || tkhd.body_len < 24) { p = trak.next; continue; }
    if (!fmp4_box_find(trak.body, trak.body_len, "mdia", &mdia)) { p = trak.next; continue; }
    if (!fmp4_box_find(mdia.body, mdia.body_len, "mdhd", &mdhd) || mdhd.body_len < 24) { p = trak.next; continue; }
    if (!fmp4_box_find(mdia.body, mdia.body_len, "minf", &minf)) { p = trak.next; continue; }
    if (!fmp4_box_find(minf.body, minf.body_len, "stbl", &stbl)) { p = trak.next; continue; }
    if (!fmp4_box_find(stbl.body, stbl.body_len, "stsd", &stsd) || stsd.body_len < 8) { p = trak.next; continue; }
    if (!fmp4_box_read(stsd.body + 8, stsd.body + stsd.body_len, &entry)) { p = trak.next; continue; }
    if (!fmp4_parse_stsd_entry(&entry, &stsd_entry)) { p = trak.next; continue; }

    cand[n].track_id = fmp4_rb_u32(tkhd.body + (tkhd.body[0] == 1 ? 20 : 12));
    cand[n].stream_idx = stream_idx;
    cand[n].timescale = fmp4_rb_u32(mdhd.body + (mdhd.body[0] == 1 ? 20 : 12));
    if (stsd_entry.codec == CODEC_AAC && stsd_entry.cpriv_len < 2) {
      log_code("LW400239", "fMP4 remux: track %u AAC without AudioSpecificConfig, track skipped", cand[n].track_id);
      p = trak.next;
      continue;
    }
    if (!esbuild_track_init(&cand[n].track, &stsd_entry)) {
      log_code("LW400227", "fMP4 remux: track %u parameter sets exceed %d bytes, track skipped", cand[n].track_id, ESBUILD_PS_POOL);
      p = trak.next;
      continue;
    }
    n++;
    p = trak.next;
  }
  new_n = n;
  if (new_n == r->n_tracks) return 0;

  for (i = 0; i < new_n; i++) codecs[i] = cand[i].track.codec;
  esbuild_assign_pids(codecs, new_n, assigned);
  for (i = 0; i < new_n; i++) {
    r->tracks[i].track = cand[i].track;
    r->tracks[i].es = assigned[i];
    r->tracks[i].fmp4_track_id = cand[i].track_id;
    r->tracks[i].stream_idx = cand[i].stream_idx;
    r->tracks[i].timescale = cand[i].timescale ? cand[i].timescale : 90000;
  }
  r->n_tracks = new_n;
  r->pmt_version = (r->pmt_version + 1) & 0x1F;
  r->have_init = 1;
  return 1;
}

void esbuild_remux_free(esbuild_remux_t *r) {
  free(r->pesbuf);
  free(r->esbuf);
  r->pesbuf = NULL;
  r->esbuf = NULL;
  r->pes_cap = 0;
  r->es_cap = 0;
}

int esbuild_remux_init(esbuild_remux_t *r, const unsigned char *init_data, size_t init_len) {
  memset(r, 0, sizeof *r);
  return esbuild_remux_add_init(r, 0, init_data, init_len);
}

static esbuild_remux_track_t *find_track(esbuild_remux_t *r, unsigned stream_idx, unsigned fmp4_track_id) {
  for (unsigned i = 0; i < r->n_tracks; i++)
    if (r->tracks[i].stream_idx == stream_idx && r->tracks[i].fmp4_track_id == fmp4_track_id) return &r->tracks[i];
  return NULL;
}

static int grow_buf(unsigned char **buf, size_t *cap, size_t need) {
  unsigned char *p;
  if (*cap >= need) return 1;
  p = realloc(*buf, need);
  if (!p) return 0;
  *buf = p;
  *cap = need;
  return 1;
}

static unsigned emit_pat_pmt(esbuild_remux_t *r, ts_packet_cb cb, void *cb_ctx) {
  unsigned char sec[188];
  unsigned char ptr = 0x00;
  size_t n;
  esbuild_es_t es_list[ESBUILD_MAX_ES];

  n = esbuild_build_pat(1, 0, 1, sec, sizeof sec);
  if (n) ts_packet_emit(0x0000, &r->pat_cc, &ptr, sec, n, 0, 0, cb, cb_ctx);

  ptr = 0x00;
  for (unsigned i = 0; i < r->n_tracks; i++) es_list[i] = r->tracks[i].es;
  n = esbuild_build_pmt(r->pmt_version, 1, es_list, r->n_tracks, sec, sizeof sec);
  if (n) ts_packet_emit(ESBUILD_PMT_PID, &r->pmt_cc, &ptr, sec, n, 0, 0, cb, cb_ctx);
  return esbuild_pcr_pid(es_list, r->n_tracks);
}

/* one moof+mdat. ret: samples dropped */
static unsigned feed_fragment(esbuild_remux_t *r, unsigned stream_idx, const fmp4_box_t *moof_box, const fmp4_box_t *mdat_box, unsigned pcr_pid, ts_packet_cb cb, void *cb_ctx) {
  const fmp4_box_t moof = *moof_box;
  const fmp4_box_t mdat = *mdat_box;
  const unsigned char *p;
  const unsigned char *end;
  unsigned dropped = 0;

  p = moof.body;
  end = moof.body + moof.body_len;
  while ((size_t)(end - p) >= 8) {
    fmp4_box_t traf;
    fmp4_box_t tfhd_box;
    fmp4_box_t tfdt_box;
    fmp4_box_t trun_box;
    fmp4_tfhd_t tfhd;
    esbuild_remux_track_t *rt;
    uint64_t base_dts;
    fmp4_dec_sample_t samples[FMP4_MAX_SAMPLES];
    unsigned n_samples;
    uint64_t dts;

    if (!fmp4_box_read(p, end, &traf)) break;
    if (memcmp(traf.fourcc, "traf", 4)) {
      p = traf.next;
      continue;
    }
    if (!fmp4_box_find(traf.body, traf.body_len, "tfhd", &tfhd_box) || !fmp4_parse_tfhd(tfhd_box.body, tfhd_box.body_len, &tfhd)) {
      p = traf.next;
      continue;
    }
    rt = find_track(r, stream_idx, tfhd.track_id);
    if (!rt) {
      p = traf.next;
      continue;
    }

    base_dts = rt->next_dts;
    if (fmp4_box_find(traf.body, traf.body_len, "tfdt", &tfdt_box)) fmp4_parse_tfdt(tfdt_box.body, tfdt_box.body_len, &base_dts);

    n_samples = 0;
    if (fmp4_box_find(traf.body, traf.body_len, "trun", &trun_box))
      n_samples = fmp4_parse_trun_samples(trun_box.body, trun_box.body_len, &tfhd, &moof, mdat.body, mdat.body_len, samples, FMP4_MAX_SAMPLES);

    dts = base_dts;
    for (unsigned i = 0; i < n_samples; i++) {
      size_t bound = esbuild_es_bound(&rt->track, samples[i].size);
      size_t eslen = 0;
      if (grow_buf(&r->esbuf, &r->es_cap, bound) && grow_buf(&r->pesbuf, &r->pes_cap, bound + ESBUILD_PES_HDR_MAX)) eslen = esbuild_convert_sample(&rt->track, &samples[i], r->esbuf, r->es_cap);
      if (eslen) {
        uint64_t dts_90k = to_90k(dts, rt->timescale);
        uint64_t pts_90k = to_90k(dts + (uint64_t)(int64_t)samples[i].cts_offset, rt->timescale);
        if (!esbuild_ts_packetize(rt->es.pid, &rt->cc, stream_id_for(rt->track.codec), pts_90k, 1, dts_90k, r->esbuf, eslen, rt->es.pid == pcr_pid, dts_90k, r->pesbuf, r->pes_cap, cb, cb_ctx)) dropped++;
      } else dropped++;
      dts += samples[i].duration;
    }
    rt->next_dts = dts;
    p = traf.next;
  }
  return dropped;
}

/* chunked CMAF: N moof+mdat pairs */
void esbuild_remux_feed(esbuild_remux_t *r, unsigned stream_idx, const unsigned char *seg_data, size_t seg_len, ts_packet_cb cb, void *cb_ctx) {
  const unsigned char *p = seg_data;
  const unsigned char *end = seg_data + seg_len;
  fmp4_box_t moof;
  int have_moof = 0;
  int psi_sent = 0;
  unsigned pcr_pid = 0;
  unsigned dropped = 0;

  if (!r->have_init) return;
  while ((size_t)(end - p) >= 8) {
    fmp4_box_t box;
    if (!fmp4_box_read(p, end, &box)) break;
    if (!memcmp(box.fourcc, "moof", 4)) {
      moof = box;
      have_moof = 1;
    } else if (!memcmp(box.fourcc, "mdat", 4) && have_moof) {
      if (!psi_sent) {
        pcr_pid = emit_pat_pmt(r, cb, cb_ctx);
        psi_sent = 1;
      }
      dropped += feed_fragment(r, stream_idx, &moof, &box, pcr_pid, cb, cb_ctx);
      have_moof = 0;
    }
    p = box.next;
  }
  if (dropped) log_code("LW400228", "fMP4 remux: %u sample(s) dropped, conversion failed", dropped);
}
