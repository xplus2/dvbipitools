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

/* CW length, bytes: 16 CISSA, 8 CSA2 */
size_t scrambler_cw_len(scramble_algo_t algo);

/* fires once per packet, in order, on *_queued emit (immediate or batched). pkt: complete
   188B, ready to send */
typedef void (*scrambler_emit_cb)(void *ctx, const unsigned char pkt[188]);

/* flushes any pending queued batch for this parity first, under the still-current key -
   else those packets would silently pick up the new key at flush time. 0: ok. -1: bad
   parity/length or backend unavailable */
int scrambler_set_key(scrambler_t *s, int parity, const unsigned char *cw, size_t cw_len, scrambler_emit_cb emit, void *ctx);

/* -1: no key loaded for parity, or backend fail */
int scrambler_encrypt_packet(scrambler_t *s, unsigned char pkt[188], int parity);

/* parity read from pkt's own control bits. 0: unscrambled (untouched) or decrypted (control
   bits cleared). -1: no key for that parity, reserved control value 01, or backend fail -
   packet untouched */
int scrambler_decrypt_packet(scrambler_t *s, unsigned char pkt[188]);

/* pkt copied, caller's buffer reusable on return. CSA2+SIMD backend: queued, emitted with
   its batch on fill / parity change / scrambler_flush() - not necessarily before this
   returns. CISSA, or CSA2 w/o libdvbcsa: emitted immediately, no batching benefit.
   -1: no key loaded, not queued/emitted. 0 otherwise */
int scrambler_encrypt_packet_queued(scrambler_t *s, unsigned char pkt[188], int parity, scrambler_emit_cb emit, void *ctx);

/* same queued/immediate split as scrambler_encrypt_packet_queued. parity from pkt's own
   control bits, same failure cases as scrambler_decrypt_packet */
int scrambler_decrypt_packet_queued(scrambler_t *s, unsigned char pkt[188], scrambler_emit_cb emit, void *ctx);

/* no crypto needed, but keeps pkt's position in whatever *_queued batch is pending - else
   it could jump ahead of still-queued earlier packets on the same pid */
void scrambler_passthrough_queued(scrambler_t *s, const unsigned char pkt[188], scrambler_emit_cb emit, void *ctx);

/* flushes+emits anything held by *_queued/passthrough_queued, in order. call at stream end,
   or wherever held-back latency is unacceptable (e.g. after a PCR packet). safe: nothing
   queued, or s NULL */
void scrambler_flush(scrambler_t *s, scrambler_emit_cb emit, void *ctx);

#endif
