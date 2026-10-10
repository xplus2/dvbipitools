/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/hls/playlist.h"

static http_url_t make_base(const char *host, unsigned port, const char *path) {
  http_url_t u;
  memset(&u, 0, sizeof u);
  bufcpy(u.host, sizeof u.host, host);
  u.port = port;
  bufcpy(u.path, sizeof u.path, path);
  return u;
}

START_TEST(hls_playlist_parse_realistic_live_manifest) {
  char body[] =
    "#EXTM3U\n#EXT-X-VERSION:3\n## Created with a test packager\n"
    "#EXT-X-MEDIA-SEQUENCE:279611805\n#EXT-X-INDEPENDENT-SEGMENTS\n#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-PROGRAM-DATE-TIME:2026-09-15T23:39:05.600000Z\n"
    "#EXTINF:6.4, no desc\nstation-audio=96000-279611805.ts\n"
    "#EXTINF:6.4, no desc\nstation-audio=96000-279611806.ts\n";
  http_url_t base = make_base("live.example.com", 80, "/live/station-audio=96000.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_uint_eq(pl.target_duration, 6);
  ck_assert_uint_eq((unsigned)pl.media_sequence, 279611805u);
  ck_assert_int_eq(pl.endlist, 0);
  ck_assert_uint_eq(pl.n_segments, 2);
  ck_assert_str_eq(pl.segments[0].url, "http://live.example.com/live/station-audio=96000-279611805.ts");
  ck_assert_str_eq(pl.segments[1].url, "http://live.example.com/live/station-audio=96000-279611806.ts");
}
END_TEST

START_TEST(hls_playlist_parse_absolute_segment_url) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nhttp://cdn.example.com/seg1.ts\n";
  http_url_t base = make_base("cdn.example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_uint_eq(pl.n_segments, 1);
  ck_assert_str_eq(pl.segments[0].url, "http://cdn.example.com/seg1.ts");
}
END_TEST

START_TEST(hls_playlist_parse_endlist_flag) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nseg1.ts\n#EXT-X-ENDLIST\n";
  http_url_t base = make_base("example.com", 80, "/vod/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.endlist, 1);
  ck_assert_uint_eq(pl.n_segments, 1);
}
END_TEST

START_TEST(hls_playlist_parse_rejects_non_extm3u_body) {
  char body[] = "not a playlist at all\n";
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, NULL, &pl), 0);
}
END_TEST

START_TEST(hls_playlist_parse_ignores_uri_without_extinf) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\nseg1.ts\n#EXTINF:4.0,\nseg2.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_uint_eq(pl.n_segments, 1);
  ck_assert_str_eq(pl.segments[0].url, "http://example.com/live/seg2.ts");
}
END_TEST

START_TEST(hls_playlist_parse_map_uri_marks_fmp4) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:4.0,\nseg1.m4s\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_str_eq(pl.map_uri, "http://example.com/live/init.mp4");
}
END_TEST

START_TEST(hls_playlist_parse_no_map_uri_means_ts) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nseg1.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_str_eq(pl.map_uri, "");
}
END_TEST

START_TEST(hls_playlist_parse_detects_low_latency_part) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-PART:DURATION=1.0,URI=\"p1.ts\"\n#EXTINF:4.0,\nseg1.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.low_latency, 1);
}
END_TEST

START_TEST(hls_playlist_parse_detects_low_latency_preload_hint) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"p2.ts\"\n#EXTINF:4.0,\nseg1.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.low_latency, 1);
}
END_TEST

START_TEST(hls_playlist_parse_detects_low_latency_block_reload) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-SERVER-CONTROL:CAN-BLOCK-RELOAD=YES\n#EXTINF:4.0,\nseg1.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.low_latency, 1);
}
END_TEST

START_TEST(hls_playlist_parse_regular_hls_is_not_low_latency) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nseg1.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.low_latency, 0);
}
END_TEST

START_TEST(hls_body_is_master_detects_stream_inf) {
  char master[] = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000000\nlow.m3u8\n";
  char media[] = "#EXTM3U\n#EXTINF:4.0,\nseg1.ts\n";
  ck_assert_int_eq(hls_body_is_master(master), 1);
  ck_assert_int_eq(hls_body_is_master(media), 0);
}
END_TEST

START_TEST(hls_master_parse_picks_highest_bandwidth) {
  char body[] =
    "#EXTM3U\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360\nlow.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=3000000,RESOLUTION=1920x1080\nhigh.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=1500000,RESOLUTION=1280x720\nmid.m3u8\n";
  http_url_t base = make_base("example.com", 80, "/live/master.m3u8");
  hls_master_t m;
  int best;

  ck_assert_int_eq(hls_master_parse(body, &base, &m), 1);
  ck_assert_uint_eq(m.n_variants, 3);
  best = hls_master_pick_highest(&m);
  ck_assert_int_ge(best, 0);
  ck_assert_uint_eq(m.variants[(unsigned)best].bandwidth, 3000000u);
  ck_assert_str_eq(m.variants[(unsigned)best].url, "http://example.com/live/high.m3u8");
}
END_TEST

START_TEST(hls_master_parse_resolution_breaks_bandwidth_tie) {
  char body[] =
    "#EXTM3U\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1280x720\na.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1920x1080\nb.m3u8\n";
  http_url_t base = make_base("example.com", 80, "/live/master.m3u8");
  hls_master_t m;
  int best;

  ck_assert_int_eq(hls_master_parse(body, &base, &m), 1);
  best = hls_master_pick_highest(&m);
  ck_assert_str_eq(m.variants[(unsigned)best].url, "http://example.com/live/b.m3u8");
}
END_TEST

START_TEST(hls_master_pick_highest_empty_returns_negative) {
  hls_master_t m;
  memset(&m, 0, sizeof m);
  ck_assert_int_eq(hls_master_pick_highest(&m), -1);
}
END_TEST

START_TEST(hls_master_parse_links_variant_to_audio_group) {
  char body[] =
    "#EXTM3U\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aac\",NAME=\"English\",URI=\"audio.m3u8\"\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=3000000,AUDIO=\"aac\"\nvideo.m3u8\n";
  http_url_t base = make_base("example.com", 80, "/live/master.m3u8");
  hls_master_t m;
  const hls_audio_rendition_t *ar;

  ck_assert_int_eq(hls_master_parse(body, &base, &m), 1);
  ck_assert_uint_eq(m.n_variants, 1);
  ck_assert_uint_eq(m.n_audio_renditions, 1);
  ck_assert_str_eq(m.variants[0].audio_group_id, "aac");
  ar = hls_master_find_audio(&m, m.variants[0].audio_group_id);
  ck_assert_ptr_nonnull(ar);
  ck_assert_str_eq(ar->url, "http://example.com/live/audio.m3u8");
}
END_TEST

START_TEST(hls_master_parse_no_audio_attr_means_self_contained) {
  char body[] = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=3000000\nvideo.m3u8\n";
  http_url_t base = make_base("example.com", 80, "/live/master.m3u8");
  hls_master_t m;

  ck_assert_int_eq(hls_master_parse(body, &base, &m), 1);
  ck_assert_str_eq(m.variants[0].audio_group_id, "");
  ck_assert_ptr_null(hls_master_find_audio(&m, m.variants[0].audio_group_id));
}
END_TEST

START_TEST(hls_playlist_parse_key_aes128_tags_segments) {
  char body[] =
    "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nclear.ts\n"
    "#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\",IV=0x000102030405060708090A0B0C0D0E0F\n"
    "#EXTINF:4.0,\nenc1.ts\n#EXTINF:4.0,\nenc2.ts\n#EXT-X-KEY:METHOD=NONE\n#EXTINF:4.0,\nclear2.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_uint_eq(pl.n_segments, 4);
  ck_assert_uint_eq(pl.n_keys, 1);
  ck_assert_str_eq(pl.keys[0].url, "http://example.com/live/key.bin");
  ck_assert_int_eq(pl.keys[0].has_iv, 1);
  ck_assert_uint_eq(pl.keys[0].iv[0], 0x00);
  ck_assert_uint_eq(pl.keys[0].iv[15], 0x0f);
  ck_assert_uint_eq(pl.segments[0].key, 0);
  ck_assert_uint_eq(pl.segments[1].key, 1);
  ck_assert_uint_eq(pl.segments[2].key, 1);
  ck_assert_uint_eq(pl.segments[3].key, 0);
}
END_TEST

START_TEST(hls_playlist_parse_key_without_iv) {
  char body[] = "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\"\n#EXTINF:4.0,\ns.ts\n";
  http_url_t base = make_base("example.com", 80, "/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.keys[0].has_iv, 0);
}
END_TEST

START_TEST(hls_playlist_parse_rejects_sample_aes) {
  char body[] = "#EXTM3U\n#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"k\"\n#EXTINF:4.0,\ns.ts\n";
  http_url_t base = make_base("example.com", 80, "/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 0);
}
END_TEST

START_TEST(hls_playlist_parse_rejects_foreign_keyformat) {
  char body[] = "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\",KEYFORMAT=\"com.example.drm\"\n#EXTINF:4.0,\ns.ts\n";
  http_url_t base = make_base("example.com", 80, "/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 0);
}
END_TEST

START_TEST(hls_playlist_parse_rejects_encrypted_map) {
  char body[] = "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\"\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:4.0,\ns.m4s\n";
  http_url_t base = make_base("example.com", 80, "/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 0);
}
END_TEST

START_TEST(hls_playlist_parse_protocol_relative_segment) {
  static const char src[] = "#EXTM3U\n#EXTINF:4.0,\n//cdn.example.com/a/seg1.ts\n";
  char body[sizeof src];
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  memcpy(body, src, sizeof src);
  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_str_eq(pl.segments[0].url, "http://cdn.example.com/a/seg1.ts");
  memcpy(body, src, sizeof src);
  base.tls = 1;
  base.port = 443;
  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_str_eq(pl.segments[0].url, "https://cdn.example.com/a/seg1.ts");
}
END_TEST

typedef struct {
  const char *name;
  const char *body;
} reject_case_t;

static const reject_case_t reject_cases[] = {
  {"key without method", "#EXTM3U\n#EXT-X-KEY:URI=\"k\"\n#EXTINF:4.0,\ns.ts\n"},
  {"key without uri", "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128\n#EXTINF:4.0,\ns.ts\n"},
  {"key with short iv", "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\",IV=0x0102\n#EXTINF:4.0,\ns.ts\n"},
  {"key with non-hex iv", "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\",IV=0xZZ0102030405060708090A0B0C0D0E0F\n#EXTINF:4.0,\ns.ts\n"},
  {"key with iv lacking prefix", "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\",IV=000102030405060708090A0B0C0D0E0F00\n#EXTINF:4.0,\ns.ts\n"},
  {"key with trailing iv bytes", "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\",IV=0x000102030405060708090A0B0C0D0E0F00\n#EXTINF:4.0,\ns.ts\n"},
  {"key uri with unterminated quote", "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\n#EXTINF:4.0,\ns.ts\n"},
  {"too many distinct keys",
   "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k1\"\n#EXTINF:4.0,\na.ts\n#EXT-X-KEY:METHOD=AES-128,URI=\"k2\"\n#EXTINF:4.0,\nb.ts\n"
   "#EXT-X-KEY:METHOD=AES-128,URI=\"k3\"\n#EXTINF:4.0,\nc.ts\n#EXT-X-KEY:METHOD=AES-128,URI=\"k4\"\n#EXTINF:4.0,\nd.ts\n"
   "#EXT-X-KEY:METHOD=AES-128,URI=\"k5\"\n#EXTINF:4.0,\ne.ts\n"},
};

START_TEST(hls_playlist_parse_rejects_malformed_or_unsupported_keys) {
  const reject_case_t *c = &reject_cases[_i];
  char body[1024];
  http_url_t base = make_base("example.com", 80, "/index.m3u8");
  hls_playlist_t pl;

  ck_assert_uint_lt(strlen(c->body), sizeof body);
  strcpy(body, c->body);
  ck_assert_msg(hls_playlist_parse(body, &base, &pl) == 0, "%s: accepted", c->name);
}
END_TEST

START_TEST(hls_playlist_parse_reuses_a_key_seen_again) {
  char body[] =
    "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k1\"\n#EXTINF:4.0,\na.ts\n"
    "#EXT-X-KEY:METHOD=AES-128,URI=\"k2\"\n#EXTINF:4.0,\nb.ts\n"
    "#EXT-X-KEY:METHOD=AES-128,URI=\"k1\"\n#EXTINF:4.0,\nc.ts\n";
  http_url_t base = make_base("example.com", 80, "/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_uint_eq(pl.n_keys, 2);
  ck_assert_uint_eq(pl.segments[0].key, 1);
  ck_assert_uint_eq(pl.segments[1].key, 2);
  ck_assert_uint_eq(pl.segments[2].key, 1);
}
END_TEST

START_TEST(hls_playlist_parse_accepts_identity_keyformat_and_lowercase_iv) {
  char body[] = "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\",KEYFORMAT=\"identity\",IV=0xaabbccddeeff00112233445566778899\n#EXTINF:4.0,\ns.ts\n";
  http_url_t base = make_base("example.com", 80, "/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.keys[0].has_iv, 1);
  ck_assert_uint_eq(pl.keys[0].iv[0], 0xAA);
  ck_assert_uint_eq(pl.keys[0].iv[15], 0x99);
}
END_TEST

START_TEST(hls_playlist_parse_caps_segments_and_skips_unresolvable_uris) {
  char body[HLS_MAX_SEGMENTS * 40 + 256];
  size_t n = 0;
  http_url_t base = make_base("example.com", 80, "/index.m3u8");
  hls_playlist_t pl;

  n += (size_t)snprintf(body + n, sizeof body - n, "#EXTM3U\n#EXT-X-TARGETDURATION:4\n");
  for (unsigned i = 0; i < HLS_MAX_SEGMENTS + 5; i++) n += (size_t)snprintf(body + n, sizeof body - n, "#EXTINF:4.0,\ns%u.ts\n", i);
  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_uint_eq(pl.n_segments, HLS_MAX_SEGMENTS);
  ck_assert_str_eq(pl.segments[HLS_MAX_SEGMENTS - 1].url, "http://example.com/s63.ts");
}
END_TEST

START_TEST(hls_playlist_parse_server_control_without_block_reload_is_not_low_latency) {
  char body[] = "#EXTM3U\n#EXT-X-SERVER-CONTROL:CAN-BLOCK-RELOAD=NO,HOLD-BACK=9\n#EXTINF:4.0,\ns.ts\n";
  http_url_t base = make_base("example.com", 80, "/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.low_latency, 0);
}
END_TEST

START_TEST(hls_playlist_parse_map_without_uri_leaves_ts_mode) {
  char body[] = "#EXTM3U\n#EXT-X-MAP:BYTERANGE=\"10@0\"\n#EXTINF:4.0,\ns.ts\n";
  http_url_t base = make_base("example.com", 80, "/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_str_eq(pl.map_uri, "");
}
END_TEST

START_TEST(hls_body_is_master_requires_tag_at_line_start) {
  ck_assert_int_eq(hls_body_is_master("#EXTM3U\n#EXTINF:4.0,\n# see #EXT-X-STREAM-INF later\ns.ts\n"), 0);
  ck_assert_int_eq(hls_body_is_master("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nv.m3u8\n"), 1);
  ck_assert_int_eq(hls_body_is_master("#EXT-X-STREAM-INF:BANDWIDTH=1\nv.m3u8\n"), 1);
}
END_TEST

START_TEST(hls_master_parse_skips_non_audio_media_and_incomplete_entries) {
  char body[] =
    "#EXTM3U\n"
    "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",URI=\"s.m3u8\"\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\"\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,URI=\"x.m3u8\"\n"
    "#EXT-X-MEDIA:TYPE=audio,GROUP-ID=\"aud\",URI=\"a.m3u8\"\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=500000,RESOLUTION=640\n"
    "v0.m3u8\n"
    "#EXT-X-STREAM-INF:RESOLUTION=1280x720,AUDIO=\"aud\"\n"
    "v1.m3u8\n"
    "stray.m3u8\n";
  http_url_t base = make_base("example.com", 80, "/master.m3u8");
  hls_master_t m;
  const hls_audio_rendition_t *ar;

  ck_assert_int_eq(hls_master_parse(body, &base, &m), 1);
  ck_assert_uint_eq(m.n_audio_renditions, 1);
  ck_assert_uint_eq(m.n_variants, 2);
  ck_assert_uint_eq(m.variants[0].bandwidth, 500000u);
  ck_assert_uint_eq(m.variants[0].width, 0u);
  ck_assert_uint_eq(m.variants[1].bandwidth, 0u);
  ck_assert_uint_eq(m.variants[1].width, 1280u);
  ck_assert_uint_eq(m.variants[1].height, 720u);
  ck_assert_str_eq(m.variants[1].audio_group_id, "aud");
  ar = hls_master_find_audio(&m, "aud");
  ck_assert_ptr_nonnull(ar);
  ck_assert_str_eq(ar->url, "http://example.com/a.m3u8");
  ck_assert_ptr_null(hls_master_find_audio(&m, "missing"));
  ck_assert_ptr_null(hls_master_find_audio(&m, ""));
  ck_assert_ptr_null(hls_master_find_audio(&m, NULL));
}
END_TEST

START_TEST(hls_master_parse_rejects_missing_header_and_empty_master) {
  char no_header[] = "#EXT-X-STREAM-INF:BANDWIDTH=1\nv.m3u8\n";
  char empty[] = "#EXTM3U\n#EXT-X-VERSION:3\n";
  http_url_t base = make_base("example.com", 80, "/master.m3u8");
  hls_master_t m;

  ck_assert_int_eq(hls_master_parse(no_header, &base, &m), 0);
  ck_assert_int_eq(hls_master_parse(empty, &base, &m), 0);
}
END_TEST

START_TEST(hls_master_parse_caps_variants_and_renditions) {
  char body[HLS_MAX_VARIANTS * 160 + 256];
  size_t n = 0;
  http_url_t base = make_base("example.com", 80, "/master.m3u8");
  hls_master_t m;

  n += (size_t)snprintf(body + n, sizeof body - n, "#EXTM3U\n");
  for (unsigned i = 0; i < HLS_MAX_VARIANTS + 4; i++) {
    n += (size_t)snprintf(body + n, sizeof body - n, "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"g%u\",URI=\"a%u.m3u8\"\n", i, i);
    n += (size_t)snprintf(body + n, sizeof body - n, "#EXT-X-STREAM-INF:BANDWIDTH=%u\nv%u.m3u8\n", 1000 + i, i);
  }
  ck_assert_int_eq(hls_master_parse(body, &base, &m), 1);
  ck_assert_uint_eq(m.n_variants, HLS_MAX_VARIANTS);
  ck_assert_uint_eq(m.n_audio_renditions, HLS_MAX_VARIANTS);
  ck_assert_int_eq(hls_master_pick_highest(&m), HLS_MAX_VARIANTS - 1);
}
END_TEST

static Suite *hls_playlist_suite(void) {
  Suite *s = suite_create("hls_playlist");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, hls_playlist_parse_realistic_live_manifest);
  tcase_add_test(tc, hls_playlist_parse_absolute_segment_url);
  tcase_add_test(tc, hls_playlist_parse_endlist_flag);
  tcase_add_test(tc, hls_playlist_parse_rejects_non_extm3u_body);
  tcase_add_test(tc, hls_playlist_parse_ignores_uri_without_extinf);
  tcase_add_test(tc, hls_playlist_parse_map_uri_marks_fmp4);
  tcase_add_test(tc, hls_playlist_parse_no_map_uri_means_ts);
  tcase_add_test(tc, hls_playlist_parse_detects_low_latency_part);
  tcase_add_test(tc, hls_playlist_parse_detects_low_latency_preload_hint);
  tcase_add_test(tc, hls_playlist_parse_detects_low_latency_block_reload);
  tcase_add_test(tc, hls_playlist_parse_regular_hls_is_not_low_latency);
  tcase_add_test(tc, hls_body_is_master_detects_stream_inf);
  tcase_add_test(tc, hls_master_parse_picks_highest_bandwidth);
  tcase_add_test(tc, hls_master_parse_resolution_breaks_bandwidth_tie);
  tcase_add_test(tc, hls_master_pick_highest_empty_returns_negative);
  tcase_add_test(tc, hls_master_parse_links_variant_to_audio_group);
  tcase_add_test(tc, hls_master_parse_no_audio_attr_means_self_contained);
  tcase_add_test(tc, hls_playlist_parse_key_aes128_tags_segments);
  tcase_add_test(tc, hls_playlist_parse_key_without_iv);
  tcase_add_test(tc, hls_playlist_parse_rejects_sample_aes);
  tcase_add_test(tc, hls_playlist_parse_rejects_foreign_keyformat);
  tcase_add_test(tc, hls_playlist_parse_rejects_encrypted_map);
  tcase_add_test(tc, hls_playlist_parse_protocol_relative_segment);
  tcase_add_loop_test(tc, hls_playlist_parse_rejects_malformed_or_unsupported_keys, 0, (int)(sizeof reject_cases / sizeof reject_cases[0]));
  tcase_add_test(tc, hls_playlist_parse_reuses_a_key_seen_again);
  tcase_add_test(tc, hls_playlist_parse_accepts_identity_keyformat_and_lowercase_iv);
  tcase_add_test(tc, hls_playlist_parse_caps_segments_and_skips_unresolvable_uris);
  tcase_add_test(tc, hls_playlist_parse_server_control_without_block_reload_is_not_low_latency);
  tcase_add_test(tc, hls_playlist_parse_map_without_uri_leaves_ts_mode);
  tcase_add_test(tc, hls_body_is_master_requires_tag_at_line_start);
  tcase_add_test(tc, hls_master_parse_skips_non_audio_media_and_incomplete_entries);
  tcase_add_test(tc, hls_master_parse_rejects_missing_header_and_empty_master);
  tcase_add_test(tc, hls_master_parse_caps_variants_and_renditions);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(hls_playlist_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
