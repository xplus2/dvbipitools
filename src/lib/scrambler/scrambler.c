/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "cissa.h"
#include "csa2.h"

#include "scrambler.h"

struct scrambler {
  scramble_algo_t algo;
  unsigned char cw[2][CISSA_CW_LEN]; /* CISSA only, CSA2 keeps state in csa2_key */
  csa2_key_t *csa2_key[2];
  int have_key[2];
};

scrambler_t *scrambler_new(scramble_algo_t algo) {
  scrambler_t *s = calloc(1, sizeof *s);
  if (s)
    s->algo = algo;
  return s;
}

void scrambler_free(scrambler_t *s) {
  if (!s)
    return;
  csa2_key_free(s->csa2_key[SCRAMBLE_PARITY_EVEN]);
  csa2_key_free(s->csa2_key[SCRAMBLE_PARITY_ODD]);
  free(s);
}

size_t scrambler_cw_len(scramble_algo_t algo) {
  return algo == SCRAMBLE_ALGO_CISSA ? (size_t)CISSA_CW_LEN : (size_t)CSA2_CW_LEN;
}

int scrambler_set_key(scrambler_t *s, int parity, const unsigned char *cw, size_t cw_len) {
  if (!s || (parity != SCRAMBLE_PARITY_EVEN && parity != SCRAMBLE_PARITY_ODD))
    return -1;
  if (cw_len != scrambler_cw_len(s->algo))
    return -1;

  if (s->algo == SCRAMBLE_ALGO_CISSA) {
    memcpy(s->cw[parity], cw, cw_len);
  } else {
    csa2_key_t *k = csa2_key_new(cw);
    if (!k)
      return -1;
    csa2_key_free(s->csa2_key[parity]);
    s->csa2_key[parity] = k;
  }
  s->have_key[parity] = 1;
  return 0;
}

/* TS header adaptation_field_control (byte 3 bits 4-5): 01 payload only,
   10 AF only (no payload), 11 AF+payload. AF, if present, is 1 length byte plus that many bytes. */
static size_t ts_payload_offset(const unsigned char *pkt) {
  unsigned afc = (pkt[3] >> 4) & 0x3;
  if (afc == 2 || afc == 3)
    return 4 + 1 + pkt[4];
  return 4;
}

int scrambler_encrypt_packet(scrambler_t *s, unsigned char pkt[188], int parity) {
  size_t payload_off, payload_size, enc_size, block;

  if (!s || (parity != SCRAMBLE_PARITY_EVEN && parity != SCRAMBLE_PARITY_ODD) || !s->have_key[parity])
    return -1;

  payload_off = ts_payload_offset(pkt);
  if (payload_off >= 188)
    return 0; /* AF fills the packet, nothing to scramble, leave control bits at 00 */
  payload_size = 188 - payload_off;

  /* only the aligned part gets encrypted, any trailing residual bytes stay clear.
     (CISSA: TS 103 127 clause 6.3.2, 16-byte blocks; CSA2: classic DVB-CSA convention, 8-byte blocks) */
  block = s->algo == SCRAMBLE_ALGO_CISSA ? 16u : 8u;
  enc_size = payload_size - (payload_size % block);
  if (enc_size == 0)
    return 0; /* payload smaller than one cipher block: nothing to scramble, leave control bits at 00 */

  pkt[3] = (unsigned char)((pkt[3] & 0x3F) | (parity == SCRAMBLE_PARITY_ODD ? 0xC0 : 0x80));

  if (s->algo == SCRAMBLE_ALGO_CISSA)
    return cissa_encrypt_block(s->cw[parity], pkt + payload_off, enc_size);

  csa2_encrypt_block(s->csa2_key[parity], pkt + payload_off, enc_size);
  return 0;
}
