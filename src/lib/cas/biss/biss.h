/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_BISS_H
#define DVBIPITOOLS_LIB_CAS_BISS_H

#define BISS1_CA_SYSTEM_ID 0x2600 /* EBU Tech 3292 rev. 2, ITU-T J.96 */
#define BISS2_CA_SYSTEM_ID 0x2602 /* EBU Tech 3292 v3.0, Mode 1/E */

#define BISS_KEY_LEN 16 /* SW/ESW/ID, all 128-bit */

/* 32 hex chars, optional 0x prefix. 0 ok, -1 bad */
int biss_parse_hex16(const char *hex, unsigned char out[BISS_KEY_LEN]);

/* AES-128-ECB, no padding. ESW = AES128enc(id, sw). 0 ok, -1 bad args/no backend */
int biss_esw_encrypt(const unsigned char id[BISS_KEY_LEN], const unsigned char sw[BISS_KEY_LEN], unsigned char esw_out[BISS_KEY_LEN]);

int biss_esw_decrypt(const unsigned char id[BISS_KEY_LEN], const unsigned char esw[BISS_KEY_LEN], unsigned char sw_out[BISS_KEY_LEN]);

#define BISS1_KEY_LEN 8 /* full CSA1 CW, incl. 2 checksum bytes */
#define BISS1_SW_LEN 6  /* legacy Session Word: 48 bits free entropy */
#define BISS1_ID_LEN 7  /* injected/buried ID, 56-bit */
#define BISS1_ESW_LEN 8

/* 12 hex chars (optional 0x prefix) -> free-entropy bytes at cw_out[0..2]/[4..6],
   cw_out[3]/[7] filled with the CSA1 checksum convention. 0 ok, -1 bad length/chars */
int biss1_parse_sw(const char *hex, unsigned char cw_out[BISS1_KEY_LEN]);

/* 14 hex chars, optional 0x prefix. 0 ok, -1 bad */
int biss1_parse_id(const char *hex, unsigned char out[BISS1_ID_LEN]);

/* 16 hex chars, optional 0x prefix. 0 ok, -1 bad */
int biss1_parse_esw(const char *hex, unsigned char out[BISS1_ESW_LEN]);

/* cw_out[3]/[7] get CSA1 checksums */
void biss1_sw_to_cw(const unsigned char sw[BISS1_SW_LEN], unsigned char cw_out[BISS1_KEY_LEN]);

/* Tech 3292 rev. 2 Mode E. buried: rotate SW per P(). cw_out checksummed. 0 ok, -1 bad args */
int biss1_esw_decrypt(const unsigned char id[BISS1_ID_LEN], const unsigned char esw[BISS1_ESW_LEN], int buried, unsigned char cw_out[BISS1_KEY_LEN]);

/* random outer bits per mgmt center proc, ESW differs per call. 0 ok, -1 bad args/no entropy */
int biss1_esw_encrypt(const unsigned char id[BISS1_ID_LEN], const unsigned char cw[BISS1_KEY_LEN], int buried, unsigned char esw_out[BISS1_ESW_LEN]);

#endif
