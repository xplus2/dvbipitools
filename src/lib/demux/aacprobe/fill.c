/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#define EXT_DATA_ELEMENT 2
#define EXT_DYNAMIC_RANGE 11
#define EXT_SBR_DATA 13
#define EXT_SBR_DATA_CRC 14
#define ANC_DATA 0

static unsigned excluded_channels(br_t *b) {
  unsigned n = 1;
  br_u(b, 7);
  while (br_u(b, 1) && !b->err) {
    br_u(b, 7);
    n++;
  }
  return n;
}

/* bytes used */
static unsigned dynamic_range_info(br_t *b) {
  unsigned n = 1;
  unsigned bands = 1;
  if (br_u(b, 1)) {
    br_u(b, 8);
    n++;
  }
  if (br_u(b, 1)) n += excluded_channels(b);
  if (br_u(b, 1)) {
    bands += br_u(b, 4);
    br_u(b, 4);
    n++;
    br_skip(b, (size_t)bands * 8);
    n += bands;
  }
  if (br_u(b, 1)) {
    br_u(b, 8);
    n++;
  }
  br_skip(b, (size_t)bands * 8);
  return n + bands;
}

static void sbr_payload(br_t *b, aacprobe_t *p, int crc, int prev_id, size_t end) {
  br_t s = *b;
  int ps = 0;
  p->sbr_seen = 1;
  p->sbr_mono = prev_id == AACPROBE_ID_SCE;
  if (!p->sbr_mono) return;
  s.len = end / 8;
  if (crc) br_u(&s, 10);
  if (br_u(&s, 1) && aacprobe_sbr_header(&s, &p->sbr, p->sbr_rate)) return;
  if (s.err || !p->sbr.have_header || aacprobe_sbr_data(&s, &p->sbr, &ps)) return;
  p->sbr_parsed++;
  if (ps) p->ps_seen = 1;
}

int aacprobe_fill(br_t *b, aacprobe_t *p, int prev_id) {
  unsigned cnt = br_u(b, 4);
  size_t end;
  if (cnt == 15) cnt += br_u(b, 8) - 1;
  end = b->bit + (size_t)cnt * 8;
  if (end > b->len * 8 || b->err) return -1;
  while (cnt > 0) {
    unsigned type = br_u(b, 4);
    unsigned used = cnt;
    if (type == EXT_DYNAMIC_RANGE) {
      used = dynamic_range_info(b);
    } else if (type == EXT_SBR_DATA || type == EXT_SBR_DATA_CRC) {
      sbr_payload(b, p, type == EXT_SBR_DATA_CRC, prev_id, end);
    } else if (type == EXT_DATA_ELEMENT && br_u(b, 4) == ANC_DATA) {
      unsigned part;
      unsigned total = 0;
      unsigned loops = 0;
      do {
        part = br_u(b, 8);
        total += part;
        loops++;
      } while (part == 255 && !b->err);
      br_skip(b, (size_t)total * 8);
      used = total + loops + 1;
    }
    if (b->err || used > cnt) return -1;
    cnt -= used;
  }
  b->bit = end;
  return 0;
}
