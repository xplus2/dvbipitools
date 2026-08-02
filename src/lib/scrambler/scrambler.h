/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_SCRAMBLER_SCRAMBLER_H
#define DVBIPITOOLS_LIB_SCRAMBLER_SCRAMBLER_H

#include <stddef.h>

typedef struct scrambler scrambler_t;

typedef enum { SCRAMBLE_ALGO_CISSA, SCRAMBLE_ALGO_CSA2 } scramble_algo_t;

#define SCRAMBLE_PARITY_EVEN 0
#define SCRAMBLE_PARITY_ODD 1

scrambler_t *scrambler_new(scramble_algo_t algo);
void scrambler_free(scrambler_t *s);

/* control word length for algo, in bytes: 16 for CISSA, 8 for CSA2 */
size_t scrambler_cw_len(scramble_algo_t algo);

/* load control word for 1 parity slot (SCRAMBLE_PARITY_EVEN/_ODD).
   0 on success, -1 on bad parity/length or backend unavailable */
int scrambler_set_key(scrambler_t *s, int parity, const unsigned char *cw, size_t cw_len);

/* scrambles one 188-byte TS packet in place with given parity's key,
   sets transport_scrambling_control bits.
   ret: -1 if that parity has no key loaded or backend fails */
int scrambler_encrypt_packet(scrambler_t *s, unsigned char pkt[188], int parity);

#endif
