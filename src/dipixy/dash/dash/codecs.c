/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipixy/dash/dash.h"
#include "dipixy/dash/dash_int.h"
#include "lib/sys/ioutil.h"

static void hevc_codecs_string(const uint8_t *p, char *out, size_t outsz) {
  static const char *const space_name[4] = {"", "A", "B", "C"};
  unsigned space = p[1] >> 6;
  unsigned tier = (p[1] >> 5) & 1;
  unsigned profile = p[1] & 0x1F;
  uint32_t compat = ((uint32_t)p[2] << 24) | ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5];
  uint32_t rev = 0;
  size_t n;
  int last = -1;
  for (int i = 0; i < 32; i++) rev |= ((compat >> i) & 1u) << (31 - i);
  n = (size_t)snprintf(out, outsz, "hvc1.%s%u.%X.%c%u", space_name[space], profile, (unsigned)rev, tier ? 'H' : 'L', p[12]);
  for (int i = 0; i < 6; i++)
    if (p[6 + i]) last = i;
  for (int i = 0; i <= last && n < outsz; i++) n += (size_t)snprintf(out + n, outsz - n, ".%X", p[6 + i]);
}

/* avc1.PPCCLL from AVCProfileIndication/profile_compatibility/AVCLevelIndication in init seg's avcC box.
   hvc1.1.6.L93.B0 (Main, Level 3.1) when no hvcC found */
static void dash_codecs(const uint8_t *init, size_t initsz, codec_t vcodec, char *out, size_t outsz) {
  if (vcodec == CODEC_HEVC) {
    for (size_t i = 0; i + 4 + 13 <= initsz; i++) {
      if (!memcmp(init + i, "hvcC", 4)) {
        hevc_codecs_string(init + i + 4, out, outsz);
        return;
      }
    }
    bufcpy(out, outsz, "hvc1.1.6.L93.B0");
    return;
  }
  if (vcodec == CODEC_VVC) {
    bufcpy(out, outsz, "vvc1.1.L1.CQ");
    return;
  }
  if (vcodec == CODEC_AV1) {
    for (size_t i = 0; i + 7 <= initsz; i++) {
      if (init[i] == 'a' && init[i + 1] == 'v' && init[i + 2] == '1' && init[i + 3] == 'C') {
        unsigned pl = init[i + 5];
        unsigned th = init[i + 6];
        unsigned profile = (pl >> 5) & 0x07;
        unsigned level = pl & 0x1F;
        unsigned tier = (th >> 7) & 1;
        unsigned hbd = (th >> 6) & 1;
        unsigned twelve = (th >> 5) & 1;
        unsigned bitdepth;
        if (twelve) bitdepth = 12;
        else if (hbd) bitdepth = 10;
        else bitdepth = 8;
        snprintf(out, outsz, "av01.%u.%02u%c.%02u", profile, level, tier ? 'H' : 'M', bitdepth);
        return;
      }
    }
    bufcpy(out, outsz, "av01.0.04M.08");
    return;
  }
  for (size_t i = 0; i + 8 <= initsz; i++) {
    if (init[i] == 'a' && init[i + 1] == 'v' && init[i + 2] == 'c' && init[i + 3] == 'C') {
      sbuf_t b;
      sbuf_init(&b, out, outsz);
      sbuf_add(&b, "avc1.");
      sbuf_add_hex2(&b, init[i + 5]);
      sbuf_add_hex2(&b, init[i + 6]);
      sbuf_add_hex2(&b, init[i + 7]);
      return;
    }
  }
  bufcpy(out, outsz, "avc1.640028");
}

/* if init has no audio track="". mp4a.40.<N>: N from the AAC ASC's top 5 bits, fixed offset into build_esds() layout */
static void dash_audio_codecs(const uint8_t *init, size_t initsz, char *out, size_t outsz) {
  out[0] = '\0';
  for (size_t i = 0; i + 4 <= initsz; i++) {
    if (!memcmp(init + i, "ac-3", 4)) {
      bufcpy(out, outsz, "ac-3");
      return;
    }
    if (!memcmp(init + i, "ec-3", 4)) {
      bufcpy(out, outsz, "ec-3");
      return;
    }
    if (!memcmp(init + i, "Opus", 4)) {
      bufcpy(out, outsz, "opus");
      return;
    }
    if (!memcmp(init + i, "dtsc", 4) || !memcmp(init + i, "dtsh", 4) || !memcmp(init + i, "dtse", 4) || !memcmp(init + i, "dtsl", 4)) {
      char fourcc[5];
      memcpy(fourcc, init + i, 4);
      fourcc[4] = '\0';
      bufcpy(out, outsz, fourcc);
      return;
    }
    if (!memcmp(init + i, "mlpa", 4)) {
      bufcpy(out, outsz, "mlpa");
      return;
    }
    if (!memcmp(init + i, "dac4", 4) && i + 6 <= initsz) {
      sbuf_t b;
      unsigned bs_ver = (unsigned)(((init[i + 4] & 0x1F) << 2) | (init[i + 5] >> 6));
      sbuf_init(&b, out, outsz);
      sbuf_add(&b, "ac-4.");
      sbuf_add_hex2(&b, (unsigned char)bs_ver);
      sbuf_add(&b, ".00.00");
      return;
    }
    if (!memcmp(init + i, "esds", 4) && i + 31 <= initsz) {
      if (init[i + 15] == 0x40) {
        sbuf_t b;
        sbuf_init(&b, out, outsz);
        sbuf_add(&b, "mp4a.40.");
        sbuf_add_u64(&b, (uint64_t)(init[i + 30] >> 3));
      } else bufcpy(out, outsz, "mp4a.6B");
      return;
    }
  }
}

void dash_compute_codecs(const uint8_t *init, size_t initsz, codec_t vcodec, char *vcodec_out, size_t vcodec_outsz, char *acodec_out, size_t acodec_outsz) {
  dash_codecs(init, initsz, vcodec, vcodec_out, vcodec_outsz);
  dash_audio_codecs(init, initsz, acodec_out, acodec_outsz);
}

void mpd_codecs(const hls_snapshot_t *snap, char *codecs, size_t codecs_sz) {
  bufcpy(codecs, codecs_sz, snap->vcodec_str[0] ? snap->vcodec_str : "avc1.640028");
}
