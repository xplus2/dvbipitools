/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/mux/psi_build.h"

#include "cadescbuild.h"

size_t cadescbuild_ca_descriptor(unsigned ca_system_id, unsigned ca_pid, unsigned char *out, size_t cap) {
  if (cap < 6)
    return 0;
  out[0] = 0x09;
  out[1] = 4;
  psi_put16(out + 2, ca_system_id);
  psi_put16(out + 4, 0xE000 | (ca_pid & 0x1FFF));
  return 6;
}

size_t cadescbuild_scrambling_descriptor(unsigned char scrambling_mode, unsigned char *out, size_t cap) {
  if (cap < 3)
    return 0;
  out[0] = 0x65;
  out[1] = 1;
  out[2] = scrambling_mode;
  return 3;
}
