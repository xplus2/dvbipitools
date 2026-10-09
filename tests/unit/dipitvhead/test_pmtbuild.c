/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE /* memmem */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/mux/pmtbuild.h"
#include "lib/demux/crc32.h"
#include "lib/mux/psi_build.h"
#include "lib/sys/ioutil.h"

static void wrap_ts_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (size_t i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

START_TEST(pmtbuild_map_es_picks_video_first_and_drops_unsupported) {
  psi_es_t es[5];
  out_es_t out_es[8];
  out_program_pids_t pids;
  unsigned pcr_pid;
  int n;
  int dropped;

  out_program_pids(0, &pids);
  memset(es, 0, sizeof es);
  es[0].pid = 0x0101;
  es[0].cls = PID_VIDEO;
  es[0].codec = CODEC_H264;
  es[1].pid = 0x0102;
  es[1].cls = PID_AUDIO;
  es[1].codec = CODEC_AAC;
  bufcpy(es[1].lang, sizeof es[1].lang, "deu");
  es[2].pid = 0x0103;
  es[2].cls = PID_SUBTITLE;
  es[2].sub_type = 1;
  es[2].sub_composition_page = 100;
  es[2].sub_ancillary_page = 200;
  bufcpy(es[2].lang, sizeof es[2].lang, "deu");
  es[3].pid = 0x0104;
  es[3].cls = PID_DATA; /* unsupported, must be dropped */
  es[4].pid = 0x0105;
  es[4].cls = PID_TELETEXT;
  es[4].ttx_page = 777;
  es[4].ttx_type = 2;
  bufcpy(es[4].ttx_lang, sizeof es[4].ttx_lang, "deu");

  n = pmtbuild_map_es(es, 5, TVSTRIP_DATA, 0x0102 /* PCR on the audio pid */, pids.video_pid, pids.es_pid_base, out_es, 8, &pcr_pid, &dropped);

  ck_assert_int_eq(n, 4); /* video + audio + subtitle + teletext, PID_DATA dropped */
  ck_assert_int_eq(dropped, 0); /* unsupported ES isn't a cap drop */
  ck_assert_uint_eq(out_es[0].out_pid, pids.video_pid);
  ck_assert_uint_eq(out_es[0].in_pid, 0x0101u);
  ck_assert_uint_eq(out_es[1].out_pid, pids.es_pid_base);
  ck_assert_uint_eq(out_es[1].in_pid, 0x0102u);
  ck_assert_uint_eq(out_es[2].in_pid, 0x0103u);
  ck_assert_uint_eq(out_es[3].in_pid, 0x0105u);
  ck_assert_uint_eq(pcr_pid, out_es[1].out_pid); /* reassigned to match src_pcr_pid, not the default video pid */
}
END_TEST

START_TEST(pmtbuild_map_es_defaults_pcr_to_first_es_when_no_match) {
  psi_es_t es[1];
  out_es_t out_es[4];
  out_program_pids_t pids;
  unsigned pcr_pid;
  int n;
  int dropped;

  out_program_pids(0, &pids);
  memset(es, 0, sizeof es);
  es[0].pid = 0x0101;
  es[0].cls = PID_VIDEO;
  es[0].codec = CODEC_H264;

  n = pmtbuild_map_es(es, 1, 0, 0x9999 /* no ES has this pid */, pids.video_pid, pids.es_pid_base, out_es, 4, &pcr_pid, &dropped);
  ck_assert_int_eq(n, 1);
  ck_assert_int_eq(dropped, 0);
  ck_assert_uint_eq(pcr_pid, out_es[0].out_pid);
}
END_TEST

START_TEST(pmtbuild_map_es_picks_video_when_the_source_declares_no_pcr_pid) {
  psi_es_t es[2];
  out_es_t out_es[4];
  out_program_pids_t pids;
  unsigned pcr_pid;
  int n;
  int dropped;

  out_program_pids(0, &pids);
  memset(es, 0, sizeof es);
  es[0].pid = 0x0102;
  es[0].cls = PID_AUDIO;
  es[0].codec = CODEC_AAC_LATM;
  es[1].pid = 0x0101;
  es[1].cls = PID_VIDEO;
  es[1].codec = CODEC_H264;

  n = pmtbuild_map_es(es, 2, 0, 0x1FFF, pids.video_pid, pids.es_pid_base, out_es, 4, &pcr_pid, &dropped);
  ck_assert_int_eq(n, 2);
  ck_assert_uint_eq(pcr_pid, pids.video_pid);
}
END_TEST

START_TEST(pmtbuild_map_es_picks_the_first_es_for_audio_only_without_a_pcr_pid) {
  psi_es_t es[2];
  out_es_t out_es[4];
  out_program_pids_t pids;
  unsigned pcr_pid;
  int n;
  int dropped;

  out_program_pids(0, &pids);
  memset(es, 0, sizeof es);
  es[0].pid = 0x0102;
  es[0].cls = PID_AUDIO;
  es[0].codec = CODEC_AAC_LATM;
  es[1].pid = 0x0103;
  es[1].cls = PID_AUDIO;
  es[1].codec = CODEC_AAC_LATM;

  n = pmtbuild_map_es(es, 2, 0, 0x1FFF, pids.video_pid, pids.es_pid_base, out_es, 4, &pcr_pid, &dropped);
  ck_assert_int_eq(n, 2);
  ck_assert_uint_eq(pcr_pid, pids.es_pid_base);
  ck_assert_uint_eq(out_es[0].in_pid, 0x0102u);
}
END_TEST

START_TEST(pmtbuild_map_es_reports_dropped_beyond_cap) {
  psi_es_t es[4];
  out_es_t out_es[2];
  out_program_pids_t pids;
  unsigned pcr_pid;
  int n;
  int dropped;
  int i;

  out_program_pids(0, &pids);
  memset(es, 0, sizeof es);
  es[0].pid = 0x0101;
  es[0].cls = PID_VIDEO;
  es[0].codec = CODEC_H264;
  for (i = 1; i < 4; i++) {
    es[i].pid = (unsigned)(0x0101 + i);
    es[i].cls = PID_AUDIO;
    es[i].codec = CODEC_AAC;
  }

  n = pmtbuild_map_es(es, 4, 0, es[0].pid, pids.video_pid, pids.es_pid_base, out_es, 2, &pcr_pid, &dropped);

  ck_assert_int_eq(n, 2); /* video + one audio, cap is 2 */
  ck_assert_int_eq(dropped, 2); /* the other two audio ES */
}
END_TEST

START_TEST(pmtbuild_pmt_round_trips_video_audio_subtitle_teletext) {
  /* CA_descriptor(0x09) & stream_identifier_descriptor(0x52, EN 300 468 6.2.39) */
  static const unsigned char video_desc[] = {0x09, 4, 0x4A, 0x75, 0xE0, 0x20, 0x52, 1, 7};
  static const unsigned char audio_desc[] = {0x0A, 4, 'd', 'e', 'u', 0x00};
  static const unsigned char sub_desc[] = {0x59, 16, 'd', 'e', 'u', 1, 0, 100, 0, 200, 'e', 'n', 'g', 2, 0, 101, 0, 201};
  static const unsigned char ttx_desc[] = {0x56, 10, 'e', 'n', 'g', 0x09, 0x00, 'd', 'e', 'u', 0x17, 0x77};
  psi_es_t es[4];
  out_es_t out_es[8];
  out_program_pids_t pids;
  unsigned pcr_pid;
  int n;
  int dropped;
  int desc_truncated;
  unsigned char section[512];
  unsigned char pkt[188];
  unsigned char pat_section[32];
  size_t slen;
  size_t pat_len;
  psi_t *p;
  const psi_es_t *dec;
  int count;

  out_program_pids(0, &pids);
  memset(es, 0, sizeof es);
  es[0].pid = 0x0101;
  es[0].cls = PID_VIDEO;
  es[0].codec = CODEC_H264;
  memcpy(es[0].desc, video_desc, sizeof video_desc);
  es[0].desc_len = sizeof video_desc;
  es[1].pid = 0x0102;
  es[1].cls = PID_AUDIO;
  es[1].codec = CODEC_AAC;
  memcpy(es[1].desc, audio_desc, sizeof audio_desc);
  es[1].desc_len = sizeof audio_desc;
  es[2].pid = 0x0103;
  es[2].cls = PID_SUBTITLE;
  memcpy(es[2].desc, sub_desc, sizeof sub_desc);
  es[2].desc_len = sizeof sub_desc;
  es[3].pid = 0x0104;
  es[3].cls = PID_TELETEXT;
  memcpy(es[3].desc, ttx_desc, sizeof ttx_desc);
  es[3].desc_len = sizeof ttx_desc;

  n = pmtbuild_map_es(es, 4, 0, 0x0101, pids.video_pid, pids.es_pid_base, out_es, 8, &pcr_pid, &dropped);
  ck_assert_int_eq(n, 4);
  ck_assert_int_eq(dropped, 0);

  slen = pmtbuild_pmt(1, 55, pcr_pid, NULL, 0, out_es, n, NULL, 0, section, sizeof section, &desc_truncated);
  ck_assert_uint_ne(slen, 0u);
  ck_assert_int_eq(desc_truncated, 0);
  ck_assert_uint_eq(crc32_mpeg(section, slen), 0u);

  p = psi_new();
  pat_len = psi_build_pat(0x1234, 0, 55, pids.pmt_pid, pat_section, sizeof pat_section);
  wrap_ts_packet(pkt, OUT_PID_PAT, pat_section, pat_len);
  psi_feed(p, pkt);
  wrap_ts_packet(pkt, pids.pmt_pid, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_pmt(p), 1);
  ck_assert_uint_eq(psi_pcr_pid(p), pcr_pid);
  dec = psi_es(p, &count);
  ck_assert_int_eq(count, 4);

  ck_assert_uint_eq(dec[0].pid, pids.video_pid);
  ck_assert_int_eq(dec[0].cls, PID_VIDEO);
  ck_assert_int_eq(dec[0].codec, CODEC_H264);
  ck_assert_uint_eq(dec[0].desc_len, 3u);
  ck_assert_mem_eq(dec[0].desc, video_desc + 6, 3);

  ck_assert_int_eq(dec[1].cls, PID_AUDIO);
  ck_assert_int_eq(dec[1].codec, CODEC_AAC);
  ck_assert_str_eq(dec[1].lang, "deu");

  ck_assert_int_eq(dec[2].cls, PID_SUBTITLE);
  ck_assert_uint_eq(dec[2].sub_type, 1u);
  ck_assert_uint_eq(dec[2].sub_composition_page, 100u);
  ck_assert_uint_eq(dec[2].sub_ancillary_page, 200u);
  ck_assert_uint_eq(dec[2].desc_len, sizeof sub_desc);
  ck_assert_mem_eq(dec[2].desc, sub_desc, sizeof sub_desc);

  ck_assert_int_eq(dec[3].cls, PID_TELETEXT);
  ck_assert_uint_eq(dec[3].ttx_page, 777u);
  ck_assert_int_eq(dec[3].ttx_type, 2);
  ck_assert_str_eq(dec[3].ttx_lang, "deu");
  ck_assert_uint_eq(dec[3].desc_len, sizeof ttx_desc);
  ck_assert_mem_eq(dec[3].desc, ttx_desc, sizeof ttx_desc);

  psi_free(p);
}
END_TEST

START_TEST(pmtbuild_pmt_truncates_es_descriptors_gracefully) {
  /* 20x3-byte descriptors */
  unsigned char big_desc[60];
  psi_es_t src;
  out_es_t out_es[1];
  unsigned char out[48];
  size_t slen;
  int desc_truncated;
  for (int i = 0; i < 20; i++) {
    big_desc[i * 3] = 0x80;
    big_desc[i * 3 + 1] = 1;
    big_desc[i * 3 + 2] = (unsigned char)i;
  }
  memset(&src, 0, sizeof src);
  src.cls = PID_VIDEO;
  src.codec = CODEC_H264;
  memcpy(src.desc, big_desc, sizeof big_desc);
  src.desc_len = sizeof big_desc;
  memset(out_es, 0, sizeof out_es);
  out_es[0].out_pid = 0x0101;
  out_es[0].stream_type = 0x1B;
  out_es[0].src = &src;
  slen = pmtbuild_pmt(0, 1, 0x0101, NULL, 0, out_es, 1, NULL, 0, out, sizeof out, &desc_truncated);

  ck_assert_uint_ne(slen, 0u);
  ck_assert_int_eq(desc_truncated, 1);
  ck_assert_uint_eq(crc32_mpeg(out, slen), 0u);
  ck_assert_uint_le(slen, sizeof out);
}
END_TEST

START_TEST(pmtbuild_pmt_rejects_small_cap) {
  out_es_t es[1];
  unsigned char out[8];
  int desc_truncated;
  memset(es, 0, sizeof es);
  ck_assert_uint_eq(pmtbuild_pmt(0, 1, 0x100, NULL, 0, es, 0, NULL, 0, out, sizeof out, &desc_truncated), 0u);
}
END_TEST

START_TEST(pmtbuild_pmt_places_prog_desc_in_program_info) {
  static const unsigned char ca_desc[] = {0x09, 4, 0x4A, 0x75, 0xE0, 0x20};
  psi_es_t src;
  out_es_t es[1];
  out_program_pids_t pids;
  unsigned char out[64];
  size_t slen;
  int desc_truncated;

  out_program_pids(0, &pids);
  memset(&src, 0, sizeof src);
  src.cls = PID_VIDEO;
  memset(es, 0, sizeof es);
  es[0].out_pid = pids.video_pid;
  es[0].stream_type = 0x1B;
  es[0].src = &src;

  slen = pmtbuild_pmt(0, 1, pids.video_pid, ca_desc, sizeof ca_desc, es, 1, NULL, 0, out, sizeof out, &desc_truncated);
  ck_assert_uint_ne(slen, 0u);

  ck_assert_uint_eq(out[10], (unsigned char)(0xF0 | ((sizeof ca_desc >> 8) & 0x0F)));
  ck_assert_uint_eq(out[11], (unsigned char)sizeof ca_desc);
  ck_assert_mem_eq(out + 12, ca_desc, sizeof ca_desc);
  ck_assert_uint_eq(out[12 + sizeof ca_desc], 0x1B); /* ES loop starts right after program_info */
}
END_TEST

START_TEST(pmtbuild_pmt_round_trips_av1_registration_descriptor) {
  static const unsigned char av1_desc[] = {0x05, 4, 'A', 'V', '0', '1'};
  psi_es_t es[1];
  out_es_t out_es[4];
  out_program_pids_t pids;
  unsigned pcr_pid;
  int n;
  int dropped;
  int desc_truncated;
  unsigned char section[512];
  unsigned char pkt[188];
  unsigned char pat_section[32];
  size_t slen;
  size_t pat_len;
  psi_t *p;
  const psi_es_t *dec;
  int count;

  out_program_pids(0, &pids);
  memset(es, 0, sizeof es);
  es[0].pid = 0x0101;
  es[0].cls = PID_VIDEO;
  es[0].codec = CODEC_AV1;
  memcpy(es[0].desc, av1_desc, sizeof av1_desc);
  es[0].desc_len = sizeof av1_desc;

  n = pmtbuild_map_es(es, 1, 0, 0x0101, pids.video_pid, pids.es_pid_base, out_es, 4, &pcr_pid, &dropped);
  ck_assert_int_eq(n, 1);
  ck_assert_int_eq(dropped, 0);

  slen = pmtbuild_pmt(1, 55, pcr_pid, NULL, 0, out_es, n, NULL, 0, section, sizeof section, &desc_truncated);
  ck_assert_uint_ne(slen, 0u);
  ck_assert_int_eq(desc_truncated, 0);
  ck_assert_uint_eq(crc32_mpeg(section, slen), 0u);
  ck_assert_uint_eq(section[12], 0x06); /* stream_type: AV1 has no dedicated value, shares 0x06 */
  ck_assert_ptr_nonnull(memmem(section, slen, av1_desc, sizeof av1_desc));

  p = psi_new();
  pat_len = psi_build_pat(0x1234, 0, 55, pids.pmt_pid, pat_section, sizeof pat_section);
  wrap_ts_packet(pkt, OUT_PID_PAT, pat_section, pat_len);
  psi_feed(p, pkt);
  wrap_ts_packet(pkt, pids.pmt_pid, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_pmt(p), 1);
  dec = psi_es(p, &count);
  ck_assert_int_eq(count, 1);
  ck_assert_int_eq(dec[0].cls, PID_VIDEO);
  ck_assert_int_eq(dec[0].codec, CODEC_AV1);

  psi_free(p);
}
END_TEST

typedef struct {
  const char *name;
  unsigned ecm_pid;
  unsigned emm_pid;
  int pre;
  int cap;
  int expect_n;
  int expect_dropped;
  unsigned expect_ecm_out;
  unsigned expect_emm_out;
} ca_pass_case_t;

#define CA_BASE 0x0101
#define CA_VIDEO 0x0100

static const ca_pass_case_t ca_pass_cases[] = {
    {"ecm and emm after video and audio", 0x1FF0, 0x1FF1, 2, 8, 4, 0, CA_BASE + 1, CA_BASE + 2},
    {"ecm only", 0x1FF0, 0, 2, 8, 3, 0, CA_BASE + 1, 0},
    {"emm only", 0, 0x1FF1, 2, 8, 3, 0, 0, CA_BASE + 1},
    {"neither", 0, 0, 2, 8, 2, 0, 0, 0},
    {"cap leaves room for one", 0x1FF0, 0x1FF1, 2, 3, 3, 1, CA_BASE + 1, 0},
    {"cap already full", 0x1FF0, 0x1FF1, 2, 2, 2, 2, 0, 0},
    {"emm dropped alone when full", 0, 0x1FF1, 2, 2, 2, 1, 0, 0},
    {"program without any es", 0x1FF0, 0x1FF1, 0, 8, 2, 0, CA_BASE, CA_BASE + 1},
    {"video only program", 0x1FF0, 0x1FF1, 1, 8, 3, 0, CA_BASE, CA_BASE + 1},
};

static void fill_pre_existing(out_es_t *es, int pre) {
  memset(es, 0, 8 * sizeof *es);
  for (int i = 0; i < pre; i++) {
    es[i].in_pid = 0x0200 + (unsigned)i;
    es[i].out_pid = i == 0 ? CA_VIDEO : CA_BASE + (unsigned)(i - 1);
    es[i].stream_type = i == 0 ? 0x1B : 0x0F;
  }
}

START_TEST(pmtbuild_add_ca_passthrough_assigns_pids_and_honours_the_cap) {
  const ca_pass_case_t *c = &ca_pass_cases[_i];
  out_es_t es[8];
  int n = c->pre;
  int dropped = 0;
  int ecm_idx = -1;
  int emm_idx = -1;

  fill_pre_existing(es, c->pre);
  pmtbuild_add_ca_passthrough(c->ecm_pid, 0x4A75, c->emm_pid, 0x0963, CA_BASE, CA_VIDEO, es, &n, c->cap, &dropped);
  ck_assert_msg(n == c->expect_n, "%s: %d entries, want %d", c->name, n, c->expect_n);
  ck_assert_msg(dropped == c->expect_dropped, "%s: dropped %d, want %d", c->name, dropped, c->expect_dropped);

  for (int i = c->pre; i < n; i++) {
    ck_assert_msg(es[i].src == NULL && es[i].stream_type == 0, "%s: entry %d not synthetic", c->name, i);
    if (es[i].is_ca == CA_PASS_ECM) ecm_idx = i;
    if (es[i].is_ca == CA_PASS_EMM) emm_idx = i;
  }
  if (c->expect_ecm_out) {
    ck_assert_msg(ecm_idx >= 0, "%s: no ecm entry", c->name);
    ck_assert_msg(es[ecm_idx].out_pid == c->expect_ecm_out, "%s: ecm out pid 0x%X", c->name, es[ecm_idx].out_pid);
    ck_assert_msg(es[ecm_idx].in_pid == c->ecm_pid, "%s: ecm in pid", c->name);
    ck_assert_msg(es[ecm_idx].ca_system_id == 0x4A75, "%s: ecm ca system id", c->name);
  } else {
    ck_assert_msg(ecm_idx < 0, "%s: unexpected ecm entry", c->name);
  }
  if (c->expect_emm_out) {
    ck_assert_msg(emm_idx >= 0, "%s: no emm entry", c->name);
    ck_assert_msg(es[emm_idx].out_pid == c->expect_emm_out, "%s: emm out pid 0x%X", c->name, es[emm_idx].out_pid);
    ck_assert_msg(es[emm_idx].in_pid == c->emm_pid, "%s: emm in pid", c->name);
    ck_assert_msg(es[emm_idx].ca_system_id == 0x0963, "%s: emm ca system id", c->name);
  } else {
    ck_assert_msg(emm_idx < 0, "%s: unexpected emm entry", c->name);
  }
}
END_TEST

START_TEST(pmtbuild_pmt_keeps_ca_passthrough_out_of_the_es_loop_and_in_program_info) {
  static const unsigned char ca_desc[] = {0x09, 4, 0x4A, 0x75, 0xE1, 0xF0};
  static const unsigned char extra_entry[] = {0x05, 0xE0, 0x1F, 0xF0, 0x00};
  psi_es_t video_src;
  psi_es_t audio_src;
  out_es_t es[8];
  unsigned char section[256];
  int n = 2;
  int dropped = 0;
  int desc_truncated;
  size_t slen;
  size_t pos;
  size_t prog_info_len;
  unsigned loop_pids[8];
  int loop_count = 0;

  memset(&video_src, 0, sizeof video_src);
  memset(&audio_src, 0, sizeof audio_src);
  video_src.cls = PID_VIDEO;
  audio_src.cls = PID_AUDIO;
  fill_pre_existing(es, 2);
  es[0].src = &video_src;
  es[1].src = &audio_src;
  pmtbuild_add_ca_passthrough(0x1FF0, 0x4A75, 0x1FF1, 0x4A75, CA_BASE, CA_VIDEO, es, &n, 8, &dropped);
  ck_assert_int_eq(n, 4);

  slen = pmtbuild_pmt(3, 77, CA_VIDEO, ca_desc, sizeof ca_desc, es, n, extra_entry, sizeof extra_entry, section, sizeof section, &desc_truncated);
  ck_assert_uint_ne(slen, 0u);

  prog_info_len = ((size_t)(section[10] & 0x0F) << 8) | section[11];
  ck_assert_uint_eq(prog_info_len, sizeof ca_desc);
  ck_assert_mem_eq(section + 12, ca_desc, sizeof ca_desc);

  pos = 12 + prog_info_len;
  while (pos + 5 <= slen - 4) {
    size_t info = ((size_t)(section[pos + 3] & 0x0F) << 8) | section[pos + 4];

    ck_assert_int_lt(loop_count, 8);
    loop_pids[loop_count++] = (((unsigned)section[pos + 1] & 0x1F) << 8) | section[pos + 2];
    pos += 5 + info;
  }
  ck_assert_uint_eq(pos, slen - 4);
  ck_assert_int_eq(loop_count, 3);
  ck_assert_uint_eq(loop_pids[0], CA_VIDEO);
  ck_assert_uint_eq(loop_pids[1], CA_BASE);
  ck_assert_uint_eq(loop_pids[2], 0x1F);
  ck_assert_mem_eq(section + slen - 4 - sizeof extra_entry, extra_entry, sizeof extra_entry);
}
END_TEST

static Suite *pmtbuild_suite(void) {
  Suite *s = suite_create("pmtbuild");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, pmtbuild_map_es_picks_video_first_and_drops_unsupported);
  tcase_add_test(tc, pmtbuild_map_es_defaults_pcr_to_first_es_when_no_match);
  tcase_add_test(tc, pmtbuild_map_es_picks_video_when_the_source_declares_no_pcr_pid);
  tcase_add_test(tc, pmtbuild_map_es_picks_the_first_es_for_audio_only_without_a_pcr_pid);
  tcase_add_test(tc, pmtbuild_map_es_reports_dropped_beyond_cap);
  tcase_add_test(tc, pmtbuild_pmt_round_trips_video_audio_subtitle_teletext);
  tcase_add_test(tc, pmtbuild_pmt_truncates_es_descriptors_gracefully);
  tcase_add_test(tc, pmtbuild_pmt_rejects_small_cap);
  tcase_add_test(tc, pmtbuild_pmt_places_prog_desc_in_program_info);
  tcase_add_test(tc, pmtbuild_pmt_round_trips_av1_registration_descriptor);
  tcase_add_loop_test(tc, pmtbuild_add_ca_passthrough_assigns_pids_and_honours_the_cap, 0, (int)(sizeof ca_pass_cases / sizeof ca_pass_cases[0]));
  tcase_add_test(tc, pmtbuild_pmt_keeps_ca_passthrough_out_of_the_es_loop_and_in_program_info);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pmtbuild_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
