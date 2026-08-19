/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <dlfcn.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

#include "../log.h"

#include "csa2.h"

/* local ABI, not linked against libdvbcsa-dev: dlopen'd at runtime so the
   package can carry CSA2 support without a hard dependency */
struct dvbcsa_key_s;
struct dvbcsa_bs_key_s;
struct dvbcsa_bs_batch_s {
  unsigned char *data;
  unsigned int len;
};

typedef struct dvbcsa_key_s *(*dvbcsa_key_alloc_fn)(void);
typedef void (*dvbcsa_key_free_fn)(struct dvbcsa_key_s *);
typedef void (*dvbcsa_key_set_fn)(const unsigned char cw[8], struct dvbcsa_key_s *);
typedef void (*dvbcsa_crypt_fn)(const struct dvbcsa_key_s *, unsigned char *, unsigned int);
typedef struct dvbcsa_bs_key_s *(*dvbcsa_bs_key_alloc_fn)(void);
typedef void (*dvbcsa_bs_key_free_fn)(struct dvbcsa_bs_key_s *);
typedef void (*dvbcsa_bs_key_set_fn)(const unsigned char cw[8], struct dvbcsa_bs_key_s *);
typedef unsigned int (*dvbcsa_bs_batch_size_fn)(void);
typedef void (*dvbcsa_bs_crypt_fn)(const struct dvbcsa_bs_key_s *, const struct dvbcsa_bs_batch_s *, unsigned int);

static dvbcsa_key_alloc_fn p_dvbcsa_key_alloc;
static dvbcsa_key_free_fn p_dvbcsa_key_free;
static dvbcsa_key_set_fn p_dvbcsa_key_set;
static dvbcsa_crypt_fn p_dvbcsa_decrypt;
static dvbcsa_crypt_fn p_dvbcsa_encrypt;
static dvbcsa_bs_key_alloc_fn p_dvbcsa_bs_key_alloc;
static dvbcsa_bs_key_free_fn p_dvbcsa_bs_key_free;
static dvbcsa_bs_key_set_fn p_dvbcsa_bs_key_set;
static dvbcsa_bs_batch_size_fn p_dvbcsa_bs_batch_size;
static dvbcsa_bs_crypt_fn p_dvbcsa_bs_decrypt;
static dvbcsa_bs_crypt_fn p_dvbcsa_bs_encrypt;

static atomic_bool csa2_dl_ready;
static bool csa2_dl_ok;

/* resolves all symbols or none: a partial match means an incompatible/broken
   libdvbcsa, treated the same as not installed */
static bool csa2_dl_resolve(void *h) {
  p_dvbcsa_key_alloc = (dvbcsa_key_alloc_fn)dlsym(h, "dvbcsa_key_alloc");
  p_dvbcsa_key_free = (dvbcsa_key_free_fn)dlsym(h, "dvbcsa_key_free");
  p_dvbcsa_key_set = (dvbcsa_key_set_fn)dlsym(h, "dvbcsa_key_set");
  p_dvbcsa_decrypt = (dvbcsa_crypt_fn)dlsym(h, "dvbcsa_decrypt");
  p_dvbcsa_encrypt = (dvbcsa_crypt_fn)dlsym(h, "dvbcsa_encrypt");
  p_dvbcsa_bs_key_alloc = (dvbcsa_bs_key_alloc_fn)dlsym(h, "dvbcsa_bs_key_alloc");
  p_dvbcsa_bs_key_free = (dvbcsa_bs_key_free_fn)dlsym(h, "dvbcsa_bs_key_free");
  p_dvbcsa_bs_key_set = (dvbcsa_bs_key_set_fn)dlsym(h, "dvbcsa_bs_key_set");
  p_dvbcsa_bs_batch_size = (dvbcsa_bs_batch_size_fn)dlsym(h, "dvbcsa_bs_batch_size");
  p_dvbcsa_bs_decrypt = (dvbcsa_bs_crypt_fn)dlsym(h, "dvbcsa_bs_decrypt");
  p_dvbcsa_bs_encrypt = (dvbcsa_bs_crypt_fn)dlsym(h, "dvbcsa_bs_encrypt");
  return p_dvbcsa_key_alloc && p_dvbcsa_key_free && p_dvbcsa_key_set && p_dvbcsa_decrypt &&
         p_dvbcsa_encrypt && p_dvbcsa_bs_key_alloc && p_dvbcsa_bs_key_free && p_dvbcsa_bs_key_set &&
         p_dvbcsa_bs_batch_size && p_dvbcsa_bs_decrypt && p_dvbcsa_bs_encrypt;
}

/* first caller wins, redundant concurrent loads are harmless (dlopen/dlsym are
   themselves thread-safe and idempotent for the same path) - csa2_dl_ready is
   the release/acquire fence that makes the pointer table above visible */
static bool csa2_dl_ensure(void) {
  void *h;

  if (atomic_load_explicit(&csa2_dl_ready, memory_order_acquire))
    return csa2_dl_ok;

  h = dlopen("libdvbcsa.so.1", RTLD_NOW);
  if (!h)
    h = dlopen("libdvbcsa.so", RTLD_NOW);

  if (h) {
    if (csa2_dl_resolve(h)) {
      csa2_dl_ok = true;
    } else {
      dlclose(h);
    }
  }

  if (!csa2_dl_ok)
    log_line("csa2: libdvbcsa not available at runtime (%s), CSA2 scrambling unavailable", dlerror());

  atomic_store_explicit(&csa2_dl_ready, true, memory_order_release);
  return csa2_dl_ok;
}

struct csa2_key {
  struct dvbcsa_key_s *k;
  struct dvbcsa_bs_key_s *bsk;
};

csa2_key_t *csa2_key_new(const unsigned char cw[CSA2_CW_LEN]) {
  csa2_key_t *k;

  if (!csa2_dl_ensure())
    return NULL;

  k = calloc(1, sizeof *k);
  if (!k)
    return NULL;
  k->k = p_dvbcsa_key_alloc();
  k->bsk = p_dvbcsa_bs_key_alloc();
  if (!k->k || !k->bsk) {
    p_dvbcsa_key_free(k->k);
    p_dvbcsa_bs_key_free(k->bsk);
    free(k);
    return NULL;
  }
  p_dvbcsa_key_set(cw, k->k);
  p_dvbcsa_bs_key_set(cw, k->bsk);
  return k;
}

void csa2_key_free(csa2_key_t *k) {
  if (!k)
    return;
  p_dvbcsa_key_free(k->k);
  p_dvbcsa_bs_key_free(k->bsk);
  free(k);
}

void csa2_encrypt_block(csa2_key_t *k, unsigned char *data, size_t len) {
  p_dvbcsa_encrypt(k->k, data, (unsigned int)len);
}

void csa2_decrypt_block(csa2_key_t *k, unsigned char *data, size_t len) {
  p_dvbcsa_decrypt(k->k, data, (unsigned int)len);
}

unsigned csa2_batch_size(void) {
  if (!csa2_dl_ensure())
    return 0;
  return p_dvbcsa_bs_batch_size();
}

/* maxlen must be a multiple of 8 per libdvbcsa's contract, rounding batch's own longest entry up covers every entry's residue termination
   in one call, verified against the single-packet API on mixed non-8-aligned lengths (real TS payloads vary with adaptation field size) */
static unsigned int csa2_batch_maxlen(const csa2_batch_entry_t *entries, unsigned n) {
  unsigned int maxlen = 0;
  for (unsigned i = 0; i < n; i++)
    if ((unsigned int)entries[i].len > maxlen)
      maxlen = (unsigned int)entries[i].len;
  return (maxlen + 7u) & ~7u;
}

void csa2_encrypt_batch(csa2_key_t *k, csa2_batch_entry_t *entries, unsigned n) {
  struct dvbcsa_bs_batch_s batch[n + 1];
  for (unsigned i = 0; i < n; i++) {
    batch[i].data = entries[i].data;
    batch[i].len = (unsigned int)entries[i].len;
  }
  batch[n].data = NULL;
  p_dvbcsa_bs_encrypt(k->bsk, batch, csa2_batch_maxlen(entries, n));
}

void csa2_decrypt_batch(csa2_key_t *k, csa2_batch_entry_t *entries, unsigned n) {
  struct dvbcsa_bs_batch_s batch[n + 1];
  for (unsigned i = 0; i < n; i++) {
    batch[i].data = entries[i].data;
    batch[i].len = (unsigned int)entries[i].len;
  }
  batch[n].data = NULL;
  p_dvbcsa_bs_decrypt(k->bsk, batch, csa2_batch_maxlen(entries, n));
}
