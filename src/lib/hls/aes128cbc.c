/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "aes128cbc.h"

#include <string.h>

#define AES_ROUNDS 10

typedef struct {
  unsigned char sbox[256];
  unsigned char inv[256];
} aes_tables_t;

static unsigned char xtime(unsigned char a) { return (unsigned char)((a << 1) ^ ((a & 0x80) ? 0x1B : 0)); }

static unsigned char rotl8(unsigned char v, int s) { return (unsigned char)((v << s) | (v >> (8 - s))); }

/* GF(2^8) walk over generator 3, avoids typing tables */
static void tables_build(aes_tables_t *t) {
  unsigned char p = 1;
  unsigned char q = 1;
  do {
    p = (unsigned char)(p ^ xtime(p));
    q ^= (unsigned char)(q << 1);
    q ^= (unsigned char)(q << 2);
    q ^= (unsigned char)(q << 4);
    if (q & 0x80) q ^= 0x09;
    t->sbox[p] = (unsigned char)(q ^ rotl8(q, 1) ^ rotl8(q, 2) ^ rotl8(q, 3) ^ rotl8(q, 4) ^ 0x63);
  } while (p != 1);
  t->sbox[0] = 0x63;
  for (int i = 0; i < 256; i++) t->inv[t->sbox[i]] = (unsigned char)i;
}

static void key_expand(const aes_tables_t *t, const unsigned char key[16], unsigned char rk[(AES_ROUNDS + 1) * 16]) {
  unsigned char rcon = 1;
  memcpy(rk, key, 16);
  for (int i = 16; i < (AES_ROUNDS + 1) * 16; i += 4) {
    unsigned char w[4];
    memcpy(w, rk + i - 4, 4);
    if (i % 16 == 0) {
      unsigned char first = w[0];
      w[0] = (unsigned char)(t->sbox[w[1]] ^ rcon);
      w[1] = t->sbox[w[2]];
      w[2] = t->sbox[w[3]];
      w[3] = t->sbox[first];
      rcon = xtime(rcon);
    }
    for (int j = 0; j < 4; j++) rk[i + j] = (unsigned char)(rk[i - 16 + j] ^ w[j]);
  }
}

static void add_round_key(unsigned char *s, const unsigned char *rk) {
  for (int i = 0; i < 16; i++) s[i] ^= rk[i];
}

static void inv_shift_sub(const aes_tables_t *t, unsigned char *s) {
  unsigned char o[16];
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) o[((c + r) % 4) * 4 + r] = t->inv[s[c * 4 + r]];
  memcpy(s, o, 16);
}

static void inv_mix_columns(unsigned char *s) {
  for (int c = 0; c < 4; c++) {
    unsigned char *col = s + c * 4;
    unsigned char m2[4];
    unsigned char m4[4];
    unsigned char m8[4];
    unsigned char a[4];
    for (int i = 0; i < 4; i++) {
      a[i] = col[i];
      m2[i] = xtime(a[i]);
      m4[i] = xtime(m2[i]);
      m8[i] = xtime(m4[i]);
    }
    for (int i = 0; i < 4; i++) {
      int i1 = (i + 1) % 4;
      int i2 = (i + 2) % 4;
      int i3 = (i + 3) % 4;
      unsigned char x14 = (unsigned char)(m8[i] ^ m4[i] ^ m2[i]);
      unsigned char x11 = (unsigned char)(m8[i1] ^ m2[i1] ^ a[i1]);
      unsigned char x13 = (unsigned char)(m8[i2] ^ m4[i2] ^ a[i2]);
      unsigned char x9 = (unsigned char)(m8[i3] ^ a[i3]);
      col[i] = (unsigned char)(x14 ^ x11 ^ x13 ^ x9);
    }
  }
}

static void block_decrypt(const aes_tables_t *t, const unsigned char *rk, unsigned char *s) {
  add_round_key(s, rk + AES_ROUNDS * 16);
  for (int r = AES_ROUNDS - 1; r >= 1; r--) {
    inv_shift_sub(t, s);
    add_round_key(s, rk + r * 16);
    inv_mix_columns(s);
  }
  inv_shift_sub(t, s);
  add_round_key(s, rk);
}

int aes128cbc_decrypt(const unsigned char key[16], const unsigned char iv[16], unsigned char *data, size_t len) {
  aes_tables_t t;
  unsigned char rk[(AES_ROUNDS + 1) * 16];
  unsigned char prev[16];
  unsigned char cur[16];

  if (!key || !iv || !data || len == 0 || len % 16) return -1;
  tables_build(&t);
  key_expand(&t, key, rk);
  memcpy(prev, iv, 16);
  for (size_t off = 0; off < len; off += 16) {
    memcpy(cur, data + off, 16);
    block_decrypt(&t, rk, data + off);
    for (int i = 0; i < 16; i++) data[off + i] ^= prev[i];
    memcpy(prev, cur, 16);
  }
  return 0;
}

int aes128cbc_decrypt_pkcs7(const unsigned char key[16], const unsigned char iv[16], unsigned char *data, size_t *len) {
  unsigned pad;
  if (aes128cbc_decrypt(key, iv, data, *len)) return -1;
  pad = data[*len - 1];
  if (pad == 0 || pad > 16 || pad > *len) return -1;
  for (unsigned i = 1; i <= pad; i++)
    if (data[*len - i] != pad) return -1;
  *len -= pad;
  return 0;
}
