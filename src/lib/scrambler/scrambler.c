/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "cissa.h"
#include "csa2.h"

#include "scrambler.h"

#define SCRAMBLER_QUEUE_MODE_NONE 0
#define SCRAMBLER_QUEUE_MODE_ENCRYPT 1
#define SCRAMBLER_QUEUE_MODE_DECRYPT 2

/* total queued entries (crypto + passthrough) can outrun crypto batch size during long unscrambled/no-key runs.
   this bounds it, well above queue_batch_size, so passthrough-heavy streams never overflow the array.
   hitting it just forces an early flush */
#define SCRAMBLER_QUEUE_CAP_MULTIPLIER 16u

typedef struct {
  unsigned char pkt[188];
  int needs_crypto;
  size_t payload_off, payload_size;
} scrambler_queue_entry_t;

struct scrambler {
  scramble_algo_t algo;
  cissa_key_t *cissa_key[2]; /* CISSA only */
  csa2_key_t *csa2_key[2];   /* CSA2 only */
  int have_key[2];

  scrambler_queue_entry_t *queue;
  unsigned queue_batch_size; /* csa2_batch_size(); 0 = no batching backend */
  unsigned queue_cap;        /* allocated s->queue length */
  unsigned queue_len;
  unsigned queue_scrambled_count;
  int queue_parity;
  int queue_mode;
};

static void scrambler_queue_flush(scrambler_t *s, scrambler_emit_cb emit, void *ctx);

scrambler_t *scrambler_new(scramble_algo_t algo) {
  scrambler_t *s = calloc(1, sizeof *s);
  if (!s)
    return NULL;
  s->algo = algo;
  s->queue_parity = -1;
  s->queue_mode = SCRAMBLER_QUEUE_MODE_NONE;
  if (algo == SCRAMBLE_ALGO_CSA2) {
    s->queue_batch_size = csa2_batch_size();
    if (s->queue_batch_size > 0) {
      s->queue_cap = s->queue_batch_size * SCRAMBLER_QUEUE_CAP_MULTIPLIER;
      s->queue = calloc(s->queue_cap, sizeof *s->queue);
      if (!s->queue) {
        free(s);
        return NULL;
      }
    }
  }
  return s;
}

void scrambler_free(scrambler_t *s) {
  if (!s)
    return;
  csa2_key_free(s->csa2_key[SCRAMBLE_PARITY_EVEN]);
  csa2_key_free(s->csa2_key[SCRAMBLE_PARITY_ODD]);
  cissa_key_free(s->cissa_key[SCRAMBLE_PARITY_EVEN]);
  cissa_key_free(s->cissa_key[SCRAMBLE_PARITY_ODD]);
  free(s->queue);
  free(s);
}

size_t scrambler_cw_len(scramble_algo_t algo) {
  return algo == SCRAMBLE_ALGO_CISSA ? (size_t)CISSA_CW_LEN : (size_t)CSA2_CW_LEN;
}

int scrambler_set_key(scrambler_t *s, int parity, const unsigned char *cw, size_t cw_len, scrambler_emit_cb emit, void *ctx) {
  if (!s || (parity != SCRAMBLE_PARITY_EVEN && parity != SCRAMBLE_PARITY_ODD))
    return -1;
  if (cw_len != scrambler_cw_len(s->algo))
    return -1;

  /* scrambler_queue_flush() below looks up s->csa2_key[parity] at flush time, not at
     enqueue time - a batch already pending under this parity must go out under the
     still-current key before it's replaced, or its packets get the new key instead */
  if (s->queue_scrambled_count > 0 && s->queue_parity == parity)
    scrambler_queue_flush(s, emit, ctx);

  if (s->algo == SCRAMBLE_ALGO_CISSA) {
    cissa_key_t *k = cissa_key_new(cw);
    if (!k)
      return -1;
    cissa_key_free(s->cissa_key[parity]);
    s->cissa_key[parity] = k;
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
  size_t payload_off, payload_size;

  if (!s || (parity != SCRAMBLE_PARITY_EVEN && parity != SCRAMBLE_PARITY_ODD) || !s->have_key[parity])
    return -1;

  payload_off = ts_payload_offset(pkt);
  if (payload_off >= 188)
    return 0; /* AF fills the packet, nothing to scramble, leave control bits at 00 */
  payload_size = 188 - payload_off;

  if (s->algo == SCRAMBLE_ALGO_CSA2) {
    /* classic DVB-CSA: libdvbcsa's single-packet API runs its residue termination
       stage for any length, no block alignment needed (unlike its bitslice batch API) */
    pkt[3] = (unsigned char)((pkt[3] & 0x3F) | (parity == SCRAMBLE_PARITY_ODD ? 0xC0 : 0x80));
    csa2_encrypt_block(s->csa2_key[parity], pkt + payload_off, payload_size);
    return 0;
  } else {
    /* CISSA: TS 103 127 clause 6.3.2, AES-CBC without stealing, needs whole 16-byte blocks. trailing residual bytes stay clear. */
    size_t enc_size = payload_size - (payload_size % 16u);
    if (enc_size == 0)
      return 0; /* payload smaller than one cipher block: nothing to scramble, leave control bits at 00 */
    pkt[3] = (unsigned char)((pkt[3] & 0x3F) | (parity == SCRAMBLE_PARITY_ODD ? 0xC0 : 0x80));
    return cissa_encrypt_block(s->cissa_key[parity], pkt + payload_off, enc_size);
  }
}

int scrambler_decrypt_packet(scrambler_t *s, unsigned char pkt[188]) {
  unsigned ctrl = (pkt[3] >> 6) & 0x3;
  size_t payload_off, payload_size;
  int parity;

  if (ctrl == 0)
    return 0; /* not scrambled */
  if (ctrl == 1)
    return -1; /* reserved value, this scrambler never produces it */
  parity = (ctrl == 3) ? SCRAMBLE_PARITY_ODD : SCRAMBLE_PARITY_EVEN;

  if (!s || !s->have_key[parity])
    return -1;

  payload_off = ts_payload_offset(pkt);
  if (payload_off >= 188) {
    pkt[3] &= 0x3F; /* AF fills the packet, nothing was ever scrambled here */
    return 0;
  }
  payload_size = 188 - payload_off;

  if (s->algo == SCRAMBLE_ALGO_CSA2) {
    /* mirrors scrambler_encrypt_packet: libdvbcsa's residue termination covers any length, no block alignment needed */
    csa2_decrypt_block(s->csa2_key[parity], pkt + payload_off, payload_size);
    pkt[3] &= 0x3F;
    return 0;
  } else {
    size_t dec_size = payload_size - (payload_size % 16u);
    if (dec_size == 0) {
      pkt[3] &= 0x3F; /* payload smaller than one cipher block: never scrambled */
      return 0;
    }
    if (cissa_decrypt_block(s->cissa_key[parity], pkt + payload_off, dec_size) != 0)
      return -1;
    pkt[3] &= 0x3F;
    return 0;
  }
}

static void scrambler_queue_flush(scrambler_t *s, scrambler_emit_cb emit, void *ctx) {
  unsigned i;
  if (s->queue_len == 0)
    return;
  if (s->queue_scrambled_count > 0) {
    csa2_batch_entry_t entries[s->queue_scrambled_count];
    unsigned ei = 0;
    for (i = 0; i < s->queue_len; i++) {
      if (s->queue[i].needs_crypto) {
        entries[ei].data = s->queue[i].pkt + s->queue[i].payload_off;
        entries[ei].len = s->queue[i].payload_size;
        ei++;
      }
    }
    if (s->queue_mode == SCRAMBLER_QUEUE_MODE_ENCRYPT)
      csa2_encrypt_batch(s->csa2_key[s->queue_parity], entries, s->queue_scrambled_count);
    else
      csa2_decrypt_batch(s->csa2_key[s->queue_parity], entries, s->queue_scrambled_count);
  }
  for (i = 0; i < s->queue_len; i++)
    emit(ctx, s->queue[i].pkt);
  s->queue_len = 0;
  s->queue_scrambled_count = 0;
  s->queue_parity = -1;
  s->queue_mode = SCRAMBLER_QUEUE_MODE_NONE;
}

/* queue_cap == 0 (CISSA, or CSA2 without libdvbcsa): no batching backend, emit immediately.
   needs_crypto entries never reach here, @see scrambler_encrypt_packet_queued/scrambler_decrypt_packet_queued. */
static void scrambler_queue_push(scrambler_t *s, const unsigned char pkt[188], int needs_crypto, size_t payload_off, size_t payload_size, scrambler_emit_cb emit, void *ctx) {
  scrambler_queue_entry_t *e;
  int mode, parity;
  if (s->queue_cap == 0) {
    emit(ctx, pkt);
    return;
  }
  mode = s->queue_mode;
  parity = s->queue_parity;
  if (s->queue_len == s->queue_cap) {
    /* flush resets mode/parity to (NONE,-1): restore ensure_batch's commit, else
       push below lands under reset */
    scrambler_queue_flush(s, emit, ctx);
    if (needs_crypto) {
      s->queue_mode = mode;
      s->queue_parity = parity;
    }
  }
  e = &s->queue[s->queue_len];
  memcpy(e->pkt, pkt, 188);
  e->needs_crypto = needs_crypto;
  e->payload_off = payload_off;
  e->payload_size = payload_size;
  s->queue_len++;
  if (needs_crypto) {
    s->queue_scrambled_count++;
    if (s->queue_scrambled_count == s->queue_batch_size)
      scrambler_queue_flush(s, emit, ctx);
  }
}

/* flushes first if a crypto batch is already accumulating under a different mode/parity than what's to be queued.
   keeps every batch's entries under one shared key */
static void scrambler_queue_ensure_batch(scrambler_t *s, int mode, int parity, scrambler_emit_cb emit, void *ctx) {
  if (s->queue_scrambled_count > 0 && (s->queue_mode != mode || s->queue_parity != parity))
    scrambler_queue_flush(s, emit, ctx);
  s->queue_mode = mode;
  s->queue_parity = parity;
}

int scrambler_encrypt_packet_queued(scrambler_t *s, unsigned char pkt[188], int parity, scrambler_emit_cb emit, void *ctx) {
  size_t payload_off, payload_size;

  if (!s || (parity != SCRAMBLE_PARITY_EVEN && parity != SCRAMBLE_PARITY_ODD) || !s->have_key[parity])
    return -1;

  if (s->queue_cap == 0) {
    int ret = scrambler_encrypt_packet(s, pkt, parity);
    if (ret == 0)
      emit(ctx, pkt);
    return ret;
  }

  payload_off = ts_payload_offset(pkt);
  if (payload_off >= 188) {
    scrambler_queue_push(s, pkt, 0, 0, 0, emit, ctx);
    return 0;
  }
  payload_size = 188 - payload_off;

  scrambler_queue_ensure_batch(s, SCRAMBLER_QUEUE_MODE_ENCRYPT, parity, emit, ctx);
  pkt[3] = (unsigned char)((pkt[3] & 0x3F) | (parity == SCRAMBLE_PARITY_ODD ? 0xC0 : 0x80));
  scrambler_queue_push(s, pkt, 1, payload_off, payload_size, emit, ctx);
  return 0;
}

int scrambler_decrypt_packet_queued(scrambler_t *s, unsigned char pkt[188], scrambler_emit_cb emit, void *ctx) {
  unsigned ctrl = (pkt[3] >> 6) & 0x3;
  size_t payload_off, payload_size;
  int parity;

  if (!s)
    return -1;

  if (ctrl == 0) {
    scrambler_queue_push(s, pkt, 0, 0, 0, emit, ctx);
    return 0;
  }
  if (ctrl == 1)
    return -1;
  parity = (ctrl == 3) ? SCRAMBLE_PARITY_ODD : SCRAMBLE_PARITY_EVEN;

  if (!s->have_key[parity])
    return -1;

  if (s->queue_cap == 0) {
    int ret = scrambler_decrypt_packet(s, pkt);
    if (ret == 0)
      emit(ctx, pkt);
    return ret;
  }

  payload_off = ts_payload_offset(pkt);
  if (payload_off >= 188) {
    pkt[3] &= 0x3F;
    scrambler_queue_push(s, pkt, 0, 0, 0, emit, ctx);
    return 0;
  }
  payload_size = 188 - payload_off;

  scrambler_queue_ensure_batch(s, SCRAMBLER_QUEUE_MODE_DECRYPT, parity, emit, ctx);
  pkt[3] &= 0x3F;
  scrambler_queue_push(s, pkt, 1, payload_off, payload_size, emit, ctx);
  return 0;
}

void scrambler_passthrough_queued(scrambler_t *s, const unsigned char pkt[188], scrambler_emit_cb emit, void *ctx) {
  if (!s) {
    emit(ctx, pkt);
    return;
  }
  scrambler_queue_push(s, pkt, 0, 0, 0, emit, ctx);
}

void scrambler_flush(scrambler_t *s, scrambler_emit_cb emit, void *ctx) {
  if (s)
    scrambler_queue_flush(s, emit, ctx);
}
