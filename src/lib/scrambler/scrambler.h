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

/* invoked once per packet, in original order, whenever a *_queued call below
   emits (immediately, or later via a batch flush). pkt is a fully finished,
   188-byte packet ready to send/write/mux onward. */
typedef void (*scrambler_emit_cb)(void *ctx, const unsigned char pkt[188]);

/* load control word for 1 parity slot (SCRAMBLE_PARITY_EVEN/_ODD). if a
   *_queued batch is still pending for this same parity, flushes it first
   (via emit/ctx) under the key still in effect - otherwise those packets
   would silently pick up the new key at flush time instead of the one
   they were queued under.
   0 on success, -1 on bad parity/length or backend unavailable */
int scrambler_set_key(scrambler_t *s, int parity, const unsigned char *cw, size_t cw_len, scrambler_emit_cb emit, void *ctx);

/* scrambles one 188-byte TS packet in place with given parity's key,
   sets transport_scrambling_control bits. ret: -1 if that parity has no key loaded or backend fails */
int scrambler_encrypt_packet(scrambler_t *s, unsigned char pkt[188], int parity);

/* descrambles one 188-byte TS packet in place, using whichever parity slot its own
   transport_scrambling_control bits (byte 3, top 2 bits) select. 00 = not scrambled,
   no-op, returns 0. Clears the control bits to 00 on a successful decrypt.
   ret: 0 not scrambled or successfully decrypted, -1 marked scrambled but that
   parity has no key loaded, the control value is the reserved "01", or the
   backend fails - packet left untouched in all -1 cases */
int scrambler_decrypt_packet(scrambler_t *s, unsigned char pkt[188]);

/* CSA2 with a SIMD bitslice backend available: queues pkt (a copy - the
   caller's buffer is free to reuse right after this call returns) for
   batched hardware-accelerated encryption, calling emit() for it (and any
   other packets in the same batch) once the batch fills, the parity
   changes, or scrambler_flush() is called - NOT necessarily before this
   call returns. CISSA, or CSA2 without libdvbcsa: encrypts immediately and
   calls emit() before returning (batching gives these no benefit).
   ret: -1 if that parity has no key loaded, packet not queued/emitted in
   that case; 0 otherwise. */
int scrambler_encrypt_packet_queued(scrambler_t *s, unsigned char pkt[188], int parity, scrambler_emit_cb emit, void *ctx);

/* same queued/immediate split as scrambler_encrypt_packet_queued, for
   descrambling - parity is read from pkt's own scrambling control bits,
   same as scrambler_decrypt_packet. ret: -1 same failure cases as
   scrambler_decrypt_packet, packet not queued/emitted; 0 otherwise. */
int scrambler_decrypt_packet_queued(scrambler_t *s, unsigned char pkt[188], scrambler_emit_cb emit, void *ctx);

/* pkt needs no crypto (caller already decided) but must still keep its
   position relative to whatever *_queued above is currently batching for
   this scrambler, e.g. a CAS-registered pid whose key isn't loaded yet -
   without this, a byte-identical passthrough packet emitted directly could
   jump ahead of still-queued earlier packets on the same pid. */
void scrambler_passthrough_queued(scrambler_t *s, const unsigned char pkt[188], scrambler_emit_cb emit, void *ctx);

/* forces an immediate flush+emit of any packets held by *_queued/
   *_passthrough_queued calls, in original order. call at end of stream, and
   at any point where held-back latency is unacceptable (e.g. right after a
   PCR-bearing packet, so batching never delays PCR delivery). safe to call
   with nothing queued, or with s NULL. */
void scrambler_flush(scrambler_t *s, scrambler_emit_cb emit, void *ctx);

#endif
