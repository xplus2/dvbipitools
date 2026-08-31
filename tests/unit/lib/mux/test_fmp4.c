/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/fmp4/fmp4.h"

static unsigned rd16(const unsigned char *p) {
  return ((unsigned)p[0] << 8) | p[1];
}

static uint32_t rd32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t rd64(const unsigned char *p) {
  return ((uint64_t)rd32(p) << 32) | rd32(p + 4);
}

/* path: dot-separated fourcc chain, e.g. "moov.trak.tkhd". 1 = match (out/outlen = payload past 8B header), 0 = missing seg */
static int find_box(const unsigned char *buf, size_t len, const char *path, const unsigned char **out, size_t *outlen) {
  char pathbuf[128];
  char *seg, *save = NULL;
  const unsigned char *p = buf;
  size_t rem = len;

  strncpy(pathbuf, path, sizeof pathbuf - 1);
  pathbuf[sizeof pathbuf - 1] = '\0';
  seg = strtok_r(pathbuf, ".", &save);
  while (seg) {
    const unsigned char *found = NULL;
    size_t foundlen = 0;
    const unsigned char *q = p;
    size_t qrem = rem;
    while (qrem >= 8) {
      uint32_t bsize = rd32(q);
      if (bsize < 8 || bsize > qrem)
        return 0;
      if (memcmp(q + 4, seg, 4) == 0) {
        found = q + 8;
        foundlen = bsize - 8;
        break;
      }
      q += bsize;
      qrem -= bsize;
    }
    if (!found)
      return 0;
    p = found;
    rem = foundlen;
    seg = strtok_r(NULL, ".", &save);
  }
  *out = p;
  *outlen = rem;
  return 1;
}

static fmp4_mux_t *make_video_mux(unsigned track_id, unsigned w, unsigned h) {
  static const unsigned char fake_avcc[] = {0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x00, 0x01, 0x00, 0x00};
  fmp4_track_cfg_t trk;
  memset(&trk, 0, sizeof trk);
  trk.codec = CODEC_H264;
  trk.track_id = track_id;
  trk.timescale = 90000;
  trk.width = w;
  trk.height = h;
  trk.cpriv = fake_avcc;
  trk.cpriv_len = sizeof fake_avcc;
  return fmp4_mux_new(&trk, 1);
}

static fmp4_mux_t *make_aac_mux(codec_t codec, const unsigned char *asc, size_t asclen) {
  fmp4_track_cfg_t trk;
  memset(&trk, 0, sizeof trk);
  trk.codec = codec;
  trk.track_id = 2;
  trk.timescale = 44100;
  trk.rate = 44100;
  trk.channels = 2;
  trk.cpriv = asc;
  trk.cpriv_len = asclen;
  return fmp4_mux_new(&trk, 1);
}

static fmp4_mux_t *make_ac3_mux(codec_t codec, unsigned char bsid, unsigned char bsmod, unsigned char acmod,
                                 unsigned char lfeon, unsigned bitrate_code) {
  fmp4_track_cfg_t trk;
  memset(&trk, 0, sizeof trk);
  trk.codec = codec;
  trk.track_id = 2;
  trk.timescale = 48000;
  trk.rate = 48000;
  trk.channels = 2;
  trk.ac3_bsid = bsid;
  trk.ac3_bsmod = bsmod;
  trk.ac3_acmod = acmod;
  trk.ac3_lfeon = lfeon;
  trk.ac3_bitrate_code = bitrate_code;
  return fmp4_mux_new(&trk, 1);
}

START_TEST(aac_stsd_has_mp4a_esds_with_asc) {
  static const unsigned char asc[] = {0x12, 0x10};
  fmp4_mux_t *m = make_aac_mux(CODEC_AAC, asc, sizeof asc);
  unsigned char *out;
  const unsigned char *stsd, *mp4a, *esds;
  size_t len, stsd_len, mp4a_len, esds_len;

  len = fmp4_init_segment(m, &out);
  ck_assert(find_box(out, len, "moov.trak.mdia.minf.stbl.stsd", &stsd, &stsd_len));
  ck_assert(find_box(stsd + 8, stsd_len - 8, "mp4a", &mp4a, &mp4a_len));
  ck_assert_uint_eq(rd16(mp4a + 16), 2);      /* channelcount, past 2x reserved(6+8)+dref_idx(2) */
  ck_assert_uint_eq(rd32(mp4a + 24) >> 16, 44100); /* samplerate, 16.16 fixed */
  ck_assert(find_box(mp4a + 28, mp4a_len - 28, "esds", &esds, &esds_len));

  ck_assert_int_eq(esds[4], 0x03); /* ES_Descriptor tag */
  ck_assert_int_eq(esds[11], 0x40); /* objectTypeIndication: MPEG-4 Audio */
  ck_assert_int_eq(esds[24], 0x05); /* DecoderSpecificInfo tag */
  ck_assert_uint_eq(esds[25], sizeof asc);
  ck_assert_int_eq(memcmp(esds + 26, asc, sizeof asc), 0);
  ck_assert_int_eq(esds[28], 0x06); /* SLConfigDescriptor tag */

  fmp4_mux_free(m);
}
END_TEST

START_TEST(mp2a_stsd_esds_has_no_decoder_specific_info) {
  fmp4_mux_t *m = make_aac_mux(CODEC_MP2A, NULL, 0);
  unsigned char *out;
  const unsigned char *stsd, *mp4a, *esds;
  size_t len, stsd_len, mp4a_len, esds_len;

  len = fmp4_init_segment(m, &out);
  ck_assert(find_box(out, len, "moov.trak.mdia.minf.stbl.stsd", &stsd, &stsd_len));
  ck_assert(find_box(stsd + 8, stsd_len - 8, "mp4a", &mp4a, &mp4a_len));
  ck_assert(find_box(mp4a + 28, mp4a_len - 28, "esds", &esds, &esds_len));

  ck_assert_int_eq(esds[11], 0x6B); /* objectTypeIndication: MPEG-1 Audio */
  ck_assert_int_eq(esds[24], 0x06); /* SLConfigDescriptor tag, no DecoderSpecificInfo before it */

  fmp4_mux_free(m);
}
END_TEST

START_TEST(ac3_stsd_has_dac3_with_bsi_fields) {
  fmp4_mux_t *m = make_ac3_mux(CODEC_AC3, 8, 1, 7, 1, 15);
  unsigned char *out;
  const unsigned char *stsd, *entry, *dac3;
  size_t len, stsd_len, entry_len, dac3_len;

  len = fmp4_init_segment(m, &out);
  ck_assert(find_box(out, len, "moov.trak.mdia.minf.stbl.stsd", &stsd, &stsd_len));
  ck_assert(find_box(stsd + 8, stsd_len - 8, "ac-3", &entry, &entry_len));
  ck_assert(find_box(entry + 28, entry_len - 28, "dac3", &dac3, &dac3_len));

  ck_assert_uint_eq(dac3_len, 3);
  ck_assert_uint_eq(dac3[0] >> 6, 0);           /* fscod: 48000 -> 0 */
  ck_assert_uint_eq((dac3[0] >> 1) & 0x1F, 8);  /* bsid */
  ck_assert_uint_eq(((unsigned)(dac3[0] & 1) << 2) | (dac3[1] >> 6), 1); /* bsmod */
  ck_assert_uint_eq((dac3[1] >> 3) & 7, 7);     /* acmod */
  ck_assert_uint_eq((dac3[1] >> 2) & 1, 1);     /* lfeon */
  ck_assert_uint_eq(((unsigned)(dac3[1] & 3) << 3) | (dac3[2] >> 5), 15); /* bit_rate_code */

  fmp4_mux_free(m);
}
END_TEST

START_TEST(eac3_stsd_has_dec3_with_bsi_fields) {
  fmp4_mux_t *m = make_ac3_mux(CODEC_EAC3, 16, 0, 2, 1, 192);
  unsigned char *out;
  const unsigned char *stsd, *entry, *dec3;
  size_t len, stsd_len, entry_len, dec3_len;

  len = fmp4_init_segment(m, &out);
  ck_assert(find_box(out, len, "moov.trak.mdia.minf.stbl.stsd", &stsd, &stsd_len));
  ck_assert(find_box(stsd + 8, stsd_len - 8, "ec-3", &entry, &entry_len));
  ck_assert(find_box(entry + 28, entry_len - 28, "dec3", &dec3, &dec3_len));

  ck_assert_uint_eq(dec3_len, 5);
  ck_assert_uint_eq(rd16(dec3) >> 3, 192);      /* data_rate */
  ck_assert_uint_eq(dec3[2] >> 6, 0);           /* fscod: 48000 -> 0 */
  ck_assert_uint_eq((dec3[2] >> 1) & 0x1F, 16); /* bsid */
  ck_assert_uint_eq(dec3[3] >> 5, 0);           /* bsmod */
  ck_assert_uint_eq((dec3[3] >> 2) & 7, 2);     /* acmod */
  ck_assert_uint_eq((dec3[3] >> 1) & 1, 1);     /* lfeon */

  fmp4_mux_free(m);
}
END_TEST

START_TEST(init_segment_has_ftyp_then_moov) {
  fmp4_mux_t *m = make_video_mux(1, 320, 240);
  unsigned char *out;
  size_t len = fmp4_init_segment(m, &out);
  ck_assert_uint_gt(len, 8);
  ck_assert_int_eq(memcmp(out + 4, "ftyp", 4), 0);
  ck_assert_uint_eq(rd32(out), 28); /* 8-byte header + major(4)+minor(4)+3 compat brands(4 each) */
  fmp4_mux_free(m);
}
END_TEST

START_TEST(init_segment_track_fields_correct) {
  fmp4_mux_t *m = make_video_mux(7, 1280, 720);
  unsigned char *out;
  const unsigned char *tkhd, *mdhd, *stsd, *avc1;
  size_t len, tkhd_len, mdhd_len, stsd_len, avc1_len;

  len = fmp4_init_segment(m, &out);
  ck_assert(find_box(out, len, "moov.trak.tkhd", &tkhd, &tkhd_len));
  ck_assert_uint_eq(rd32(tkhd + 12), 7); /* track_ID: past version/flags, creation, modification (3x u32) */
  ck_assert_uint_eq(rd32(tkhd + tkhd_len - 8) >> 16, 1280); /* width, 16.16 fixed */
  ck_assert_uint_eq(rd32(tkhd + tkhd_len - 4) >> 16, 720);  /* height */

  ck_assert(find_box(out, len, "moov.trak.mdia.mdhd", &mdhd, &mdhd_len));
  ck_assert_uint_eq(rd32(mdhd + 12), 90000); /* timescale: after version/flags,creation,modification */

  ck_assert(find_box(out, len, "moov.trak.mdia.minf.stbl.stsd", &stsd, &stsd_len));
  ck_assert_uint_eq(rd32(stsd + 4), 1); /* entry_count */
  ck_assert(find_box(stsd + 8, stsd_len - 8, "avc1", &avc1, &avc1_len)); /* +8: past stsd's version/flags/entry_count */
  ck_assert(find_box(avc1 + 78, avc1_len - 78, "avcC", &avc1, &avc1_len)); /* +78: past VisualSampleEntry's fixed fields */
  ck_assert_uint_eq(avc1_len, 11);
  ck_assert_int_eq(avc1[0], 0x01);

  fmp4_mux_free(m);
}
END_TEST

START_TEST(fragment_moof_and_mdat_match_samples) {
  fmp4_mux_t *m = make_video_mux(1, 320, 240);
  unsigned char *out;
  size_t len;
  static const unsigned char sample0[] = {0xAA, 0xAA, 0xAA, 0xAA};
  static const unsigned char sample1[] = {0xBB, 0xBB};
  fmp4_sample_t s;
  const unsigned char *mfhd, *tfhd, *tfdt, *trun, *mdat;
  size_t mfhd_len, tfhd_len, tfdt_len, trun_len, mdat_len;

  fmp4_segment_begin(m, 42);
  memset(&s, 0, sizeof s);
  s.track_idx = 0;
  s.data = sample0;
  s.size = sizeof sample0;
  s.duration = 3600;
  s.keyframe = 1;
  fmp4_segment_add_sample(m, &s);
  s.data = sample1;
  s.size = sizeof sample1;
  s.duration = 3600;
  s.keyframe = 0;
  fmp4_segment_add_sample(m, &s);
  len = fmp4_segment_end(m, &out);

  ck_assert_int_eq(memcmp(out + 4, "styp", 4), 0);

  ck_assert(find_box(out, len, "moof.mfhd", &mfhd, &mfhd_len));
  ck_assert_uint_eq(rd32(mfhd + 4), 42); /* sequence_number */

  ck_assert(find_box(out, len, "moof.traf.tfhd", &tfhd, &tfhd_len));
  ck_assert_uint_eq(rd32(tfhd + 4), 1); /* track_ID */

  ck_assert(find_box(out, len, "moof.traf.tfdt", &tfdt, &tfdt_len));
  ck_assert_uint_eq(rd64(tfdt + 4), 0); /* baseMediaDecodeTime */

  ck_assert(find_box(out, len, "moof.traf.trun", &trun, &trun_len));
  ck_assert_uint_eq(rd32(trun + 4), 2); /* sample_count */
  {
    uint32_t data_offset = rd32(trun + 8);
    const unsigned char *sample_entry = trun + 12;
    ck_assert_uint_eq(rd32(sample_entry), 3600);          /* sample 0 duration */
    ck_assert_uint_eq(rd32(sample_entry + 4), sizeof sample0); /* sample 0 size */
    ck_assert_uint_eq(rd32(sample_entry + 8), 0x02000000u);    /* sample 0 flags: keyframe */
    sample_entry += 16;
    ck_assert_uint_eq(rd32(sample_entry + 4), sizeof sample1);
    ck_assert_uint_eq(rd32(sample_entry + 8), 0x01010000u);    /* sample 1 flags: non-key */

    ck_assert(find_box(out, len, "mdat", &mdat, &mdat_len));
    ck_assert_uint_eq(mdat_len, sizeof sample0 + sizeof sample1);
    ck_assert_int_eq(memcmp(mdat, sample0, sizeof sample0), 0);
    ck_assert_int_eq(memcmp(mdat + sizeof sample0, sample1, sizeof sample1), 0);

    {
      uint32_t moof_start_off = rd32(out);
      ck_assert_uint_eq((size_t)(mdat - out) - moof_start_off, data_offset);
    }
  }
  fmp4_mux_free(m);
}
END_TEST

START_TEST(second_fragment_tfdt_advances_by_first_fragment_duration) {
  fmp4_mux_t *m = make_video_mux(1, 320, 240);
  unsigned char *out;
  size_t len;
  static const unsigned char sample0[] = {0xAA};
  fmp4_sample_t s;
  const unsigned char *tfdt;
  size_t tfdt_len;

  memset(&s, 0, sizeof s);
  s.track_idx = 0;
  s.data = sample0;
  s.size = sizeof sample0;
  s.duration = 3600;
  s.keyframe = 1;

  fmp4_segment_begin(m, 1);
  fmp4_segment_add_sample(m, &s);
  fmp4_segment_add_sample(m, &s);
  fmp4_segment_end(m, &out);

  fmp4_segment_begin(m, 2);
  fmp4_segment_add_sample(m, &s);
  len = fmp4_segment_end(m, &out);

  ck_assert(find_box(out, len, "moof.traf.tfdt", &tfdt, &tfdt_len));
  ck_assert_uint_eq(rd64(tfdt + 4), 2 * 3600u);

  fmp4_mux_free(m);
}
END_TEST

Suite *fmp4_suite(void) {
  Suite *s = suite_create("fmp4");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, init_segment_has_ftyp_then_moov);
  tcase_add_test(tc, init_segment_track_fields_correct);
  tcase_add_test(tc, fragment_moof_and_mdat_match_samples);
  tcase_add_test(tc, second_fragment_tfdt_advances_by_first_fragment_duration);
  tcase_add_test(tc, aac_stsd_has_mp4a_esds_with_asc);
  tcase_add_test(tc, mp2a_stsd_esds_has_no_decoder_specific_info);
  tcase_add_test(tc, ac3_stsd_has_dac3_with_bsi_fields);
  tcase_add_test(tc, eac3_stsd_has_dec3_with_bsi_fields);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  int failed;
  SRunner *sr = srunner_create(fmp4_suite());
  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed ? 1 : 0;
}
