/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/mux/psi_build.h"

#include "cadescbuild.h"

size_t cadescbuild_ca_descriptor(unsigned ca_system_id, unsigned ca_pid, unsigned char *out, size_t cap) {
  return cadescbuild_ca_descriptor_priv(ca_system_id, ca_pid, NULL, 0, out, cap);
}

size_t cadescbuild_ca_descriptor_priv(unsigned ca_system_id, unsigned ca_pid, const unsigned char *priv, size_t priv_len, unsigned char *out, size_t cap) {
  if (priv_len > 255 - 4 || cap < 6 + priv_len)
    return 0;
  out[0] = 0x09;
  out[1] = (unsigned char)(4 + priv_len);
  psi_put16(out + 2, ca_system_id);
  psi_put16(out + 4, 0xE000 | (ca_pid & 0x1FFF));
  if (priv_len)
    memcpy(out + 6, priv, priv_len);
  return 6 + priv_len;
}

size_t cadescbuild_scrambling_descriptor(unsigned char scrambling_mode, unsigned char *out, size_t cap) {
  if (cap < 3)
    return 0;
  out[0] = 0x65;
  out[1] = 1;
  out[2] = scrambling_mode;
  return 3;
}
