/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_ECM_PROFILE_PRIV_H
#define DIPIDESCRAMBLE_ECM_PROFILE_PRIV_H

#include "../ecm_profile.h"

/* shared across parse/validate/wire/crypto, defined once in common.c */

int cipher_is_ecb(ecm_cipher_t c);
int cipher_is_cbc(ecm_cipher_t c);
int cipher_is_gcm(ecm_cipher_t c);
int cipher_is_3des(ecm_cipher_t c);
int cipher_block_size(ecm_cipher_t c);
int cipher_key_len(ecm_cipher_t c);
crypto_ecm_cipher_t cipher_to_crypto(ecm_cipher_t c);

const ecm_header_t *find_header(const ecm_format_t *fmt, const char *id);
int is_reserved_id(const char *id);
size_t integrity_tag_wire_len(const ecm_profile_t *p);

/* 0: fixed token keyword, *out set. -1: not one, header id candidate */
int lookup_fixed_token(const char *name, ecm_token_kind_t *out);

#endif
