/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "pmt_filter.h"
#include "psi_build.h"

static int pid_in_set(unsigned pid, const unsigned *pids, size_t n) {
  size_t i;
  for (i = 0; i < n; i++)
    if (pids[i] == pid)
      return 1;
  return 0;
}

size_t pmt_filter_rewrite(const unsigned char *pmt_section, size_t seclen, const unsigned *drop_pids, size_t n_drop, unsigned char *out, size_t out_cap) {
  size_t i, o, end, pil;
  if (seclen < 16 || pmt_section[0] != 0x02 || out_cap < 12) return 0;
  memcpy(out, pmt_section, 12);
  pil = (((size_t)pmt_section[10] & 0x0F) << 8) | pmt_section[11];
  end = seclen - 4;
  if (12 + pil > end || out_cap < 12 + pil) return 0;
  memcpy(out + 12, pmt_section + 12, pil);
  o = 12 + pil;
  i = 12 + pil;
  while (i + 5 <= end) {
    unsigned pid = (((unsigned)pmt_section[i + 1] & 0x1F) << 8) | pmt_section[i + 2];
    size_t esil = (((size_t)pmt_section[i + 3] & 0x0F) << 8) | pmt_section[i + 4];
    if (i + 5 + esil > end) break;
    if (!pid_in_set(pid, drop_pids, n_drop)) {
      if (o + 5 + esil > out_cap) return 0;
      memcpy(out + o, pmt_section + i, 5 + esil);
      o += 5 + esil;
    }
    i += 5 + esil;
  }
  return psi_finish_section(out, o, out_cap, 0xB0);
}

int pmt_filter_emit_packet(unsigned char *out188, unsigned pid, unsigned char cc, const unsigned char *sec, size_t seclen) {
  if (seclen + 1 > 184) return 0;
  memset(out188, 0xFF, 188);
  out188[0] = 0x47;
  out188[1] = 0x40 | (unsigned char)((pid >> 8) & 0x1F);
  out188[2] = (unsigned char)(pid & 0xFF);
  out188[3] = 0x10 | (cc & 0x0F);
  out188[4] = 0x00;
  memcpy(out188 + 5, sec, seclen);
  return 1;
}
