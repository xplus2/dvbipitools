/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_BISS_CA_SECTIONS_H
#define DVBIPITOOLS_LIB_CAS_BISS_CA_SECTIONS_H

#include <stddef.h>

#include "ca.h"

#define BISS_CA_EMM_TABLE_ID_MIN 0x81
#define BISS_CA_EMM_TABLE_ID_MAX 0x8F
#define BISS_CA_ECM_TABLE_ID 0x80
#define BISS_CA_ENTITLEMENT_SESSION_ID_DESC_TAG 0x80
#define BISS_CA_SESSION_KEY_DESC_TAG 0x81
#define BISS_CA_ENTITLEMENT_FLAGS_DESC_TAG 0x82

/* 3292-s1 Table 3, CA_descriptor private_data. (esid, onid) pair. shape actually used (SS4.2.1.4: scoped id per scrambler). 0 on overflow */
size_t biss_ca_build_entitlement_session_id_desc(unsigned esid, unsigned onid, unsigned char *out, size_t cap);

/* 0 ok, -1 malformed/wrong tag */
int biss_ca_parse_entitlement_session_id_desc(const unsigned char *in, size_t in_len, unsigned *esid_out, unsigned *onid_out);

/* Table 9: session_key_descriptor (AES-128, Table 10-11) + entitlement_flags_descriptor.
   Table 12: flags always 0, unenforced. fixed 24 bytes, the RSA-OAEP plaintext, <= BISS_CA_SESSION_DATA_MAX. 0 on overflow */
#define BISS_CA_SESSION_DATA_LEN 24
size_t biss_ca_build_session_data(const unsigned char sk[BISS_CA_SK_LEN], int parity, unsigned char *out, size_t cap);

/* 0 ok, -1 malformed/wrong length or tags */
int biss_ca_parse_session_data(const unsigned char *in, size_t in_len, unsigned char sk_out[BISS_CA_SK_LEN], int *parity_out);

typedef struct {
  unsigned char entitlement_key_id[BISS_CA_EKID_LEN];
  unsigned char encrypted_session_data[BISS_CA_RSA_BYTES];
} biss_ca_emm_entry_t;

/* Table 7. table_id: BISS_CA_EMM_TABLE_ID_MIN..MAX. emm_cipher_type fixed RSA_2048_OAEP, entitlement_priv_data_loop fixed off.
   0 on overflow = too many entries per section, caller splits  */
size_t biss_ca_build_emm_section(unsigned table_id, unsigned esid, unsigned version, unsigned section_number, unsigned last_section_number, unsigned onid, unsigned last_table_id, const biss_ca_emm_entry_t *entries, size_t n_entries, unsigned char *out, size_t cap);

typedef struct {
  unsigned table_id;
  unsigned esid;
  unsigned onid;
  const unsigned char *entries; /* EKID(8)+encrypted_session_data(256), n_entries repeats, points into caller's buf */
  size_t n_entries;
} biss_ca_emm_parsed_t;

/* 0 ok, -1 malformed/short/bad CRC/unsupported cipher type */
int biss_ca_parse_emm_section(const unsigned char *sec, size_t sec_len, biss_ca_emm_parsed_t *out);

/* linear scan for a matching entitlement_key_id. NULL if not found (encrypted_session_data ptr into emm->entries) */
const unsigned char *biss_ca_emm_find_entry(const biss_ca_emm_parsed_t *emm, const unsigned char ekid[BISS_CA_EKID_LEN]);

/* Table 13-14. ecm_cipher_type fixed AES_128_CBC, no descriptors. 0 on overflow */
size_t biss_ca_build_ecm_section(unsigned esid, unsigned version, unsigned section_number, unsigned last_section_number, unsigned onid, int session_key_parity, const unsigned char iv[BISS_CA_IV_LEN], const unsigned char esw_even[BISS_CA_SW_LEN], const unsigned char esw_odd[BISS_CA_SW_LEN], unsigned char *out, size_t cap);

typedef struct {
  unsigned esid;
  unsigned onid;
  int session_key_parity;
  unsigned char iv[BISS_CA_IV_LEN];
  unsigned char esw_even[BISS_CA_SW_LEN];
  unsigned char esw_odd[BISS_CA_SW_LEN];
} biss_ca_ecm_parsed_t;

/* 0 ok, -1 malformed/short/bad CRC/unsupported cipher type */
int biss_ca_parse_ecm_section(const unsigned char *sec, size_t sec_len, biss_ca_ecm_parsed_t *out);

#endif
