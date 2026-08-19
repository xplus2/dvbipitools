/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "../../demux/crc32.h"
#include "../../mux/psi_build.h"

#include "ca_sections.h"

size_t biss_ca_build_entitlement_session_id_desc(unsigned esid, unsigned onid, unsigned char *out, size_t cap) {
  if (!out || cap < 6)
    return 0;
  out[0] = BISS_CA_ENTITLEMENT_SESSION_ID_DESC_TAG;
  out[1] = 4;
  psi_put16(out + 2, esid);
  psi_put16(out + 4, onid);
  return 6;
}

int biss_ca_parse_entitlement_session_id_desc(const unsigned char *in, size_t in_len, unsigned *esid_out, unsigned *onid_out) {
  if (!in || !esid_out || !onid_out || in_len < 6)
    return -1;
  if (in[0] != BISS_CA_ENTITLEMENT_SESSION_ID_DESC_TAG || in[1] < 4)
    return -1;
  *esid_out = ((unsigned)in[2] << 8) | in[3];
  *onid_out = ((unsigned)in[4] << 8) | in[5];
  return 0;
}

size_t biss_ca_build_session_data(const unsigned char sk[BISS_CA_SK_LEN], int parity, unsigned char *out, size_t cap) {
  if (!sk || !out || cap < BISS_CA_SESSION_DATA_LEN)
    return 0;
  out[0] = 0x00;
  out[1] = 0x16; /* reserved(4)+descriptor_length(12) = 22, the 2 descriptors below */
  out[2] = BISS_CA_SESSION_KEY_DESC_TAG;
  out[3] = 0x11; /* length 17: type/parity byte + 16-byte key */
  out[4] = (unsigned char)(parity & 1); /* session_key_type(7)=0 AES-128, parity(1) */
  memcpy(out + 5, sk, BISS_CA_SK_LEN);
  out[21] = BISS_CA_ENTITLEMENT_FLAGS_DESC_TAG;
  out[22] = 0x01;
  out[23] = 0x00; /* prevent_descrambled_forward/prevent_decoded_forward/insert_watermark: unenforced, always 0 */
  return BISS_CA_SESSION_DATA_LEN;
}

int biss_ca_parse_session_data(const unsigned char *in, size_t in_len, unsigned char sk_out[BISS_CA_SK_LEN], int *parity_out) {
  if (!in || !sk_out || !parity_out || in_len < BISS_CA_SESSION_DATA_LEN)
    return -1;
  if (in[2] != BISS_CA_SESSION_KEY_DESC_TAG || in[3] != 0x11)
    return -1;
  if ((in[4] >> 1) != 0)
    return -1; /* only AES-128 session_key_type supported */
  *parity_out = in[4] & 1;
  memcpy(sk_out, in + 5, BISS_CA_SK_LEN);
  if (in[21] != BISS_CA_ENTITLEMENT_FLAGS_DESC_TAG || in[22] != 0x01)
    return -1;
  return 0;
}

size_t biss_ca_build_emm_section(unsigned table_id, unsigned esid, unsigned version, unsigned section_number, unsigned last_section_number, unsigned onid, unsigned last_table_id, const biss_ca_emm_entry_t *entries, size_t n_entries, unsigned char *out, size_t cap) {
  size_t n = 0;
  size_t entry_size = BISS_CA_EKID_LEN + BISS_CA_RSA_BYTES;

  if (!out || !entries)
    return 0;
  if (14 + n_entries * entry_size + 4 > cap)
    return 0;

  out[n++] = (unsigned char)table_id;
  n += 2; /* section_length placeholder, psi_finish_section patches it */
  psi_put16(out + n, esid);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = (unsigned char)section_number;
  out[n++] = (unsigned char)last_section_number;
  psi_put16(out + n, onid);
  n += 2;
  out[n++] = (unsigned char)last_table_id;
  out[n++] = 0x00; /* emm_cipher_type(3)=RSA_2048_OAEP, entitlement_priv_data_loop(1)=off, reserved(4) */
  out[n++] = 0x00; /* reserved(4)+descriptor_length(12) hi: no top-level descriptors */
  out[n++] = 0x00; /* descriptor_length lo */

  for (size_t i = 0; i < n_entries; i++) {
    memcpy(out + n, entries[i].entitlement_key_id, BISS_CA_EKID_LEN);
    n += BISS_CA_EKID_LEN;
    memcpy(out + n, entries[i].encrypted_session_data, BISS_CA_RSA_BYTES);
    n += BISS_CA_RSA_BYTES;
  }

  return psi_finish_section(out, n, cap, 0xF0);
}

int biss_ca_parse_emm_section(const unsigned char *sec, size_t sec_len, biss_ca_emm_parsed_t *out) {
  unsigned emm_cipher_type, entitlement_priv_data_loop, descriptor_length;
  size_t n, remaining, entry_size = BISS_CA_EKID_LEN + BISS_CA_RSA_BYTES;

  if (!sec || !out || sec_len < 14 + 4)
    return -1;
  if (sec[0] < BISS_CA_EMM_TABLE_ID_MIN || sec[0] > BISS_CA_EMM_TABLE_ID_MAX)
    return -1;
  if (crc32_mpeg(sec, sec_len) != 0)
    return -1;

  out->table_id = sec[0];
  out->esid = ((unsigned)sec[3] << 8) | sec[4];
  out->onid = ((unsigned)sec[8] << 8) | sec[9];
  emm_cipher_type = (sec[11] >> 5) & 0x07;
  entitlement_priv_data_loop = (sec[11] >> 4) & 0x01;
  descriptor_length = (((unsigned)sec[12] & 0x0F) << 8) | sec[13];
  if (emm_cipher_type != 0 || entitlement_priv_data_loop)
    return -1; /* only RSA_2048_OAEP, no per-entry priv data (out of scope) */

  n = 14 + descriptor_length;
  if (n + 4 > sec_len)
    return -1;
  remaining = sec_len - 4 - n;
  if (remaining % entry_size != 0)
    return -1;

  out->entries = sec + n;
  out->n_entries = remaining / entry_size;
  return 0;
}

const unsigned char *biss_ca_emm_find_entry(const biss_ca_emm_parsed_t *emm, const unsigned char ekid[BISS_CA_EKID_LEN]) {
  if (!emm || !ekid)
    return NULL;
  for (size_t i = 0; i < emm->n_entries; i++) {
    size_t entry_size = BISS_CA_EKID_LEN + BISS_CA_RSA_BYTES;
    const unsigned char *entry = emm->entries + i * entry_size;
    if (memcmp(entry, ekid, BISS_CA_EKID_LEN) == 0)
      return entry + BISS_CA_EKID_LEN;
  }
  return NULL;
}

size_t biss_ca_build_ecm_section(unsigned esid, unsigned version, unsigned section_number, unsigned last_section_number, unsigned onid, int session_key_parity, const unsigned char iv[BISS_CA_IV_LEN], const unsigned char esw_even[BISS_CA_SW_LEN], const unsigned char esw_odd[BISS_CA_SW_LEN], unsigned char *out, size_t cap) {
  size_t n = 0;

  if (!out || !iv || !esw_even || !esw_odd)
    return 0;
  if (12 + 1 + BISS_CA_IV_LEN + 2 * BISS_CA_SW_LEN + 4 > cap)
    return 0;

  out[n++] = BISS_CA_ECM_TABLE_ID;
  n += 2;
  psi_put16(out + n, esid);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = (unsigned char)section_number;
  out[n++] = (unsigned char)last_section_number;
  psi_put16(out + n, onid);
  n += 2;
  out[n++] = 0x00; /* ecm_cipher_type(3)=AES_128_CBC, reserved(1), descriptor_length(12) hi=0 */
  out[n++] = 0x00; /* descriptor_length lo */
  out[n++] = (unsigned char)((session_key_parity & 1) << 7);
  memcpy(out + n, iv, BISS_CA_IV_LEN);
  n += BISS_CA_IV_LEN;
  memcpy(out + n, esw_even, BISS_CA_SW_LEN);
  n += BISS_CA_SW_LEN;
  memcpy(out + n, esw_odd, BISS_CA_SW_LEN);
  n += BISS_CA_SW_LEN;

  return psi_finish_section(out, n, cap, 0xF0);
}

int biss_ca_parse_ecm_section(const unsigned char *sec, size_t sec_len, biss_ca_ecm_parsed_t *out) {
  unsigned ecm_cipher_type, descriptor_length;
  size_t n;

  if (!sec || !out || sec_len < 12 + 1 + BISS_CA_IV_LEN + 2 * BISS_CA_SW_LEN + 4)
    return -1;
  if (sec[0] != BISS_CA_ECM_TABLE_ID)
    return -1;
  if (crc32_mpeg(sec, sec_len) != 0)
    return -1;

  out->esid = ((unsigned)sec[3] << 8) | sec[4];
  out->onid = ((unsigned)sec[8] << 8) | sec[9];
  ecm_cipher_type = (sec[10] >> 5) & 0x07;
  descriptor_length = (((unsigned)sec[10] & 0x0F) << 8) | sec[11];
  if (ecm_cipher_type != 0)
    return -1; /* only AES_128_CBC supported */

  n = 12 + descriptor_length;
  if (n + 1 + BISS_CA_IV_LEN + 2 * BISS_CA_SW_LEN + 4 > sec_len)
    return -1;

  out->session_key_parity = (sec[n] >> 7) & 1;
  n++;
  memcpy(out->iv, sec + n, BISS_CA_IV_LEN);
  n += BISS_CA_IV_LEN;
  memcpy(out->esw_even, sec + n, BISS_CA_SW_LEN);
  n += BISS_CA_SW_LEN;
  memcpy(out->esw_odd, sec + n, BISS_CA_SW_LEN);
  n += BISS_CA_SW_LEN;

  return 0;
}
