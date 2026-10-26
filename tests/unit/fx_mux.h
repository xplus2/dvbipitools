/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_FX_MUX_H
#define DVBIPITOOLS_TESTS_UNIT_FX_MUX_H

#include <check.h>
#include "lib/mux/fmp4/fmp4.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int has_str(const unsigned char *buf, size_t len, const char *s) {
  return memmem(buf, len, s, strlen(s)) != NULL;
}

static inline uint32_t rd32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static inline size_t build_avc_init(unsigned char *out, size_t cap) {
  static const unsigned char avcc[] = {0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x00, 0x01, 0x00, 0x00};
  fmp4_track_cfg_t tcfg;
  fmp4_mux_t *m;
  unsigned char *buf;
  size_t len;

  memset(&tcfg, 0, sizeof tcfg);
  tcfg.codec = CODEC_H264;
  tcfg.track_id = 1;
  tcfg.timescale = 90000;
  tcfg.width = 1280;
  tcfg.height = 720;
  tcfg.cpriv = avcc;
  tcfg.cpriv_len = sizeof avcc;
  m = fmp4_mux_new(&tcfg, 1);
  ck_assert_ptr_nonnull(m);
  len = fmp4_init_segment(m, &buf);
  ck_assert_uint_le(len, cap);
  memcpy(out, buf, len);
  fmp4_mux_free(m);
  return len;
}

static inline int uniform(const uint8_t *buf, size_t len) {
  for (size_t i = 1; i < len; i++) if (buf[i] != buf[0]) return 0;
  return 1;
}

#endif
