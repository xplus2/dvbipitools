/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/psi/dvbtext.h"
#include "lib/demux/psi/priv.h"

#include "bitstream_test.h"

#define DESC_MAX 24

static unsigned char *dup_exact(const unsigned char *src, size_t len) {
  unsigned char *p = malloc(len ? len : 1);

  ck_assert_ptr_nonnull(p);
  if (len) memcpy(p, src, len);
  return p;
}

typedef struct {
  const char *name;
  unsigned char data[DESC_MAX];
  size_t len;
  unsigned tag;
  int found;
  size_t off;
  size_t plen;
} find_case_t;

static const find_case_t find_cases[] = {
    {"empty", {0}, 0, 0x48, 0, 0, 0},
    {"tag byte only", {0x48}, 1, 0x48, 0, 0, 0},
    {"length past end", {0x48, 0x05, 0xAA, 0xBB}, 4, 0x48, 0, 0, 0},
    {"match at start", {0x48, 0x02, 0xAA, 0xBB}, 4, 0x48, 1, 2, 2},
    {"match after another descriptor", {0x01, 0x01, 0xEE, 0x48, 0x00}, 5, 0x48, 1, 5, 0},
    {"truncated descriptor hides later match", {0x01, 0x09, 0xEE, 0x48, 0x00}, 5, 0x48, 0, 0, 0},
    {"tag absent", {0x01, 0x01, 0xEE, 0x02, 0x00}, 5, 0x48, 0, 0, 0},
    {"zero length match", {0x48, 0x00}, 2, 0x48, 1, 2, 0},
};

START_TEST(find_desc_bounds_checks) {
  const find_case_t *c = &find_cases[_i];
  unsigned char *p = dup_exact(c->data, c->len);
  size_t plen = 99;
  const unsigned char *r = find_desc(p, c->len, c->tag, &plen);

  if (c->found) {
    ck_assert_msg(r == p + c->off, "%s: payload offset", c->name);
    ck_assert_msg(plen == c->plen, "%s: payload length %zu", c->name, plen);
  } else {
    ck_assert_msg(r == NULL, "%s: unexpected match", c->name);
  }
  free(p);
}
END_TEST

typedef struct {
  unsigned calls;
  unsigned stop_tag;
} scan_ctx_t;

static const unsigned char *counting_match(unsigned tag, const unsigned char *payload, size_t plen, void *ctx, size_t *out_len) {
  scan_ctx_t *c = ctx;

  c->calls++;
  if (tag != c->stop_tag) return NULL;
  *out_len = plen;
  return payload;
}

START_TEST(desc_scan_never_offers_truncated_descriptor) {
  static const unsigned char chain[] = {0x01, 0x00, 0x02, 0x01, 0xAA, 0x03, 0x09, 0xBB};
  scan_ctx_t ctx = {0, 0xFF};
  size_t dlen = 0;
  unsigned char *p = dup_exact(chain, sizeof chain);

  ck_assert_ptr_null(desc_scan(p, sizeof chain, counting_match, &ctx, &dlen));
  ck_assert_uint_eq(ctx.calls, 2u);
  free(p);
}
END_TEST

START_TEST(desc_scan_stops_at_first_match) {
  static const unsigned char chain[] = {0x01, 0x00, 0x02, 0x01, 0xAA, 0x02, 0x01, 0xBB};
  scan_ctx_t ctx = {0, 0x02};
  size_t dlen = 0;
  unsigned char *p = dup_exact(chain, sizeof chain);
  const unsigned char *r = desc_scan(p, sizeof chain, counting_match, &ctx, &dlen);

  ck_assert_ptr_eq(r, p + 4);
  ck_assert_uint_eq(dlen, 1u);
  ck_assert_uint_eq(ctx.calls, 2u);
  free(p);
}
END_TEST

typedef struct {
  const char *name;
  unsigned dvb;
  unsigned char data[DESC_MAX];
  size_t len;
  unsigned ext_tag;
  int found;
  size_t off;
  size_t plen;
} ext_case_t;

static const ext_case_t ext_cases[] = {
    {"ext: no sub tag byte", 0, {0x3F, 0x00}, 2, 0x18, 0, 0, 0},
    {"ext: sub tag only", 0, {0x3F, 0x01, 0x18}, 3, 0x18, 1, 3, 0},
    {"ext: sub tag with payload", 0, {0x3F, 0x03, 0x18, 0xAA, 0xBB}, 5, 0x18, 1, 3, 2},
    {"ext: other sub tag", 0, {0x3F, 0x02, 0x17, 0xAA}, 4, 0x18, 0, 0, 0},
    {"ext: dvb outer tag ignored", 0, {0x7F, 0x02, 0x18, 0xAA}, 4, 0x18, 0, 0, 0},
    {"ext: second descriptor matches", 0, {0x3F, 0x01, 0x17, 0x3F, 0x02, 0x18, 0xAA}, 7, 0x18, 1, 6, 1},
    {"ext: truncated", 0, {0x3F, 0x05, 0x18, 0xAA}, 4, 0x18, 0, 0, 0},
    {"dvb: no sub tag byte", 1, {0x7F, 0x00}, 2, 0x0E, 0, 0, 0},
    {"dvb: sub tag only", 1, {0x7F, 0x01, 0x0E}, 3, 0x0E, 1, 3, 0},
    {"dvb: sub tag with payload", 1, {0x7F, 0x03, 0x0E, 0xAA, 0xBB}, 5, 0x0E, 1, 3, 2},
    {"dvb: other sub tag", 1, {0x7F, 0x02, 0x15, 0xAA}, 4, 0x0E, 0, 0, 0},
    {"dvb: ext outer tag ignored", 1, {0x3F, 0x02, 0x0E, 0xAA}, 4, 0x0E, 0, 0, 0},
    {"dvb: truncated", 1, {0x7F, 0x05, 0x0E, 0xAA}, 4, 0x0E, 0, 0, 0},
};

START_TEST(extension_descriptor_finders_bounds_checks) {
  const ext_case_t *c = &ext_cases[_i];
  unsigned char *p = dup_exact(c->data, c->len);
  size_t plen = 99;
  const unsigned char *r = c->dvb ? find_dvb_ext_desc(p, c->len, c->ext_tag, &plen) : find_ext_desc(p, c->len, c->ext_tag, &plen);

  if (c->found) {
    ck_assert_msg(r == p + c->off, "%s: payload offset", c->name);
    ck_assert_msg(plen == c->plen, "%s: payload length %zu", c->name, plen);
  } else {
    ck_assert_msg(r == NULL, "%s: unexpected match", c->name);
  }
  free(p);
}
END_TEST

typedef struct {
  unsigned n_assets;
  unsigned construction[3];
  unsigned optional[3];
  int length_override;
} dts_sub_t;

typedef struct {
  const char *name;
  unsigned flags;
  unsigned n_sub;
  dts_sub_t sub[2];
  int expect;
} dts_case_t;

#define DTS_OPT_COMPONENT_TYPE 1u
#define DTS_OPT_LANGUAGE 2u

static void put_dts_substream(stream_t *s, const dts_sub_t *sub) {
  unsigned length = 2;

  for (unsigned a = 0; a < sub->n_assets; a++) {
    length += 3;
    if (sub->optional[a] & DTS_OPT_COMPONENT_TYPE) length += 1;
    if (sub->optional[a] & DTS_OPT_LANGUAGE) length += 3;
  }
  put_bits(s, sub->length_override >= 0 ? (unsigned)sub->length_override : length, 8);
  put_bits(s, sub->n_assets - 1, 3);
  put_bits(s, 2, 5);
  put_bits(s, 0, 1);
  put_bits(s, 0, 4);
  put_bits(s, 0, 1);
  put_bits(s, 0, 2);
  for (unsigned a = 0; a < sub->n_assets; a++) {
    put_bits(s, sub->construction[a], 5);
    put_bits(s, 0, 1);
    put_bits(s, 0, 1);
    put_bits(s, (sub->optional[a] & DTS_OPT_COMPONENT_TYPE) != 0, 1);
    put_bits(s, (sub->optional[a] & DTS_OPT_LANGUAGE) != 0, 1);
    put_bits(s, 0, 13);
    put_bits(s, 0, 2);
    if (sub->optional[a] & DTS_OPT_COMPONENT_TYPE) put_bits(s, 0x55, 8);
    if (sub->optional[a] & DTS_OPT_LANGUAGE) put_bits(s, 0x656E67, 24);
  }
}

static void build_dts_descriptor(stream_t *s, unsigned flags, unsigned n_sub, const dts_sub_t *sub) {
  stream_open(s);
  put_bits(s, flags, 5);
  put_bits(s, 0, 3);
  for (unsigned i = 0; i < n_sub; i++) put_dts_substream(s, &sub[i]);
  stream_read(s);
}

static const dts_case_t dts_cases[] = {
    {"no substream flagged", 0x00, 0, {{0}}, 0},
    {"core substream with master audio", 0x10, 1, {{1, {14}, {0}, -1}}, 1},
    {"optional fields before the master audio asset", 0x10, 1, {{2, {1, 17}, {DTS_OPT_COMPONENT_TYPE | DTS_OPT_LANGUAGE, 0}, -1}}, 1},
    {"master audio only in second substream", 0x18, 2, {{1, {1}, {0}, -1}, {1, {15}, {0}, -1}}, 1},
    {"declared length smaller than content", 0x10, 1, {{1, {1}, {0}, 0}}, 0},
    {"declared length larger than data", 0x10, 1, {{1, {1}, {0}, 200}}, 0},
};

START_TEST(dts_hd_master_audio_scan_table) {
  const dts_case_t *c = &dts_cases[_i];
  stream_t s;
  unsigned char *p;
  int r;

  build_dts_descriptor(&s, c->flags, c->n_sub, c->sub);
  p = dup_exact(s.b.d, s.b.len);
  r = dts_hd_has_ma_asset(p, s.b.len);
  ck_assert_msg(r == c->expect, "%s: got %d, want %d", c->name, r, c->expect);
  free(p);
  stream_close(&s);
}
END_TEST

START_TEST(dts_hd_master_audio_recognizes_exactly_the_xll_constructions) {
  dts_sub_t sub = {1, {0}, {0}, -1};
  stream_t s;
  unsigned char *p;
  int expect = (_i == 14 || _i == 15 || _i == 16 || _i == 17 || _i == 21);

  sub.construction[0] = (unsigned)_i;
  build_dts_descriptor(&s, 0x10, 1, &sub);
  p = dup_exact(s.b.d, s.b.len);
  ck_assert_int_eq(dts_hd_has_ma_asset(p, s.b.len), expect);
  free(p);
  stream_close(&s);
}
END_TEST

START_TEST(dts_hd_master_audio_scan_survives_every_prefix) {
  dts_sub_t sub = {1, {14}, {0}, -1};
  stream_t s;

  build_dts_descriptor(&s, 0x10, 1, &sub);
  for (size_t len = 0; len <= s.b.len; len++) {
    unsigned char *p = dup_exact(s.b.d, len);
    int r = dts_hd_has_ma_asset(p, len);

    ck_assert_int_ge(r, 0);
    ck_assert_int_le(r, 1);
    if (len <= 4) ck_assert_int_eq(r, 0);
    free(p);
  }
  stream_close(&s);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char data[DESC_MAX];
  size_t len;
  unsigned page;
  int type;
  const char *lang;
} ttx_case_t;

static const ttx_case_t ttx_cases[] = {
    {"empty", {0}, 0, 0, 0, ""},
    {"partial entry", {'e', 'n', 'g', 0x09}, 4, 0, 0, ""},
    {"single initial page", {'e', 'n', 'g', 0x09, 0x00}, 5, 100, 1, "eng"},
    {"magazine zero means eight", {'e', 'n', 'g', 0x10, 0x88}, 5, 888, 2, "eng"},
    {"subtitle entry preferred over earlier page", {'e', 'n', 'g', 0x09, 0x00, 'd', 'e', 'u', 0x17, 0x77}, 10, 777, 2, "deu"},
    {"first subtitle entry wins", {'e', 'n', 'g', 0x11, 0x00, 'd', 'e', 'u', 0x29, 0x00}, 10, 100, 2, "eng"},
    {"first initial page kept", {'e', 'n', 'g', 0x09, 0x00, 'd', 'e', 'u', 0x0A, 0x00}, 10, 100, 1, "eng"},
    {"trailing partial entry ignored", {'e', 'n', 'g', 0x09, 0x00, 'd', 'e', 'u'}, 8, 100, 1, "eng"},
};

START_TEST(teletext_descriptor_entries) {
  const ttx_case_t *c = &ttx_cases[_i];
  unsigned char *p = dup_exact(c->data, c->len);
  psi_es_t e;

  memset(&e, 0, sizeof e);
  parse_teletext_desc(&e, p, c->len);
  ck_assert_int_eq(e.cls, PID_TELETEXT);
  ck_assert_msg(e.ttx_page == c->page, "%s: page %u", c->name, e.ttx_page);
  ck_assert_msg(e.ttx_type == c->type, "%s: type %d", c->name, e.ttx_type);
  ck_assert_msg(strcmp(e.ttx_lang, c->lang) == 0, "%s: lang '%s'", c->name, e.ttx_lang);
  free(p);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char data[DESC_MAX];
  size_t len;
  const char *preset_lang;
  unsigned type;
  unsigned composition;
  unsigned ancillary;
  const char *lang;
} sub_case_t;

static const sub_case_t sub_cases[] = {
    {"empty", {0}, 0, "", 0, 0, 0, ""},
    {"seven bytes", {'d', 'e', 'u', 0x10, 0x12, 0x34, 0x56}, 7, "", 0, 0, 0, ""},
    {"full entry", {'d', 'e', 'u', 0x10, 0x12, 0x34, 0x56, 0x78}, 8, "", 0x10, 0x1234, 0x5678, "deu"},
    {"existing language kept", {'d', 'e', 'u', 0x10, 0x12, 0x34, 0x56, 0x78}, 8, "eng", 0x10, 0x1234, 0x5678, "eng"},
};

START_TEST(subtitle_descriptor_entries) {
  const sub_case_t *c = &sub_cases[_i];
  unsigned char *p = dup_exact(c->data, c->len);
  psi_es_t e;

  memset(&e, 0, sizeof e);
  strcpy(e.lang, c->preset_lang);
  parse_subtitle_desc(&e, p, c->len);
  ck_assert_int_eq(e.cls, PID_SUBTITLE);
  ck_assert_msg(e.sub_type == c->type, "%s: type %u", c->name, e.sub_type);
  ck_assert_msg(e.sub_composition_page == c->composition, "%s: composition %u", c->name, e.sub_composition_page);
  ck_assert_msg(e.sub_ancillary_page == c->ancillary, "%s: ancillary %u", c->name, e.sub_ancillary_page);
  ck_assert_msg(strcmp(e.lang, c->lang) == 0, "%s: lang '%s'", c->name, e.lang);
  free(p);
}
END_TEST

typedef struct {
  const char *name;
  unsigned stream_type;
  int hdmv;
  unsigned char desc[DESC_MAX];
  size_t len;
  pid_class_t cls;
  codec_t codec;
  const char *lang;
} classify_case_t;

static const classify_case_t classify_cases[] = {
    {"private without descriptors", 0x06, 0, {0}, 0, PID_DATA, CODEC_NONE, ""},
    {"ac3 descriptor", 0x06, 0, {0x6A, 0x00}, 2, PID_AUDIO, CODEC_AC3, ""},
    {"eac3 descriptor", 0x06, 0, {0x7A, 0x00}, 2, PID_AUDIO, CODEC_EAC3, ""},
    {"dts descriptor", 0x06, 0, {0x7B, 0x00}, 2, PID_AUDIO, CODEC_DTS, ""},
    {"dts-hd extension without assets", 0x06, 0, {0x7F, 0x01, 0x0E}, 3, PID_AUDIO, CODEC_DTS_HD, ""},
    {"ac4 extension", 0x06, 0, {0x7F, 0x01, 0x15}, 3, PID_AUDIO, CODEC_AC4, ""},
    {"registration dts1", 0x06, 0, {0x05, 0x04, 'D', 'T', 'S', '1'}, 6, PID_AUDIO, CODEC_DTS, ""},
    {"registration too short", 0x06, 0, {0x05, 0x03, 'D', 'T', 'S'}, 5, PID_DATA, CODEC_NONE, ""},
    {"registration truncated", 0x06, 0, {0x05, 0x04, 'D', 'T'}, 4, PID_DATA, CODEC_NONE, ""},
    {"registration av1", 0x06, 0, {0x05, 0x04, 'A', 'V', '0', '1'}, 6, PID_VIDEO, CODEC_AV1, ""},
    {"registration opus", 0x06, 0, {0x05, 0x04, 'O', 'p', 'u', 's'}, 6, PID_AUDIO, CODEC_OPUS, ""},
    {"empty teletext descriptor", 0x06, 0, {0x56, 0x00}, 2, PID_TELETEXT, CODEC_NONE, ""},
    {"short subtitle descriptor", 0x06, 0, {0x59, 0x03, 'e', 'n', 'g'}, 5, PID_SUBTITLE, CODEC_NONE, ""},
    {"ait", 0x05, 0, {0x6F, 0x00}, 2, PID_AIT, CODEC_NONE, ""},
    {"hdmv type ignored outside hdmv", 0x82, 0, {0}, 0, PID_DATA, CODEC_NONE, ""},
    {"hdmv dts", 0x82, 1, {0}, 0, PID_AUDIO, CODEC_DTS, ""},
    {"short language descriptor", 0x0F, 0, {0x0A, 0x02, 'e', 'n'}, 4, PID_AUDIO, CODEC_AAC, ""},
    {"language descriptor", 0x0F, 0, {0x0A, 0x04, 'e', 'n', 'g', 0x00}, 6, PID_AUDIO, CODEC_AAC, "eng"},
};

START_TEST(classify_descriptor_table) {
  const classify_case_t *c = &classify_cases[_i];
  unsigned char *p = dup_exact(c->desc, c->len);
  psi_es_t e;

  memset(&e, 0, sizeof e);
  e.stream_type = c->stream_type;
  classify(&e, p, c->len, c->hdmv);
  ck_assert_msg(e.cls == c->cls, "%s: class %d", c->name, (int)e.cls);
  ck_assert_msg(e.codec == c->codec, "%s: codec %d", c->name, (int)e.codec);
  ck_assert_msg(strcmp(e.lang, c->lang) == 0, "%s: lang '%s'", c->name, e.lang);
  free(p);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char data[DESC_MAX];
  size_t len;
  const char *provider;
  const char *service;
} service_case_t;

static const service_case_t service_cases[] = {
    {"descriptor absent", {0x01, 0x00}, 2, "keep", "keep"},
    {"payload shorter than two bytes", {0x48, 0x01, 0x01}, 3, "keep", "keep"},
    {"provider length past end", {0x48, 0x03, 0x01, 0x09, 'A'}, 5, "keep", "keep"},
    {"provider only", {0x48, 0x04, 0x01, 0x02, 'A', 'B'}, 6, "AB", "keep"},
    {"service length past end", {0x48, 0x05, 0x01, 0x01, 'A', 0x05, 'x'}, 7, "A", "keep"},
    {"provider and service", {0x48, 0x06, 0x01, 0x01, 'A', 0x02, 'h', 'i'}, 8, "A", "hi"},
};

START_TEST(service_descriptor_truncation) {
  const service_case_t *c = &service_cases[_i];
  unsigned char *p = dup_exact(c->data, c->len);
  char provider[PSI_NAME];
  char service[PSI_NAME];

  strcpy(provider, "keep");
  strcpy(service, "keep");
  decode_service_desc(p, c->len, provider, service);
  ck_assert_msg(strcmp(provider, c->provider) == 0, "%s: provider '%s'", c->name, provider);
  ck_assert_msg(strcmp(service, c->service) == 0, "%s: service '%s'", c->name, service);
  free(p);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char data[16];
  size_t len;
  size_t cap;
  const char *want;
} text_case_t;

static const text_case_t text_cases[] = {
    {"ascii", {'D', 'a', 's', ' ', 'E'}, 5, 64, "Das E"},
    {"6937 acute", {'c', 'a', 'f', 0xC2, 'e'}, 5, 64, "caf\xC3\xA9"},
    {"6937 diaeresis", {0xC8, 'u'}, 2, 64, "\xC3\xBC"},
    {"6937 sharp s and AE", {0xFB, 0xE1}, 2, 64, "\xC3\x9F\xC3\x86"},
    {"6937 euro", {0xA4}, 1, 64, "\xE2\x82\xAC"},
    {"6937 spacing acute", {0xC2, ' '}, 2, 64, "\xC2\xB4"},
    {"6937 accent without composition", {0xC2, 'x'}, 2, 64, "x"},
    {"6937 unmapped", {0xC0, 'A'}, 2, 64, "?A"},
    {"control to space", {'A', 0x01, 'B'}, 3, 64, "A B"},
    {"emphasis dropped, crlf to space", {0x86, 'A', 0x87, 0x8A, 'B'}, 5, 64, "A B"},
    {"8859-5", {0x01, 0xB0}, 2, 64, "\xD0\x90"},
    {"8859-7 via 0x10", {0x10, 0x00, 0x07, 0xC1}, 4, 64, "\xCE\x91"},
    {"8859-11", {0x07, 0xA1}, 2, 64, "\xE0\xB8\x81"},
    {"8859-12 reserved", {0x08, 'A', 0xB0}, 3, 64, "A?"},
    {"ucs2", {0x11, 0x00, 'A', 0x20, 0xAC}, 5, 64, "A\xE2\x82\xAC"},
    {"ucs2 surrogate pair", {0x11, 0xD8, 0x3D, 0xDE, 0x00}, 5, 64, "\xF0\x9F\x98\x80"},
    {"ucs2 lone surrogate", {0x11, 0xD8, 0x3D, 0x00, 'A'}, 5, 64, "?A"},
    {"utf8 passthrough", {0x15, 'a', 0xC3, 0xA9}, 4, 64, "a\xC3\xA9"},
    {"utf8 truncated sequence", {0x15, 'a', 0xC3}, 3, 64, "a?"},
    {"utf8 overlong", {0x15, 0xC0, 0x80}, 3, 64, "??"},
    {"utf8 c1 control dropped", {0x15, 'a', 0xC2, 0x86, 'b'}, 5, 64, "ab"},
    {"ksx1001", {0x12, 0xB0, 0xA1}, 3, 64, "\xEA\xB0\x80"},
    {"gb2312", {0x13, 0xB0, 0xA1, 'A'}, 4, 64, "\xE5\x95\x8A" "A"},
    {"big5", {0x14, 0xA4, 0x40}, 3, 64, "\xE4\xB8\x80"},
    {"dbcs bad trail resyncs", {0x13, 0xB0, 'A'}, 3, 64, "?A"},
    {"encoding_type_id ascii only", {0x1F, 0x01, 'A', 0xE9}, 4, 64, "A?"},
    {"cut on code point boundary", {'a', 'b', 'c', 0xC2, 'e'}, 5, 5, "abc"},
    {"fits exactly", {'a', 'b', 'c', 0xC2, 'e'}, 5, 7, "abc\xC3\xA9"},
};

START_TEST(dvbtext_conversion_table) {
  const text_case_t *c = &text_cases[_i];
  char out[64];
  size_t n;

  memset(out, 'x', sizeof out);
  n = dvbtext_to_utf8(out, c->cap, c->data, c->len);
  ck_assert_msg(strcmp(out, c->want) == 0, "%s: got '%s'", c->name, out);
  ck_assert_msg(n == strlen(c->want), "%s: length %zu", c->name, n);
}
END_TEST

START_TEST(dvbtext_zero_cap_writes_nothing) {
  char out[4] = {'x', 'x', 'x', 'x'};

  ck_assert_uint_eq(dvbtext_to_utf8(out, 0, (const unsigned char *)"A", 1), 0);
  ck_assert_int_eq(out[0], 'x');
}
END_TEST

START_TEST(dvbtext_never_overruns_or_emits_invalid_utf8) {
  unsigned char in[24];
  char out[12];
  unsigned seed = 12345u + (unsigned)_i;

  for (int round = 0; round < 400; round++) {
    size_t len = 1 + (seed >> 8) % sizeof in;
    size_t n;

    for (size_t k = 0; k < len; k++) {
      seed = seed * 1103515245u + 12345u;
      in[k] = (unsigned char)(seed >> 16);
    }
    if (_i < 8) in[0] = (unsigned char)(_i + 0x11);
    n = dvbtext_to_utf8(out, sizeof out, in, len);
    ck_assert_uint_lt(n, sizeof out);
    ck_assert_uint_eq(strlen(out), n);
    for (size_t k = 0; k < n;) {
      unsigned char b = (unsigned char)out[k];
      size_t seq = 0;
      if (b < 0x80) seq = 1;
      else if ((b & 0xE0) == 0xC0) seq = 2;
      else if ((b & 0xF0) == 0xE0) seq = 3;
      else if ((b & 0xF8) == 0xF0) seq = 4;

      ck_assert_uint_ne(seq, 0);
      ck_assert_uint_le(k + seq, n);
      for (size_t m = 1; m < seq; m++) ck_assert_int_eq((unsigned char)out[k + m] & 0xC0, 0x80);
      k += seq;
    }
  }
}
END_TEST

static Suite *descriptors_suite(void) {
  Suite *s = suite_create("psi_descriptors");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, find_desc_bounds_checks, 0, (int)(sizeof find_cases / sizeof find_cases[0]));
  tcase_add_test(tc, desc_scan_never_offers_truncated_descriptor);
  tcase_add_test(tc, desc_scan_stops_at_first_match);
  tcase_add_loop_test(tc, extension_descriptor_finders_bounds_checks, 0, (int)(sizeof ext_cases / sizeof ext_cases[0]));
  tcase_add_loop_test(tc, dts_hd_master_audio_scan_table, 0, (int)(sizeof dts_cases / sizeof dts_cases[0]));
  tcase_add_loop_test(tc, dts_hd_master_audio_recognizes_exactly_the_xll_constructions, 0, 32);
  tcase_add_test(tc, dts_hd_master_audio_scan_survives_every_prefix);
  tcase_add_loop_test(tc, teletext_descriptor_entries, 0, (int)(sizeof ttx_cases / sizeof ttx_cases[0]));
  tcase_add_loop_test(tc, subtitle_descriptor_entries, 0, (int)(sizeof sub_cases / sizeof sub_cases[0]));
  tcase_add_loop_test(tc, classify_descriptor_table, 0, (int)(sizeof classify_cases / sizeof classify_cases[0]));
  tcase_add_loop_test(tc, service_descriptor_truncation, 0, (int)(sizeof service_cases / sizeof service_cases[0]));
  tcase_add_loop_test(tc, dvbtext_conversion_table, 0, (int)(sizeof text_cases / sizeof text_cases[0]));
  tcase_add_test(tc, dvbtext_zero_cap_writes_nothing);
  tcase_add_loop_test(tc, dvbtext_never_overruns_or_emits_invalid_utf8, 0, 16);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(descriptors_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
